#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")

// Shared by the JUCE host and WinUI. Parsing has no filesystem side effects.
// Production paths remain unchanged; test profiles never share writable state.
namespace lightHost
{
struct RuntimeProfile
{
    bool test = false;
    bool noAudio = false;
    std::wstring name, key;
    std::filesystem::path directory;

    static std::wstring environment(const wchar_t* name)
    {
        const auto size = GetEnvironmentVariableW(name, nullptr, 0);
        if (size == 0) return {};
        std::wstring value(size, L'\0');
        const auto count = GetEnvironmentVariableW(name, value.data(), size);
        if (count == 0 || count >= size) return {};
        value.resize(count);
        return value;
    }

    static RuntimeProfile parse(const std::vector<std::wstring>& args,
                                const std::filesystem::path& localAppData)
    {
        RuntimeProfile result;
        std::filesystem::path root;
        bool hasRoot = false;
        for (size_t i = 0; i < args.size(); ++i)
        {
            auto option = [&](const std::wstring& flag, std::wstring& value) {
                if (args[i].rfind(flag + L"=", 0) == 0) { value = args[i].substr(flag.size() + 1); return true; }
                if (args[i] != flag) return false;
                if (++i >= args.size()) throw std::invalid_argument("Missing profile argument");
                value = args[i];
                return true;
            };
            std::wstring value;
            if (option(L"--test-profile", value))
            {
                if (result.test) throw std::invalid_argument("Duplicate test profile");
                result.test = true;
                result.name = value;
            }
            else if (option(L"--profile-root", value))
            {
                if (hasRoot || value.empty()) throw std::invalid_argument("Invalid profile root");
                hasRoot = true;
                root = value;
            }
            else if (args[i] == L"--no-audio") result.noAudio = true;
        }
        if (hasRoot && !result.test) throw std::invalid_argument("Profile root requires --test-profile");
        if (!result.test) return result;
        if (result.name.empty() || result.name.size() > 64
            || !std::all_of(result.name.begin(), result.name.end(), [](wchar_t c) {
                return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')
                    || (c >= L'0' && c <= L'9') || c == L'-' || c == L'_';
            })) throw std::invalid_argument("Test profile must contain 1-64 ASCII letters, digits, hyphens or underscores");
        if (!hasRoot) root = localAppData / L"LightHostModern" / L"TestProfiles";
        if (!root.is_absolute()) throw std::invalid_argument("Profile root must be absolute");
        result.directory = std::filesystem::weakly_canonical(root / result.name);
        auto identity = result.directory.wstring();
        std::transform(identity.begin(), identity.end(), identity.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        uint64_t hash = 14695981039346656037ull;
        for (auto c : identity) { hash ^= static_cast<uint16_t>(c); hash *= 1099511628211ull; }
        result.key = std::to_wstring(hash);
        result.noAudio = true;
        return result;
    }

    static const RuntimeProfile& current()
    {
        static const auto profile = [] {
            int count = 0;
            auto raw = CommandLineToArgvW(GetCommandLineW(), &count);
            if (!raw) throw std::runtime_error("Cannot read process arguments");
            std::vector<std::wstring> args;
            for (int i = 1; i < count; ++i) args.emplace_back(raw[i]);
            LocalFree(raw);
            return parse(args, environment(L"LOCALAPPDATA"));
        }();
        return profile;
    }

    void createDirectories() const
    {
        if (!test) return;
        for (auto child : { L"Logs", L"Temp", L"Cache" }) std::filesystem::create_directories(directory / child);
    }
    std::filesystem::path uiSettings() const
    {
        return (test ? directory : std::filesystem::path(environment(L"LOCALAPPDATA")) / L"LightHostModern") / L"ui-settings.ini";
    }
    std::wstring windowTitle() const
    {
        return test ? L"Light Host Modern [Test: " + name + L" " + key + L"]" : L"Light Host Modern";
    }
    std::wstring pipeName() const
    {
        return test ? L"\\\\.\\pipe\\LightHost-profile-" + key : L"\\\\.\\pipe\\LightHost-" + std::to_wstring(GetCurrentProcessId());
    }
    std::wstring arguments() const
    {
        return test ? L" --test-profile=" + name + L" --profile-root=\"" + directory.parent_path().wstring() + L"\"" : L"";
    }
};
}
