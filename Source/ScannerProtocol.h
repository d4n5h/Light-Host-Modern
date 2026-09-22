#pragma once
#include <juce_cryptography/juce_cryptography.h>
#include <filesystem>
#include <set>
#include <windows.h>

namespace lightHostModern::scan
{
inline constexpr int scannerProtocolVersion = 2;
inline constexpr int metadataCacheVersion = 2;
inline constexpr int batchItems = 64;
inline constexpr int maximumResponseBytes = 4 * 1024 * 1024;

inline juce::String filesystemFailure(const std::error_code& error)
{
    const bool native = error.category() == std::system_category();
    // Windows maps some network errors to the generic missing-path condition;
    // preserve the more specific native reason before consulting that mapping.
    if (native && (error.value() == ERROR_BAD_NETPATH || error.value() == ERROR_BAD_NET_NAME
        || error.value() == ERROR_NETWORK_UNREACHABLE || error.value() == ERROR_CONNECTION_UNAVAIL
        || error.value() == ERROR_NETNAME_DELETED)) return "network_unavailable";
    if (error == std::errc::permission_denied || (native && (error.value() == ERROR_ACCESS_DENIED
        || error.value() == ERROR_NETWORK_ACCESS_DENIED))) return "access_denied";
    if (error == std::errc::no_such_file_or_directory || (native && (error.value() == ERROR_FILE_NOT_FOUND
        || error.value() == ERROR_PATH_NOT_FOUND))) return "missing";
    if (error == std::errc::filename_too_long) return "path_too_long";
    return "enumeration";
}

inline juce::File batchFile(const juce::File& response, int index)
{
    return response.getSiblingFile(response.getFileName() + ".batch-" + juce::String(index) + ".xml");
}

inline juce::String fingerprint(const juce::File& module)
{
    std::vector<std::filesystem::path> entries;
    std::error_code error;
    const std::filesystem::path path(module.getFullPathName().toWideCharPointer());
    if (std::filesystem::is_directory(path, error))
    {
        std::filesystem::recursive_directory_iterator cursor(path, std::filesystem::directory_options::none, error), end;
        if (error) throw std::runtime_error("metadata_unavailable");
        for (; cursor != end; cursor.increment(error))
        {
            if (error) throw std::runtime_error("metadata_unavailable");
            const auto attributes = GetFileAttributesW(cursor->path().c_str());
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { cursor.disable_recursion_pending(); continue; }
            if (cursor->is_regular_file(error)) entries.push_back(cursor->path());
            if (error) throw std::runtime_error("metadata_unavailable");
        }
        if (error) throw std::runtime_error("metadata_unavailable");
    }
    else
    {
        if (error || !std::filesystem::is_regular_file(path, error)) throw std::runtime_error("missing");
        entries.push_back(path);
    }
    std::sort(entries.begin(), entries.end());
    juce::MemoryOutputStream manifest;
    for (const auto& entry : entries)
    {
        const juce::File file(juce::String(entry.wstring().c_str()));
        manifest.writeString(juce::String(entry.lexically_relative(path).wstring().c_str()));
        manifest.writeInt64(file.getSize());
        manifest.writeInt64(file.getLastModificationTime().toMilliseconds());
        if (file.hasFileExtension("dll;vst3;json"))
        {
            auto stream = file.createInputStream();
            if (!stream) throw std::runtime_error("metadata_unavailable");
            const auto size = stream->getTotalLength();
            const juce::SHA256 digest(*stream);
            if (stream->getPosition() != size) throw std::runtime_error("changed");
            manifest.writeString(digest.toHexString());
        }
    }
    return juce::SHA256(manifest.getData(), manifest.getDataSize()).toHexString();
}

inline bool belongsToModule(const juce::String& identifier, const juce::String& module, const juce::String& format)
{
    if (identifier == module) return true;
    if (format != "VST3" || !juce::File::isAbsolutePath(identifier)) return false;
    const juce::File binary(identifier), root(module);
    // Lexical containment on the host; filesystem validation is done in worker.
    return binary.hasFileExtension("vst3") && binary.isAChildOf(root);
}

class BatchWriter
{
public:
    BatchWriter(const juce::XmlElement& request, juce::File result)
        : response(std::move(result)), id(request.getStringAttribute("id")) { reset(); }
    bool append(std::unique_ptr<juce::XmlElement> item)
    {
        bytes += item->toString().getNumBytesAsUTF8();
        batch->addChildElement(item.release());
        return batch->getNumChildElements() < batchItems && bytes < 1024 * 1024 ? true : flush();
    }
    bool flush()
    {
        if (batch->getNumChildElements() == 0) return true;
        const auto destination = batchFile(response, count);
        juce::TemporaryFile temporary(destination);
        if (!batch->writeTo(temporary.getFile()) || !temporary.overwriteTargetFileWithTemporary()) return false;
        ++count;
        reset();
        return true;
    }
    int finish()
    {
        if (!flush()) return 5;
        juce::XmlElement result("SCAN");
        result.setAttribute("version", scannerProtocolVersion);
        result.setAttribute("id", id);
        result.setAttribute("mode", "enumerate");
        result.setAttribute("batches", count);
        return result.writeTo(response) ? 0 : 5;
    }
private:
    void reset()
    {
        batch = std::make_unique<juce::XmlElement>("BATCH");
        batch->setAttribute("version", scannerProtocolVersion);
        batch->setAttribute("id", id);
        batch->setAttribute("sequence", count);
        bytes = 0;
    }
    juce::File response;
    juce::String id;
    int count = 0;
    size_t bytes = 0;
    std::unique_ptr<juce::XmlElement> batch;
};

// Called only by the job-controlled scanner executable, including in tests.
inline int enumerate(const juce::XmlElement& request, const juce::File& response,
    const std::function<juce::String(const juce::String&)>& rootFailure = {})
{
    BatchWriter writer(request, response);
    const auto format = request.getStringAttribute("format");
    const auto extension = format == "VST3" ? ".vst3" : ".dll";
    std::set<std::wstring> seen;
    const auto fail = [&](const juce::String& path, const juce::String& reason) {
        auto item = std::make_unique<juce::XmlElement>("FAILURE");
        item->setAttribute("path", path); item->setAttribute("reason", reason);
        if (!writer.append(std::move(item))) throw std::runtime_error("write_failed");
        // Publish failures promptly even if the next root blocks.
        if (!writer.flush()) throw std::runtime_error("write_failed");
    };
    const auto candidate = [&](const std::filesystem::path& entry) {
        std::error_code error;
        const auto canonical = std::filesystem::canonical(entry, error);
        if (error) { fail(juce::String(entry.wstring().c_str()), filesystemFailure(error)); return; }
        const juce::String canonicalPath(canonical.wstring().c_str());
        if (!seen.insert(canonicalPath.toLowerCase().toWideCharPointer()).second) return;
        // Canonical paths are for deduplication only. Pass the discovered path
        // to JUCE so its original (including legacy 8.3) identity stays intact.
        const juce::String path(entry.wstring().c_str());
        try
        {
            auto item = std::make_unique<juce::XmlElement>("CANDIDATE");
            item->setAttribute("path", path);
            item->setAttribute("canonicalPath", canonicalPath);
            item->setAttribute("fingerprint", fingerprint(juce::File(path)));
            item->setAttribute("stamp", juce::String(juce::File(path).getLastModificationTime().toMilliseconds()));
            if (!writer.append(std::move(item))) throw std::runtime_error("write_failed");
            // Each completed candidate remains usable if a subsequent lookup hangs.
            if (!writer.flush()) throw std::runtime_error("write_failed");
        }
        catch (const std::exception& exception) { fail(path, exception.what()); }
    };
    try
    {
        for (const auto* root : request.getChildIterator())
        {
            if (!root->hasTagName("ROOT")) continue;
            const auto rootName = root->getStringAttribute("path");
            auto started = std::make_unique<juce::XmlElement>("ROOT");
            started->setAttribute("path", rootName); started->setAttribute("index", root->getIntAttribute("index"));
            if (!writer.append(std::move(started)) || !writer.flush()) throw std::runtime_error("write_failed");
            if (rootFailure)
            {
                const auto reason = rootFailure(rootName);
                if (reason.isNotEmpty()) { fail(rootName, reason); continue; }
            }
            const std::filesystem::path path(rootName.toWideCharPointer());
            std::error_code error;
            if (!std::filesystem::exists(path, error)) { fail(rootName, error ? filesystemFailure(error) : "missing"); continue; }
            if (juce::String(path.extension().wstring().c_str()).equalsIgnoreCase(extension)) { candidate(path); continue; }
            std::filesystem::recursive_directory_iterator cursor(path, std::filesystem::directory_options::none, error), end;
            if (error) { fail(rootName, filesystemFailure(error)); continue; }
            for (; cursor != end; cursor.increment(error))
            {
                if (error) { fail(rootName, filesystemFailure(error)); break; }
                const auto entry = cursor->path();
                const auto attributes = GetFileAttributesW(entry.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES)
                {
                    const auto code = GetLastError();
                    fail(juce::String(entry.wstring().c_str()), filesystemFailure(std::error_code(static_cast<int>(code), std::system_category())));
                    continue;
                }
                if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { cursor.disable_recursion_pending(); continue; }
                const juce::String suffix(entry.extension().wstring().c_str());
                if (suffix.equalsIgnoreCase(extension)) { cursor.disable_recursion_pending(); candidate(entry); }
                else if (suffix.equalsIgnoreCase(".vst3") || suffix.equalsIgnoreCase(".lv2")) cursor.disable_recursion_pending();
            }
            if (error) fail(rootName, filesystemFailure(error));
        }
        return writer.finish();
    }
    catch (...) { writer.flush(); return 6; }
}
}
