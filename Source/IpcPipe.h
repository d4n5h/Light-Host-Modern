#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <stdexcept>
#include <string>

namespace lightHost::ipc
{
inline constexpr size_t maxMessageBytes = 4 * 1024 * 1024;

class Handle
{
public:
    explicit Handle(HANDLE value = nullptr) noexcept : handle(value) {}
    ~Handle() { if (*this) CloseHandle(handle); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const noexcept { return handle; }
    explicit operator bool() const noexcept { return handle && handle != INVALID_HANDLE_VALUE; }
private:
    HANDLE handle;
};

class StopEvent
{
public:
    StopEvent() : event(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
        if (!event) throw std::runtime_error("Cannot create IPC stop event");
    }
    void signal() noexcept { SetEvent(event.get()); }
    HANDLE get() const noexcept { return event.get(); }
private:
    Handle event;
};

// All operations drain cancellation before their OVERLAPPED and buffers leave
// scope. Deadlines cover the whole message, including partial reads.
class PipeIo
{
public:
    PipeIo(HANDLE pipeIn, HANDLE stopIn, DWORD timeoutMs)
        : pipe(pipeIn), stop(stopIn), deadline(GetTickCount64() + timeoutMs) {}

    DWORD connect()
    {
        if (stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) return ERROR_OPERATION_ABORTED;
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event) return GetLastError();
        OVERLAPPED operation{};
        operation.hEvent = event.get();
        DWORD transferred = 0;
        auto error = ConnectNamedPipe(pipe, &operation) ? ERROR_SUCCESS : GetLastError();
        if (error == ERROR_PIPE_CONNECTED) return ERROR_SUCCESS;
        return finish(operation, error, transferred);
    }

    DWORD read(std::string& message)
    {
        message.clear();
        std::array<char, 16 * 1024> buffer;
        do
        {
            DWORD bytes = 0;
            const auto error = transfer(false, buffer.data(), static_cast<DWORD>(buffer.size()), bytes);
            if (error != ERROR_SUCCESS && error != ERROR_MORE_DATA) return error;
            if (message.size() + bytes > maxMessageBytes) return ERROR_BUFFER_OVERFLOW;
            message.append(buffer.data(), bytes);
            if (error == ERROR_SUCCESS) return message.empty() ? ERROR_INVALID_DATA : ERROR_SUCCESS;
        } while (true);
    }

    DWORD write(const std::string& message)
    {
        if (message.empty() || message.size() > maxMessageBytes) return ERROR_BUFFER_OVERFLOW;
        DWORD bytes = 0;
        const auto error = transfer(true, const_cast<char*>(message.data()), static_cast<DWORD>(message.size()), bytes);
        return error != ERROR_SUCCESS ? error : (bytes == message.size() ? ERROR_SUCCESS : ERROR_WRITE_FAULT);
    }

private:
    DWORD transfer(bool writing, void* data, DWORD size, DWORD& bytes)
    {
        if (stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) return ERROR_OPERATION_ABORTED;
        if (GetTickCount64() >= deadline) return ERROR_TIMEOUT;
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event) return GetLastError();
        OVERLAPPED operation{};
        operation.hEvent = event.get();
        const auto ok = writing ? WriteFile(pipe, data, size, &bytes, &operation)
                                : ReadFile(pipe, data, size, &bytes, &operation);
        return finish(operation, ok ? ERROR_SUCCESS : GetLastError(), bytes);
    }

    DWORD finish(OVERLAPPED& operation, DWORD error, DWORD& bytes)
    {
        if (error == ERROR_SUCCESS || error == ERROR_MORE_DATA)
        {
            // With an overlapped handle lpNumberOfBytesRead is not authoritative,
            // even when ReadFile completes inline (notably ERROR_MORE_DATA).
            const auto ok = GetOverlappedResult(pipe, &operation, &bytes, FALSE);
            return ok ? ERROR_SUCCESS : GetLastError();
        }
        if (error != ERROR_IO_PENDING) return error;
        HANDLE events[] = { operation.hEvent, stop };
        const auto now = GetTickCount64();
        const DWORD remaining = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
        const auto wait = WaitForMultipleObjects(stop ? 2 : 1, events, FALSE, remaining);
        if (wait == WAIT_OBJECT_0)
            return GetOverlappedResult(pipe, &operation, &bytes, FALSE) ? ERROR_SUCCESS : GetLastError();
        const auto failure = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_OPERATION_ABORTED;
        CancelIoEx(pipe, &operation);
        // Required even if completion won the race with cancellation.
        GetOverlappedResult(pipe, &operation, &bytes, TRUE);
        return failure;
    }

    HANDLE pipe;
    HANDLE stop;
    ULONGLONG deadline;
};
}
