#pragma once
#include "../../Source/RuntimeProfile.h"

namespace lightHost::ui
{
    inline std::wstring uiSettingsFilePath()
    {
        const auto& profile = lightHost::RuntimeProfile::current();
        if (profile.test) return profile.uiSettings().wstring();
        auto base = RuntimeProfile::environment(L"LOCALAPPDATA");
        if (base.empty())
            base = L".";

        const auto directory = base + L"\\LightHostModern";
        CreateDirectoryW(directory.c_str(), nullptr);
        return directory + L"\\ui-settings.ini";
    }

    inline int clampBackdropModeIndex(int index)
    {
        if (index < 0 || index > 3)
            return 0;

        return index;
    }

    inline int loadBackdropModeIndex()
    {
        const auto settingsFile = uiSettingsFilePath();
        return clampBackdropModeIndex((int) GetPrivateProfileIntW(L"Appearance", L"BackdropMode", 0, settingsFile.c_str()));
    }

    inline void saveBackdropModeIndex(int index)
    {
        const auto value = std::to_wstring(clampBackdropModeIndex(index));
        const auto settingsFile = uiSettingsFilePath();
        WritePrivateProfileStringW(L"Appearance", L"BackdropMode", value.c_str(), settingsFile.c_str());
    }

    inline std::wstring loadUiSetting(wchar_t const* section, wchar_t const* key, wchar_t const* fallback = L"")
    {
        std::wstring value(2048, L'\0');
        const auto settingsFile = uiSettingsFilePath();
        DWORD length = 0;
        for (;;) {
            length = GetPrivateProfileStringW(section, key, fallback, value.data(), static_cast<DWORD>(value.size()), settingsFile.c_str());
            if (length + 1 < value.size() || value.size() >= 1024 * 1024) break;
            value.resize(value.size() * 2);
        }
        value.resize(length);
        return value;
    }

    inline void saveUiSetting(wchar_t const* section, wchar_t const* key, std::wstring const& value)
    {
        const auto settingsFile = uiSettingsFilePath();
        WritePrivateProfileStringW(section, key, value.c_str(), settingsFile.c_str());
    }

}
