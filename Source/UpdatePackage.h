#pragma once
#include "UpdateWindows.h"
#include <juce_core/juce_core.h>
#include <msiquery.h>
#include <set>

namespace lightHostModern::update
{
struct MsiHandle
{
    MSIHANDLE value = 0;
    ~MsiHandle() { if (value) MsiCloseHandle(value); }
};
inline std::wstring msiField(MSIHANDLE record, unsigned field)
{
    DWORD size = 32768; std::wstring result(size, L'\0');
    require(MsiRecordGetStringW(record, field, result.data(), &size) == ERROR_SUCCESS, "package_invalid");
    result.resize(size); return result;
}
inline std::wstring msiProperty(MSIHANDLE database, const wchar_t* name)
{
    MsiHandle view, record;
    const auto query = std::wstring(L"SELECT `Value` FROM `Property` WHERE `Property`='") + name + L"'";
    require(MsiDatabaseOpenViewW(database, query.c_str(), &view.value) == ERROR_SUCCESS
        && MsiViewExecute(view.value, 0) == ERROR_SUCCESS && MsiViewFetch(view.value, &record.value) == ERROR_SUCCESS, "package_invalid");
    return msiField(record.value, 1);
}
inline void validateMsi(const std::filesystem::path& file, const Artifact& artifact)
{
    MsiHandle database, summary;
    require(MsiOpenDatabaseW(file.c_str(), reinterpret_cast<LPCWSTR>(0), &database.value) == ERROR_SUCCESS, "package_invalid");
    require(parseVersion(msiProperty(database.value, L"ProductVersion")) == parseVersion(artifact.version), "version_mismatch");
    const auto product = msiProperty(database.value, L"ProductName");
    const bool legacy = product == L"Light Host Modern" && *parseVersion(artifact.version) < std::array<unsigned,3>{1,4,0};
    // ProductName remains the MSI compatibility identity during the transition.
    require(product == L"LightHostModern" || product == L"Light Host Modern", "artifact_mismatch");
    require(_wcsicmp(msiProperty(database.value, L"UpgradeCode").c_str(), upgradeCode) == 0, "artifact_mismatch");
    require(MsiGetSummaryInformationW(database.value, nullptr, 0, &summary.value) == ERROR_SUCCESS, "package_invalid");
    UINT type = 0; INT number = 0; FILETIME time{}; wchar_t value[1024]{}; DWORD size = 1024;
    require(MsiSummaryInfoGetPropertyW(summary.value, 7 /*PID_TEMPLATE*/, &type, &number, &time, value, &size) == ERROR_SUCCESS, "package_invalid");
    require(std::wstring(value).rfind(L"x64;", 0) == 0, "architecture_mismatch");
    MsiHandle view;
    require(MsiDatabaseOpenViewW(database.value, L"SELECT `FileName` FROM `File`", &view.value) == ERROR_SUCCESS
        && MsiViewExecute(view.value, 0) == ERROR_SUCCESS, "package_invalid");
    std::set<std::wstring> names;
    for (;;)
    {
        MsiHandle record;
        const auto result = MsiViewFetch(view.value, &record.value);
        if (result == ERROR_NO_MORE_ITEMS) break;
        require(result == ERROR_SUCCESS, "package_invalid");
        auto name = msiField(record.value, 1); const auto bar = name.find(L'|');
        if (bar != std::wstring::npos) name.erase(0, bar + 1);
        names.insert(std::move(name));
    }
    const auto required = legacy ? std::vector<std::wstring>{L"Light Host Modern.exe", L"LightHostWinUI.exe", L"LightHostScanner.exe", L"LightHostUpdateHelper.exe"}
        : std::vector<std::wstring>{L"LightHostModern.exe", L"LightHostModernWinUI.exe", L"LightHostModernScanner.exe", L"LightHostModernUpdateHelper.exe"};
    for (const auto& name : required)
        require(names.count(name) != 0, "package_incomplete");
}
inline void validatePe(juce::InputStream& stream)
{
    std::array<unsigned char, 65536> bytes{};
    const auto count = stream.read(bytes.data(), static_cast<int>(bytes.size()));
    require(count >= 64 && bytes[0] == 'M' && bytes[1] == 'Z', "package_invalid");
    uint32_t offset = 0; std::memcpy(&offset, bytes.data() + 60, 4);
    require(offset <= static_cast<unsigned>(count) - 6 && bytes[offset] == 'P' && bytes[offset + 1] == 'E'
        && bytes[offset + 2] == 0 && bytes[offset + 3] == 0, "package_invalid");
    require(bytes[offset + 4] == 0x64 && bytes[offset + 5] == 0x86, "architecture_mismatch");
}
inline void validateZip(const std::filesystem::path& path, const Artifact& artifact)
{
    juce::FileInputStream input(juce::File(juce::String(path.c_str())));
    require(input.openedOk(), "package_unavailable");
    juce::ZipFile zip(input);
    require(zip.getNumEntries() > 0 && zip.getNumEntries() <= 20000, "package_invalid");
    std::set<std::string> names;
    uint64_t expanded = 0;
    for (int index = 0; index < zip.getNumEntries(); ++index)
    {
        const auto* entry = zip.getEntry(index);
        const auto name = entry->filename.replaceCharacter('\\', '/');
        const auto parts = juce::StringArray::fromTokens(name, "/", "");
        require(!name.startsWithChar('/') && !name.containsChar(':') && !parts.contains("..") && !parts.contains(".")
            && !entry->isSymbolicLink && names.insert(name.toLowerCase().toStdString()).second, "package_invalid");
        require(entry->uncompressedSize >= 0 && uint64_t(entry->uncompressedSize) <= maximumPackageBytes * 4 - expanded, "size_mismatch");
        expanded += uint64_t(entry->uncompressedSize);
    }
    const auto manifestIndex = zip.getIndexOfFileName("release-info.json");
    require(manifestIndex >= 0 && zip.getEntry(manifestIndex)->uncompressedSize <= 1024 * 1024, "package_incomplete");
    auto manifestStream = std::unique_ptr<juce::InputStream>(zip.createStreamForEntry(manifestIndex));
    require(manifestStream != nullptr, "package_invalid");
    const auto manifest = juce::JSON::parse(manifestStream->readEntireStreamAsString());
    require(manifest.isObject() && manifest["name"].toString() == "LightHostModern", "artifact_mismatch");
    require(parseVersion(manifest["version"].toString().toWideCharPointer()) == parseVersion(artifact.version), "version_mismatch");
    require(manifest["platform"].toString() == "x64", "architecture_mismatch");
    for (const auto& name : {juce::String("LightHostModern.exe"), juce::String("LightHostModernScanner.exe"), juce::String("LightHostModernUpdateHelper.exe"),
                            juce::String("WinUI/x64/Release/LightHostModern.WinUI/LightHostModernWinUI.exe")})
    {
        // Compress-Archive uses backslashes on some PowerShell versions.
        int index = zip.getIndexOfFileName(name);
        if (index < 0) index = zip.getIndexOfFileName(name.replaceCharacter('/', '\\'));
        require(index >= 0, "package_incomplete");
        auto stream = std::unique_ptr<juce::InputStream>(zip.createStreamForEntry(index));
        require(stream != nullptr, "package_invalid"); validatePe(*stream);
    }
}
inline void validatePackage(const std::filesystem::path& file, const Artifact& artifact)
{
    artifact.validate();
    require(file.filename() == artifact.name, "artifact_mismatch");
    FileInput lock(file); // Disallow concurrent replacement/modification for the entire inspection.
    require(std::filesystem::file_size(file) == artifact.bytes, "size_mismatch");
    require(fileDigest(file) == normalizedDigest(artifact.digest), "checksum_mismatch");
    if (artifact.distribution == Distribution::installed) validateMsi(file, artifact);
    else validateZip(file, artifact);
}
}
