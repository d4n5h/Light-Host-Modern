#include "ChainProfileStore.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <limits>

namespace lightHostModern
{
namespace
{
std::wstring longPath(const juce::File& file)
{
    const std::wstring path(file.getFullPathName().toWideCharPointer());
    if (path.rfind(L"\\\\?\\", 0) == 0) return path;
    return path.rfind(L"\\\\", 0) == 0 ? L"\\\\?\\UNC\\" + path.substr(2) : L"\\\\?\\" + path;
}
struct Handle
{
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
bool validId(const juce::String& id)
{
    const auto lower = id.toLowerCase();
    return lower.length() == 32 && lower.containsOnly("0123456789abcdef");
}
std::string readBytes(const juce::File& file, juce::String& error)
{
    error.clear();
    Handle handle{CreateFileW(longPath(file).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (handle.value == INVALID_HANDLE_VALUE)
    {
        const auto code = GetLastError();
        if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) error = "profile_write_failed";
        return {};
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle.value, &size) || size.QuadPart < 0
        || static_cast<uint64_t>(size.QuadPart) > SessionCodec::maximumFileBytes)
    { error = "profile_invalid"; return {}; }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    size_t offset = 0;
    while (offset < bytes.size())
    {
        DWORD read = 0;
        if (!ReadFile(handle.value, bytes.data() + offset,
            static_cast<DWORD>((std::min)(bytes.size() - offset, size_t{1024 * 1024})), &read, nullptr) || read == 0)
        { error = "profile_invalid"; return {}; }
        offset += read;
    }
    return bytes;
}
juce::String replaceFile(const juce::File& destination, const std::string& bytes)
{
    destination.getParentDirectory().createDirectory();
    const auto temporary = destination.getSiblingFile(destination.getFileName() + ".pending");
    Handle handle{CreateFileW(longPath(temporary).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (handle.value == INVALID_HANDLE_VALUE) return "profile_write_failed";
    size_t offset = 0;
    while (offset < bytes.size())
    {
        DWORD written = 0;
        if (!WriteFile(handle.value, bytes.data() + offset,
            static_cast<DWORD>((std::min)(bytes.size() - offset, size_t{1024 * 1024})), &written, nullptr) || written == 0)
            return "profile_write_failed";
        offset += written;
    }
    if (!FlushFileBuffers(handle.value)) return "profile_write_failed";
    handle.value = (CloseHandle(handle.value), INVALID_HANDLE_VALUE);
    return MoveFileExW(longPath(temporary).c_str(), longPath(destination).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
        ? juce::String() : juce::String("profile_write_failed");
}
}

ChainProfileStore::ChainProfileStore(const juce::File& preferencesFile)
    : directory(preferencesFile.getSiblingFile(preferencesFile.getFileName() + ".profiles")),
      catalogFile(directory.getChildFile("catalog.json"))
{
    loadCatalog();
}

juce::String ChainProfileStore::contentHash(const PluginInstances& instances, const juce::String& migrationId)
{
    return SessionCodec::encode(SessionDocument{instances, 1, migrationId, instances.records.empty()}).digest;
}

const ChainProfile* ChainProfileStore::find(const juce::String& id) const
{
    for (const auto& profile : current.profiles)
        if (profile.id == id) return &profile;
    return nullptr;
}

juce::String ChainProfileStore::validateName(const juce::String& name, const juce::String& exceptId, juce::String& normalized) const
{
    if (!normalizeInstanceName(name, normalized) || normalized.isEmpty()) return "profile_name_invalid";
    for (const auto& profile : current.profiles)
        if (profile.id != exceptId && profile.name.compareIgnoreCase(normalized) == 0) return "profile_name_taken";
    return {};
}

juce::String ChainProfileStore::copyName(const juce::String& base) const
{
    for (int number = 2; number < 10000; ++number)
    {
        const auto suffix = " " + juce::String(number);
        auto stem = base;
        const auto overflow = stem.length() + suffix.length() - 128;
        if (overflow > 0) stem = stem.substring(0, juce::jmax(0, stem.length() - overflow));
        juce::String normalized;
        if (validateName(stem + suffix, {}, normalized).isEmpty()) return normalized;
    }
    return {};
}

juce::String ChainProfileStore::newId() const
{
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const auto id = juce::Uuid().toString().removeCharacters("-").toLowerCase();
        if (validId(id) && find(id) == nullptr && !fileFor(id).existsAsFile()) return id;
    }
    return {};
}

juce::String ChainProfileStore::readContentHash(const juce::String& id) const
{
    juce::String error;
    const auto bytes = readBytes(fileFor(id), error);
    if (bytes.empty()) return {};
    const auto root = juce::JSON::parse(juce::String::fromUTF8(bytes.data(), static_cast<int>(bytes.size())));
    return root.isObject() && root["contentHash"].isString() ? root["contentHash"].toString() : juce::String();
}

juce::String ChainProfileStore::writeProfile(const juce::String& id, const PluginInstances& instances, const juce::String& migrationId)
{
    juce::uint64 revision = 1;
    juce::String error;
    const auto existing = readBytes(fileFor(id), error);
    if (error.isNotEmpty() && error != "profile_invalid") return error;
    if (!existing.empty())
    {
        if (const auto document = SessionCodec::decode(existing, error))
        {
            try
            {
                if (SessionCodec::encode(SessionDocument{instances, 1, migrationId, instances.records.empty()}).digest == readContentHash(id))
                    return {};
            }
            catch (...) { return "profile_invalid"; }
            if (document->revision == (std::numeric_limits<juce::uint64>::max)()) return "profile_write_failed";
            revision = document->revision + 1;
        }
    }
    try
    {
        const auto encoded = SessionCodec::encode(SessionDocument{instances, revision, migrationId, instances.records.empty()});
        return replaceFile(fileFor(id), encoded.bytes);
    }
    catch (const std::length_error&) { return "profile_write_failed"; }
    catch (...) { return "profile_invalid"; }
}

juce::String ChainProfileStore::saveCatalog()
{
    auto* root = new juce::DynamicObject();
    root->setProperty("formatVersion", 1);
    root->setProperty("activeId", current.activeId);
    juce::Array<juce::var> items;
    for (const auto& profile : current.profiles)
    {
        auto* item = new juce::DynamicObject();
        item->setProperty("id", profile.id);
        item->setProperty("name", profile.name);
        items.add(juce::var(item));
    }
    root->setProperty("profiles", items);
    auto bytes = juce::JSON::toString(juce::var(root), true).toStdString();
    if (!bytes.empty() && bytes.back() != '\n') bytes.push_back('\n');
    return replaceFile(catalogFile, bytes);
}

juce::String ChainProfileStore::reconcile(const juce::String& sessionContentHash)
{
    if (sessionContentHash.isNotEmpty())
    {
        if (find(current.activeId) != nullptr && readContentHash(current.activeId) == sessionContentHash) return {};
        for (const auto& profile : current.profiles)
            if (readContentHash(profile.id) == sessionContentHash)
            {
                if (current.activeId == profile.id) return {};
                const auto previous = current.activeId;
                current.activeId = profile.id;
                if (const auto error = saveCatalog(); error.isNotEmpty()) { current.activeId = previous; return error; }
                return {};
            }
    }
    if (current.activeId.isEmpty()) return {};
    const auto previous = current.activeId;
    current.activeId.clear();
    if (const auto error = saveCatalog(); error.isNotEmpty()) { current.activeId = previous; return error; }
    return {};
}

void ChainProfileStore::loadCatalog()
{
    if (!catalogFile.existsAsFile()) return;
    juce::String error;
    const auto bytes = readBytes(catalogFile, error);
    const auto root = error.isEmpty()
        ? juce::JSON::parse(juce::String::fromUTF8(bytes.data(), static_cast<int>(bytes.size()))) : juce::var();
    const auto* profiles = root.isObject() && static_cast<int>(root["formatVersion"]) == 1 && root["activeId"].isString()
        ? root["profiles"].getArray() : nullptr;
    if (profiles == nullptr) { broken = true; return; }
    ChainProfileCatalog loaded;
    loaded.activeId = root["activeId"].toString().toLowerCase();
    for (const auto& item : *profiles)
    {
        if (!item.isObject() || !item["id"].isString() || !item["name"].isString()) { broken = true; return; }
        ChainProfile profile{item["id"].toString().toLowerCase(), item["name"].toString()};
        juce::String normalized;
        if (!validId(profile.id) || validateName(profile.name, profile.id, normalized).isNotEmpty()) { broken = true; return; }
        profile.name = normalized;
        for (const auto& other : loaded.profiles)
            if (other.id == profile.id || other.name.compareIgnoreCase(profile.name) == 0) { broken = true; return; }
        loaded.profiles.push_back(std::move(profile));
    }
    if (loaded.activeId.isNotEmpty() && std::none_of(loaded.profiles.begin(), loaded.profiles.end(),
        [&](const auto& profile) { return profile.id == loaded.activeId; })) { broken = true; return; }
    if (static_cast<int>(loaded.profiles.size()) > maximumProfiles) { broken = true; return; }
    current = std::move(loaded);
}

juce::String ChainProfileStore::ensureDefault(const PluginInstances& instances, const juce::String& migrationId, const juce::String& sessionContentHash)
{
    if (broken) return "profile_catalog_invalid";
    if (!current.profiles.empty()) return reconcile(sessionContentHash);
    juce::String id;
    return create(instances, migrationId, "Default", id);
}

juce::String ChainProfileStore::create(const PluginInstances& instances, const juce::String& migrationId, const juce::String& name, juce::String& id)
{
    if (broken) return "profile_catalog_invalid";
    if (static_cast<int>(current.profiles.size()) >= maximumProfiles) return "profile_limit";
    juce::String normalized;
    if (const auto error = validateName(name, {}, normalized); error.isNotEmpty()) return error;
    id = newId();
    if (id.isEmpty()) return "profile_write_failed";
    const auto previousActive = current.activeId;
    if (const auto error = writeProfile(id, instances, migrationId); error.isNotEmpty()) return error;
    current.profiles.push_back({id, normalized});
    current.activeId = id;
    if (const auto error = saveCatalog(); error.isNotEmpty())
    {
        current.profiles.pop_back();
        current.activeId = previousActive;
        fileFor(id).deleteFile();
        id.clear();
        return error;
    }
    return {};
}

juce::String ChainProfileStore::switchTo(const juce::String& id, PluginInstances& destination, juce::String& migrationId) const
{
    if (broken) return "profile_catalog_invalid";
    if (find(id) == nullptr) return "profile_not_found";
    juce::String error;
    const auto bytes = readBytes(fileFor(id), error);
    if (error.isNotEmpty()) return error == "profile_write_failed" ? error : juce::String("profile_invalid");
    if (bytes.empty()) return "profile_not_found";
    const auto document = SessionCodec::decode(bytes, error);
    if (!document) return "profile_invalid";
    destination.records = document->instances.records;
    destination.strips = document->instances.strips;
    destination.masterGainDb = document->instances.masterGainDb;
    destination.recoveryError = document->instances.recoveryError;
    migrationId = document->migrationId;
    return {};
}

juce::String ChainProfileStore::writeActive(const PluginInstances& instances, const juce::String& migrationId)
{
    if (broken) return "profile_catalog_invalid";
    if (current.activeId.isEmpty()) return {};
    if (find(current.activeId) == nullptr) return "profile_not_found";
    return writeProfile(current.activeId, instances, migrationId);
}

juce::String ChainProfileStore::setActive(const juce::String& id)
{
    if (broken) return "profile_catalog_invalid";
    if (find(id) == nullptr) return "profile_not_found";
    if (current.activeId == id) return {};
    const auto previous = current.activeId;
    current.activeId = id;
    if (const auto error = saveCatalog(); error.isNotEmpty()) { current.activeId = previous; return error; }
    return {};
}

juce::String ChainProfileStore::rename(const juce::String& id, const juce::String& name)
{
    if (broken) return "profile_catalog_invalid";
    auto* profile = const_cast<ChainProfile*>(find(id));
    if (profile == nullptr) return "profile_not_found";
    juce::String normalized;
    if (const auto error = validateName(name, id, normalized); error.isNotEmpty()) return error;
    if (profile->name == normalized) return {};
    const auto previous = profile->name;
    profile->name = normalized;
    if (const auto error = saveCatalog(); error.isNotEmpty()) { profile->name = previous; return error; }
    return {};
}

juce::String ChainProfileStore::duplicate(const juce::String& id, juce::String& newId)
{
    if (broken) return "profile_catalog_invalid";
    const auto* source = find(id);
    if (source == nullptr) return "profile_not_found";
    if (static_cast<int>(current.profiles.size()) >= maximumProfiles) return "profile_limit";
    const auto name = copyName(source->name);
    if (name.isEmpty()) return "profile_name_taken";
    const auto copyId = this->newId();
    if (copyId.isEmpty()) return "profile_write_failed";
    newId = copyId;
    juce::String error;
    const auto bytes = readBytes(fileFor(id), error);
    if (bytes.empty()) { newId.clear(); return error.isNotEmpty() ? error : juce::String("profile_not_found"); }
    if (const auto writeError = replaceFile(fileFor(newId), bytes); writeError.isNotEmpty()) { newId.clear(); return writeError; }
    current.profiles.push_back({newId, name});
    if (const auto catalogError = saveCatalog(); catalogError.isNotEmpty())
    {
        current.profiles.pop_back();
        fileFor(newId).deleteFile();
        newId.clear();
        return catalogError;
    }
    return {};
}

juce::String ChainProfileStore::remove(const juce::String& id)
{
    if (broken) return "profile_catalog_invalid";
    if (current.profiles.size() <= 1) return "last_profile";
    if (find(id) == nullptr) return "profile_not_found";
    if (current.activeId == id) return "profile_active";
    const auto previous = current.profiles;
    current.profiles.erase(std::remove_if(current.profiles.begin(), current.profiles.end(),
        [&](const auto& profile) { return profile.id == id; }), current.profiles.end());
    if (const auto error = saveCatalog(); error.isNotEmpty()) { current.profiles = previous; return error; }
    fileFor(id).deleteFile();
    return {};
}
}
