#include "IpcOperation.h"
#include "IpcPipe.h"
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

using namespace lightHost::ipc;
using namespace std::chrono_literals;

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static void lifetimeTests()
{
    auto operation = std::make_shared<Operation<int>>();
    auto late = [operation] { operation->execute([] { return 42; }); };
    require(!operation->waitFor(1ms), "timeout should not fabricate a result");
    late();
    require(operation->waitFor(1ms) == 42, "late result must remain available");
    int calls = 0;
    operation->execute([&] { ++calls; return 7; });
    require(calls == 0, "completed operation must not be executed twice");

    auto cancelled = std::make_shared<Operation<int>>();
    cancelled->cancelPending();
    cancelled->execute([&] { ++calls; return 1; });
    require(calls == 0 && !cancelled->waitFor(1ms), "queued cancellation must prevent execution");

    struct Owner { int calls = 0; } owner;
    auto gate = std::make_shared<LifetimeGate<Owner>>(owner);
    auto queued = [gate] { gate->invoke([](Owner& target) { ++target.calls; }); };
    queued();
    gate->close();
    queued();
    require(owner.calls == 1, "closed lifetime must reject queued callbacks");

    auto running = std::make_shared<Operation<int>>();
    std::promise<void> started, finish;
    auto ready = finish.get_future();
    std::thread worker([&] { running->execute([&] { started.set_value(); ready.wait(); return 9; }); });
    started.get_future().wait();
    running->cancelPending();
    finish.set_value();
    worker.join();
    require(running->waitFor(10ms) == 9, "started operation must retain completion after cancellation");
    Operation<int> throwing;
    throwing.execute([]() -> int { throw std::runtime_error("command failed"); });
    bool reported = false;
    try { throwing.waitFor(1ms); } catch (const std::runtime_error&) { reported = true; }
    require(reported, "throwing work must wake waiters and preserve its error");
}

static void pipeTests()
{
    const auto name = L"\\\\.\\pipe\\LightHost-test-" + std::to_wstring(GetCurrentProcessId());
    Handle server(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 4096, 4096, 1000, nullptr));
    require(static_cast<bool>(server), "create test pipe");
    StopEvent stop;
    auto connect = std::async(std::launch::async, [&] { return PipeIo(server.get(), stop.get(), 2000).connect(); });
    Handle client(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, nullptr));
    require(static_cast<bool>(client), "connect test client");
    require(connect.get() == ERROR_SUCCESS, "accept client");
    DWORD mode = PIPE_READMODE_MESSAGE;
    require(SetNamedPipeHandleState(client.get(), &mode, nullptr, nullptr) != FALSE, "set message read mode");

    std::string payload(300000, 'x');
    payload += u8" — português 日本語 🎵";
    auto send = std::async(std::launch::async, [&] { return PipeIo(client.get(), nullptr, 2000).write(payload); });
    std::string received;
    require(PipeIo(server.get(), stop.get(), 2000).read(received) == ERROR_SUCCESS, "partial reads must form one complete message");
    require(send.get() == ERROR_SUCCESS && received == payload, "large Unicode message must round trip exactly");
    require(PipeIo(client.get(), nullptr, 100).write(std::string(maxMessageBytes + 1, 'x')) == ERROR_BUFFER_OVERFLOW,
        "reject oversized outgoing messages");

    require(PipeIo(server.get(), stop.get(), 20).read(received) == ERROR_TIMEOUT, "idle read deadline");
    auto pending = std::async(std::launch::async, [&] { return PipeIo(server.get(), stop.get(), 10000).read(received); });
    stop.signal();
    require(pending.wait_for(1s) == std::future_status::ready, "shutdown must interrupt idle client");
    require(pending.get() == ERROR_OPERATION_ABORTED, "read cancelled");
    DisconnectNamedPipe(server.get());
    require(PipeIo(client.get(), nullptr, 100).read(received) != ERROR_SUCCESS, "disconnect must fail read");

    StopEvent connectStop;
    auto idle = std::async(std::launch::async, [&] { return PipeIo(server.get(), connectStop.get(), 10000).connect(); });
    connectStop.signal();
    require(idle.wait_for(1s) == std::future_status::ready, "shutdown must interrupt pending connection");
    require(idle.get() == ERROR_OPERATION_ABORTED, "connection cancelled");
}

int main()
{
    try { lifetimeTests(); pipeTests(); std::cout << "IPC lifetime and transport regressions passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
