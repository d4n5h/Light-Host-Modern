#include "LegacyInstallMigration.h"
#include <iostream>
int main()
{
    using namespace lightHostModern::migration;
    using namespace std::filesystem;
    const auto root = temp_directory_path() / (L"lhm-legacy-test-" + std::to_wstring(GetCurrentProcessId()));
    try {
        if (validRelative(L"../outside") || validRelative(L"C:/outside") || validRelative(L"a:stream") || !validRelative(L"WinUI/x64/Release/resource.pri")) return 1;
        if (safePath(root.root_path())) return 2;
        create_directories(root / L"source"); create_directories(root / L"backup");
        const auto owned = root / L"source/owned.dll", other = root / L"source/my-session.json";
        { std::ofstream(owned) << "owned bytes"; std::ofstream(other) << "user data"; }
        if (!copyThenRemove(owned, root / L"backup/owned.dll") || exists(owned) || !exists(other)) return 3;
        { std::ofstream(owned) << "new bytes"; }
        if (copyThenRemove(owned, root / L"backup/owned.dll") || !exists(owned)) return 4;
        if (legacyExecutable(owned, L"1.2.0")) return 5;
        remove(owned); remove(other); remove(root / L"backup/owned.dll"); remove(root / L"source"); remove(root / L"backup"); remove(root);
        std::cout << "Legacy cleanup boundaries, backup-before-delete and non-product rejection passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
