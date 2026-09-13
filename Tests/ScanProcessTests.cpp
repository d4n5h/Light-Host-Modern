#include "ScanProcess.h"
#include <thread>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <filesystem>

using lightHost::ipc::Handle;
static std::wstring executablePath()
{
    wchar_t path[32768] {};
    GetModuleFileNameW(nullptr, path, 32768);
    return path;
}

static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int wmain(int argc, wchar_t** argv)
{
    if (argc > 1)
    {
        if (std::wstring(argv[1]) == L"hang") { Sleep(INFINITE); return 0; }
        if (std::wstring(argv[1]) == L"crash") { TerminateProcess(GetCurrentProcess(), 44); return 44; }
        if (argc == 3 && std::wstring(argv[1]) == L"owner")
        {
            const auto result = lightHost::scan::run(executablePath(),
                L"tree " + lightHost::scan::quoteArgument(argv[2]), [] { return false; }, 10000);
            return result.outcome == lightHost::scan::Exit::success ? 0 : 92;
        }
        if (argc == 3 && std::wstring(argv[1]) == L"tree")
        {
            auto command = lightHost::scan::quoteArgument(executablePath()) + L" hang";
            STARTUPINFOW startup {}; startup.cb = sizeof(startup);
            PROCESS_INFORMATION child {};
            if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                nullptr, nullptr, &startup, &child)) return 93;
            Handle process(child.hProcess), thread(child.hThread);
            {
                std::ofstream marker { std::filesystem::path(argv[2]) };
                marker << GetCurrentProcessId() << ' ' << child.dwProcessId;
            }
            Sleep(INFINITE);
            return 0;
        }
        BOOL contained = FALSE;
        if (!IsProcessInJob(GetCurrentProcess(), nullptr, &contained) || !contained) return 90;
        return argc == 3 && std::wstring(argv[2]) == L"Unicode \u65e5\u672c \\\"quoted\\\" \\" ? 0 : 91;
    }
    try
    {
        using namespace lightHost::scan;
        wchar_t executable[32768] {};
        GetModuleFileNameW(nullptr, executable, 32768);
        const auto never = [] { return false; };
        const auto success = run(executable, L"probe " + quoteArgument(L"Unicode \u65e5\u672c \\\"quoted\\\" \\"), never, 3000);
        require(success.outcome == Exit::success, "Worker must be in job; Unicode/quotes must round trip");
        const auto crash = run(executable, L"crash", never, 3000);
        require(crash.outcome == Exit::failed && crash.code == 44, "Worker crash must be reported");
        require(run(executable, L"hang", never, 100).outcome == Exit::timeout, "Hung worker must time out");
        std::atomic<bool> cancel { false };
        std::thread timer([&] { Sleep(75); cancel.store(true); });
        const auto cancelled = run(executable, L"hang", [&] { return cancel.load(); }, 3000);
        timer.join();
        require(cancelled.outcome == Exit::cancelled, "Running worker cancellation");
        require(run(executable, L"probe", [] { return true; }).outcome == Exit::cancelled, "Cancellation before launch");
        require(run(L"C:\\nonexistent-lighthost-scanner.exe", L"", never).outcome == Exit::launchFailed, "No unsafe fallback when worker missing");
        wchar_t tempFolder[MAX_PATH] {}, markerPath[MAX_PATH] {};
        require(GetTempPathW(MAX_PATH, tempFolder) != 0, "temporary directory");
        require(GetTempFileNameW(tempFolder, L"LHS", 0, markerPath) != 0, "temporary process marker");
        struct MarkerCleanup {
            const wchar_t* path;
            ~MarkerCleanup() { DeleteFileW(path); }
        } markerCleanup { markerPath };
        auto command = quoteArgument(executable) + L" owner " + quoteArgument(markerPath);
        STARTUPINFOW startup {}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION ownerInfo {};
        require(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                               nullptr, nullptr, &startup, &ownerInfo) != FALSE, "launch simulated host owner");
        Handle owner(ownerInfo.hProcess), ownerThread(ownerInfo.hThread);
        struct OwnerCleanup {
            HANDLE process;
            ~OwnerCleanup() { TerminateProcess(process, 99); WaitForSingleObject(process, 1000); }
        } ownerCleanup { owner.get() };
        DWORD workerId = 0, descendantId = 0;
        const auto readyDeadline = GetTickCount64() + 3000;
        while (GetTickCount64() < readyDeadline)
        {
            std::ifstream marker { std::filesystem::path(markerPath) };
            if (marker >> workerId >> descendantId) break;
            Sleep(10);
        }
        require(workerId != 0 && descendantId != 0, "worker and descendant readiness");
        Handle workerProcess(OpenProcess(SYNCHRONIZE, FALSE, workerId));
        Handle descendantProcess(OpenProcess(SYNCHRONIZE, FALSE, descendantId));
        require(workerProcess && descendantProcess, "retain worker and descendant handles before abandonment");
        require(WaitForSingleObject(workerProcess.get(), 0) == WAIT_TIMEOUT
            && WaitForSingleObject(descendantProcess.get(), 0) == WAIT_TIMEOUT, "both processes alive before abandonment");
        require(TerminateProcess(owner.get(), 77) != FALSE, "simulate abrupt host exit");
        require(WaitForSingleObject(owner.get(), 3000) == WAIT_OBJECT_0, "owner exited");
        require(WaitForSingleObject(workerProcess.get(), 3000) == WAIT_OBJECT_0, "abandoned worker terminated");
        require(WaitForSingleObject(descendantProcess.get(), 3000) == WAIT_OBJECT_0, "abandoned worker descendant terminated");
        std::cout << "Scanner process isolation regressions passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
