// Compatibility entry points for updaters predating the product rename.
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int)
{
    wchar_t filename[32768]{};
    if (!GetModuleFileNameW(nullptr, filename, 32768)) return 1;
    const std::filesystem::path self(filename);
    auto target = self.parent_path() / L"LightHostModern.exe";
    if (self.filename() == L"LightHostScanner.exe") target = self.parent_path() / L"LightHostModernScanner.exe";
    if (self.filename() == L"LightHostUpdateHelper.exe") target = self.parent_path() / L"LightHostModernUpdateHelper.exe";
    if (self.filename() == L"LightHostWinUI.exe") target = self.parent_path() / L"WinUI/x64/Release/LightHostModern.WinUI/LightHostModernWinUI.exe";
    auto command = L"\"" + target.wstring() + L"\" " + arguments;
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION child{};
    if (!CreateProcessW(target.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, target.parent_path().c_str(), &startup, &child)) return 1;
    CloseHandle(child.hThread);
    WaitForSingleObject(child.hProcess, INFINITE);
    DWORD result = 1; GetExitCodeProcess(child.hProcess, &result); CloseHandle(child.hProcess);
    return static_cast<int>(result);
}
