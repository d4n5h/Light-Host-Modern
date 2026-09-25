#pragma once
#include "PluginInstances.h"
#include <deque>
#include <optional>

namespace lightHostModern
{
class ChainHistory
{
public:
    void record(const PluginInstances& before, const juce::String& coalesceKey = {})
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (coalesceKey.isNotEmpty() && coalesceKey == lastKey && now - lastRecordMs < 1500.0 && !undoStack.empty())
            return;
        undoStack.push_back(before);
        redoStack.clear();
        lastKey = coalesceKey;
        lastRecordMs = now;
        trim();
    }

    std::optional<PluginInstances> undo(const PluginInstances& current)
    {
        if (undoStack.empty()) return {};
        redoStack.push_back(current);
        auto restored = std::move(undoStack.back());
        undoStack.pop_back();
        lastKey.clear();
        return restored;
    }

    std::optional<PluginInstances> redo(const PluginInstances& current)
    {
        if (redoStack.empty()) return {};
        undoStack.push_back(current);
        auto restored = std::move(redoStack.back());
        redoStack.pop_back();
        lastKey.clear();
        return restored;
    }

    void clear() { undoStack.clear(); redoStack.clear(); lastKey.clear(); }
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    static constexpr size_t maximumEntries = 50;
    static constexpr size_t maximumStateBytes = 64u * 1024u * 1024u;

private:
    static size_t bytes(const PluginInstances& chain)
    {
        size_t total = 0;
        for (const auto& record : chain.records)
            total += static_cast<size_t>(record.lastValidState.getNumBytesAsUTF8() + record.recoveryState.getNumBytesAsUTF8());
        return total;
    }
    void trim()
    {
        while (undoStack.size() > maximumEntries) undoStack.pop_front();
        size_t total = 0;
        for (const auto& chain : undoStack) total += bytes(chain);
        for (const auto& chain : redoStack) total += bytes(chain);
        while (total > maximumStateBytes && !undoStack.empty())
        {
            total -= bytes(undoStack.front());
            undoStack.pop_front();
        }
    }
    std::deque<PluginInstances> undoStack, redoStack;
    juce::String lastKey;
    double lastRecordMs = 0;
};
}
