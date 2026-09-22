#pragma once
#include "IpcPipe.h"
#include "ProcessMetrics.h"
#include <atomic>
#include <functional>
#include <string>

namespace lightHostModern::scan
{
inline std::wstring quoteArgument(const std::wstring& value)
{
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value)
    {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}

enum class Exit { success, launchFailed, failed, timeout, cancelled };
struct Result { Exit outcome; DWORD code; };

inline Result run(const std::wstring& executable, const std::wstring& arguments,
                  const std::function<bool()>& cancelled, DWORD timeoutMs = 60000,
                  const std::function<void()>& poll = {}, const std::function<uint64_t()>& progress = {})
{
    using lightHostModern::ipc::Handle;
    if (cancelled()) return { Exit::cancelled, ERROR_CANCELLED };
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return { Exit::launchFailed, GetLastError() };
    auto command = quoteArgument(executable) + L" " + arguments;
    STARTUPINFOW startup {}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process {};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        return { Exit::launchFailed, GetLastError() };
    Handle child(process.hProcess), thread(process.hThread);
    struct WorkerAccounting
    {
        HANDLE process;
        std::optional<uint64_t> previous{0};
        uint64_t lastMemoryTick = 0, resident = 0, committed = 0;
        bool unavailable = false;
        void memory()
        {
            const auto now = GetTickCount64();
            if (now - lastMemoryTick < 1000 || now > diagnosticsVisibleUntil.load()) return;
            lastMemoryTick = now;
            const auto value = processMemory(process);
            const bool missing = !value.resident || !value.committed;
            if (missing != unavailable) { if (missing) ++workerMemoryUnavailable; else --workerMemoryUnavailable; unavailable = missing; }
            workerResidentBytes.fetch_sub(resident); workerCommittedBytes.fetch_sub(committed);
            resident = value.resident.value_or(0); committed = value.committed.value_or(0);
            workerResidentBytes.fetch_add(resident); workerCommittedBytes.fetch_add(committed);
        }
        void update()
        {
            if (!lightHostModern::diagnosticsCollectionEnabled.load(std::memory_order_relaxed))
            { previous.reset(); return; }
            memory();
            if (const auto ticks = lightHostModern::processCpuTicks(process))
            { if (previous && *ticks >= *previous) lightHostModern::workerCpuTicks.fetch_add(*ticks - *previous); previous = *ticks; }
        }
        ~WorkerAccounting() { update(); workerResidentBytes.fetch_sub(resident); workerCommittedBytes.fetch_sub(committed); if (unavailable) --workerMemoryUnavailable; }
    } accounting{child.get()};
    if (!AssignProcessToJobObject(job.get(), child.get()))
    {
        const auto error = GetLastError();
        TerminateProcess(child.get(), error);
        WaitForSingleObject(child.get(), 1000);
        return { Exit::launchFailed, error };
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1))
        return { Exit::launchFailed, GetLastError() };
    auto deadline = GetTickCount64() + timeoutMs;
    auto progressToken = progress ? progress() : 0;
    for (;;)
    {
        if (poll) poll();
        if (progress && progress() != progressToken)
        {
            progressToken = progress();
            deadline = GetTickCount64() + timeoutMs;
        }
        const auto wait = WaitForSingleObject(child.get(), 25);
        accounting.update();
        if (wait == WAIT_OBJECT_0)
        {
            DWORD code = 0;
            if (!GetExitCodeProcess(child.get(), &code)) return { Exit::failed, GetLastError() };
            return { code == 0 ? Exit::success : Exit::failed, code };
        }
        if (cancelled() || GetTickCount64() >= deadline || wait == WAIT_FAILED)
        {
            const bool wasCancelled = cancelled();
            TerminateJobObject(job.get(), ERROR_CANCELLED);
            WaitForSingleObject(child.get(), 1000);
            return { wasCancelled ? Exit::cancelled : (wait == WAIT_FAILED ? Exit::failed : Exit::timeout), ERROR_CANCELLED };
        }
    }
}
}
