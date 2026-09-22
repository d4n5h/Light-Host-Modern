#pragma once
#include "RuntimeProfile.h"
#include <windows.h>

namespace lightHostModern
{
inline void migrateStartupRegistration(const std::filesystem::path& legacyExe, const std::filesystem::path& currentExe)
{
    if (RuntimeProfile::current().test) return;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    wchar_t value[32768]{}; DWORD type = 0, bytes = sizeof(value);
    DWORD existing = 0;
    const auto canonical = RegQueryValueExW(key, L"LightHostModern", nullptr, nullptr, nullptr, &existing);
    const auto result = RegQueryValueExW(key, L"Light Host Modern", nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes);
    const auto expected = L"\"" + legacyExe.wstring() + L"\" --startup";
    if (canonical == ERROR_FILE_NOT_FOUND && result == ERROR_SUCCESS && type == REG_SZ && bytes < sizeof(value) && _wcsicmp(value, expected.c_str()) == 0) {
        const auto command = L"\"" + currentExe.wstring() + L"\" --startup";
        if (RegSetValueExW(key, L"LightHostModern", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size()+1)*sizeof(wchar_t))) == ERROR_SUCCESS)
            RegDeleteValueW(key, L"Light Host Modern");
    }
    RegCloseKey(key);
}
}
