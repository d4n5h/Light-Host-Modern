#pragma once
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace lightHost::ipc
{
using StateRevisions = std::array<uint64_t, 5>;
inline constexpr const char* revisionNames[] {"chain", "database", "devices", "scan", "operations"};
using EntityChanges = std::map<std::string, std::vector<std::string>>;

// Controller publication and the event transport share only this bounded log.
// No audio callback enters it, and a slow reader cannot delay the controller.
class StateEvents
{
public:
    struct Batch
    {
        uint64_t sequence = 0;
        StateRevisions revisions{};
        EntityChanges changes;
        bool resyncRequired = false, stopped = false;
    };
    explicit StateEvents(size_t maximumRecords = 256, size_t maximumBytes = 32 * 1024 * 1024)
        : recordLimit(maximumRecords), byteLimit(maximumBytes) {}

    uint64_t publish(const StateRevisions& revisions, EntityChanges changes = {})
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopped || (revisions == current && changes.empty())) return sequence;
        current = revisions;
        Entry entry;
        entry.sequence = ++sequence;
        entry.changes = std::move(changes);
        size_t ids = 0;
        for (const auto& group : entry.changes)
        {
            entry.bytes += group.first.size() + 64;
            ids += group.second.size();
            for (const auto& id : group.second) entry.bytes += id.size() + 64;
        }
        if (ids > 256 || entry.bytes > 64 * 1024 || entry.bytes > byteLimit)
        {
            entry.changes.clear(); entry.resyncRequired = true; entry.bytes = 128;
        }
        bytes += entry.bytes;
        entries.push_back(std::move(entry));
        while (!entries.empty() && (entries.size() > recordLimit || bytes > byteLimit))
        { bytes -= entries.front().bytes; entries.pop_front(); }
        changed.notify_all();
        return sequence;
    }

    Batch read(uint64_t afterSequence, std::chrono::milliseconds wait = {})
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait_for(lock, wait, [&] { return stopped || afterSequence != sequence; });
        Batch result;
        result.sequence = sequence; result.revisions = current; result.stopped = stopped;
        result.resyncRequired = afterSequence > sequence ||
            (afterSequence < sequence && (entries.empty() || afterSequence < entries.front().sequence - 1));
        std::map<std::string, std::set<std::string>> unique;
        size_t ids = 0;
        for (const auto& entry : entries) if (entry.sequence > afterSequence)
        {
            result.resyncRequired |= entry.resyncRequired;
            for (const auto& group : entry.changes)
                for (const auto& id : group.second)
                    if (unique[group.first].insert(id).second) ++ids;
        }
        if (ids > 256) result.resyncRequired = true;
        if (!result.resyncRequired)
            for (const auto& group : unique) result.changes[group.first] = {group.second.begin(), group.second.end()};
        return result;
    }
    void close()
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopped = true; changed.notify_all();
    }
    size_t retainedBytes() const { std::lock_guard<std::mutex> lock(mutex); return bytes; }
    size_t retainedRecords() const { std::lock_guard<std::mutex> lock(mutex); return entries.size(); }
private:
    struct Entry { uint64_t sequence; EntityChanges changes; size_t bytes = 128; bool resyncRequired = false; };
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::deque<Entry> entries;
    StateRevisions current{};
    uint64_t sequence = 0;
    size_t bytes = 0, recordLimit, byteLimit;
    bool stopped = false;
};
}
