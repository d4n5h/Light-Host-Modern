#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <exception>

// The queued closure owns its result. A caller may abandon its wait without
// invalidating either the closure or its completion notification.
namespace lightHost::ipc
{
template <typename Result>
class Operation
{
public:
    template <typename Work>
    void execute(Work&& work)
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (cancelled || started)
            return;
        started = true;
        lock.unlock();
        std::optional<Result> value;
        std::exception_ptr failure;
        try { value = work(); }
        catch (...) { failure = std::current_exception(); }
        lock.lock();
        result = std::move(value);
        error = failure;
        completed.notify_all();
    }

    void cancelPending()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!started)
            cancelled = true;
        completed.notify_all();
    }

    template <typename Duration>
    std::optional<Result> waitFor(Duration duration)
    {
        std::unique_lock<std::mutex> lock(mutex);
        completed.wait_for(lock, duration, [this] { return result.has_value() || error || cancelled; });
        if (error) std::rethrow_exception(error);
        return result;
    }

private:
    std::mutex mutex;
    std::condition_variable completed;
    bool started = false;
    bool cancelled = false;
    std::optional<Result> result;
    std::exception_ptr error;
};

// Serializes destruction against invocation. Closures retain the gate, never
// the raw owner. close() must precede releasing any state used by the callback.
template <typename Owner>
class LifetimeGate
{
public:
    explicit LifetimeGate(Owner& value) : owner(&value) {}
    template <typename Work>
    void invoke(Work&& work)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (owner != nullptr)
            work(*owner);
    }
    void close()
    {
        std::lock_guard<std::mutex> lock(mutex);
        owner = nullptr;
    }
private:
    std::mutex mutex;
    Owner* owner;
};
}
