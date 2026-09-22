#pragma once
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace lightHostModern::ipc
{
enum class OperationState { queued, running, completed, failed, cancelled };
inline const char* stateName(OperationState state)
{
    switch (state)
    {
        case OperationState::queued: return "queued";
        case OperationState::running: return "running";
        case OperationState::completed: return "completed";
        case OperationState::failed: return "failed";
        case OperationState::cancelled: return "cancelled";
    }
    return "failed";
}
inline bool terminal(OperationState state)
{
    return state != OperationState::queued && state != OperationState::running;
}

// The transport owns acceptance and lookup; only the controller executes work.
// All time and capacity decisions are deterministic with an injected clock.
class OperationRegistry
{
public:
    using Clock = std::chrono::steady_clock;
    struct Record
    {
        std::string id, content, response;
        OperationState state = OperationState::queued;
        Clock::time_point finished{};
        size_t order = 0;
        size_t bytes() const { return id.size() + content.size() + response.size(); }
    };
    struct Admission { bool inserted = false; std::string error; std::optional<Record> record; };
    struct Limits
    {
        size_t completed = 256, bytes = 32 * 1024 * 1024, pending = 256;
        std::chrono::seconds retention{600};
    };
    explicit OperationRegistry(std::function<Clock::time_point()> now = [] { return std::chrono::steady_clock::now(); })
        : OperationRegistry(Limits{}, std::move(now)) {}
    OperationRegistry(Limits limits, std::function<Clock::time_point()> now)
        : limits(limits), now(std::move(now)) {}

    Admission accept(std::string id, std::string content)
    {
        std::lock_guard<std::mutex> lock(mutex);
        prune();
        if (const auto found = records.find(id); found != records.end())
        {
            if (found->second.content != content) return {false, "request_id_conflict", {}};
            return {false, {}, found->second};
        }
        if (closed) return {false, "shutting_down", {}};
        size_t pending = 0, pendingBytes = 0;
        for (const auto& item : records)
            if (!terminal(item.second.state)) { ++pending; pendingBytes += item.second.bytes(); }
        if (pending >= limits.pending || id.size() + content.size() > limits.bytes - pendingBytes)
            return {false, "operation_capacity", {}};
        Record record{std::move(id), std::move(content), {}, OperationState::queued, {}, ++order};
        records.emplace(record.id, record);
        prune();
        return {true, {}, std::move(record)};
    }
    std::optional<Record> find(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(mutex);
        prune();
        if (const auto found = records.find(id); found != records.end()) return found->second;
        return {};
    }
    bool start(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = records.find(id);
        if (closed || found == records.end() || found->second.state != OperationState::queued) return false;
        found->second.state = OperationState::running;
        return true;
    }
    void finish(const std::string& id, std::string response, bool success)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = records.find(id);
        if (found == records.end() || found->second.state != OperationState::running) return;
        auto& record = found->second;
        record.response = std::move(response);
        record.state = success ? OperationState::completed : OperationState::failed;
        record.finished = now();
        record.order = ++order;
        prune();
    }
    void cancel(const std::string& id, const std::string& response)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = records.find(id);
        if (found == records.end() || found->second.state != OperationState::queued) return;
        found->second.state = OperationState::cancelled;
        found->second.response = response;
        found->second.finished = now();
        found->second.order = ++order;
        prune();
    }
    void close(const std::function<std::string(const std::string&)>& error)
    {
        std::lock_guard<std::mutex> lock(mutex);
        closed = true;
        for (auto& item : records)
            if (item.second.state == OperationState::queued)
            {
                item.second.state = OperationState::cancelled;
                item.second.finished = now();
                item.second.response = error(item.first);
                item.second.order = ++order;
            }
        prune();
    }
private:
    void prune()
    {
        size_t completed = 0, completedBytes = 0;
        const auto time = now();
        for (auto it = records.begin(); it != records.end();)
        {
            const auto& record = it->second;
            if (terminal(record.state) && time - record.finished >= limits.retention) it = records.erase(it);
            else
            {
                if (terminal(record.state)) { ++completed; completedBytes += record.bytes(); }
                ++it;
            }
        }
        while (completed > limits.completed || completedBytes > limits.bytes)
        {
            auto oldest = records.end();
            for (auto it = records.begin(); it != records.end(); ++it)
                if (terminal(it->second.state) && (oldest == records.end() || it->second.order < oldest->second.order)) oldest = it;
            if (oldest == records.end()) break;
            completedBytes -= oldest->second.bytes();
            --completed;
            records.erase(oldest);
        }
    }
    Limits limits;
    std::function<Clock::time_point()> now;
    std::mutex mutex;
    std::map<std::string, Record> records;
    size_t order = 0;
    bool closed = false;
};
}
