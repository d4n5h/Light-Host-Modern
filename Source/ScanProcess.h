#pragma once
#include "IpcPipe.h"
#include "ProcessMetrics.h"
#include "ScanTiming.h"
#include <juce_core/juce_core.h>
#include <TlHelp32.h>
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

enum class Exit { success, launchFailed, failed, timeout, totalTimeout, cancelled };
struct Result { Exit outcome; DWORD code; };

inline Result run(const std::wstring& executable, const std::wstring& arguments,
                  const std::function<bool()>& cancelled, DWORD timeoutMs = 60000,
                  const std::function<void()>& poll = {}, const std::function<uint64_t()>& progress = {}, DWORD totalTimeoutMs = 1800000)
{
    using lightHostModern::ipc::Handle;
    StageTiming timing("process", "arguments=" + verbose::utf8(arguments));
    if (cancelled()) return { Exit::cancelled, ERROR_CANCELLED };
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return { Exit::launchFailed, GetLastError() };
    juce::ChildProcess launched;
    const juce::String command = juce::String(executable.c_str()) + (arguments.empty() ? juce::String() : " " + juce::String(arguments.c_str()));
    if (!launched.start(command, 0)) return { Exit::launchFailed, ERROR_FILE_NOT_FOUND };
    const auto childPid = [&] {
        DWORD found = 0;
        Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (!snap) return found;
        PROCESSENTRY32W entry {};
        entry.dwSize = sizeof(entry);
        const auto self = GetCurrentProcessId();
        if (Process32FirstW(snap.get(), &entry))
            do { if (entry.th32ParentProcessID == self) found = entry.th32ProcessID; }
            while (Process32NextW(snap.get(), &entry));
        return found;
    };
    DWORD pid = 0;
    for (int attempt = 0; attempt < 50 && pid == 0 && launched.isRunning(); ++attempt)
    {
        pid = childPid();
        if (pid == 0) Sleep(1);
    }
    Handle child(pid == 0 ? nullptr : OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid));
    if (!child || !AssignProcessToJobObject(job.get(), child.get()))
    {
        if (!launched.isRunning())
        {
            const auto code = launched.getExitCode();
            return { code == 0 ? Exit::success : Exit::failed, code };
        }
        const auto error = GetLastError();
        launched.kill();
        return { Exit::launchFailed, error != 0 ? error : ERROR_INVALID_HANDLE };
    }
    struct WorkerStatistics
    {
        HANDLE process;
        StageTiming& timing;
        ~WorkerStatistics() noexcept {
            if (!timing.active()) return;
            try {
                timing.add("pid", GetProcessId(process));
                FILETIME created{}, exited{}, kernel{}, user{};
                if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
                    const auto ticks = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
                    timing.add("cpuMs", double(ticks(kernel) + ticks(user)) / 10000.0);
                }
                IO_COUNTERS io{};
                if (GetProcessIoCounters(process, &io)) {
                    timing.add("readBytes", static_cast<double>(io.ReadTransferCount));
                    timing.add("writeBytes", static_cast<double>(io.WriteTransferCount));
                }
                PROCESS_MEMORY_COUNTERS memory{}; memory.cb = sizeof(memory);
                if (GetProcessMemoryInfo(process, &memory, sizeof(memory)))
                    timing.add("peakWorkingSet", static_cast<double>(memory.PeakWorkingSetSize));
            } catch (...) {}
        }
    } statistics{child.get(), timing};
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
    timing.add("launchMs", timing.elapsedMs());
    auto deadline = GetTickCount64() + timeoutMs;
    auto totalDeadline = GetTickCount64() + totalTimeoutMs;
    auto progressToken = progress ? progress() : 0;
    for (;;)
    {
        // Consumer backpressure is intentional and must not count as worker
        // inactivity or against its processing-time limit.
        const auto beforePoll=GetTickCount64();
        if (poll) poll();
        const auto consumerTime=GetTickCount64()-beforePoll;
        deadline+=consumerTime;totalDeadline+=consumerTime;
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
        if (cancelled() || GetTickCount64() >= deadline || GetTickCount64() >= totalDeadline || wait == WAIT_FAILED)
        {
            const bool wasCancelled = cancelled();
            TerminateJobObject(job.get(), ERROR_CANCELLED);
            WaitForSingleObject(child.get(), 1000);
            return { wasCancelled ? Exit::cancelled : (wait == WAIT_FAILED ? Exit::failed : GetTickCount64()>=totalDeadline?Exit::totalTimeout:Exit::timeout), ERROR_CANCELLED };
        }
    }
}
}
