#pragma once
#include <atomic>
#include <cstdint>
#include "ProcessMetrics.h"

bool installRealtimeAllocationAudit();

namespace lightHostModern::realtimeAudit
{
// False in ordinary builds, or if executable-local CRT interception failed.
// Third-party DLL imports are never patched by the host audit.
inline std::atomic<bool> available { false };
enum class Origin { outsideCallback, host, plugin };
inline thread_local Origin origin = Origin::outsideCallback;
inline std::atomic<std::uint64_t> hostAllocations {0}, hostFrees {0}, pluginAllocations {0}, pluginFrees {0};
struct Scope
{
    explicit Scope(Origin current) noexcept : previous(origin) { origin = current; }
    ~Scope() { origin = previous; }
    Origin previous;
};
// Instrumentation hooks also used by the Release test's CRT import interception.
inline void allocation() noexcept
{
    if (!diagnosticsCollectionEnabled.load(std::memory_order_relaxed)) return;
    if (origin == Origin::host) hostAllocations.fetch_add(1, std::memory_order_relaxed);
    if (origin == Origin::plugin) pluginAllocations.fetch_add(1, std::memory_order_relaxed);
}

inline void release() noexcept
{
    if (!diagnosticsCollectionEnabled.load(std::memory_order_relaxed)) return;
    if (origin == Origin::host) hostFrees.fetch_add(1, std::memory_order_relaxed);
    if (origin == Origin::plugin) pluginFrees.fetch_add(1, std::memory_order_relaxed);
}
}
