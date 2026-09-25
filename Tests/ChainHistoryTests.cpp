#include "ChainHistory.h"
#include "ScenarioRunner.h"
#include <cstring>

using namespace juce;
using namespace lightHostModern;
using scenarios::require;

static PluginInstances chain(const char* state)
{
    PluginInstances instances;
    instances.ensureStrips();
    PluginInstanceRecord record;
    record.id = Uuid().toString().removeCharacters("-").toLowerCase();
    record.lastValidState = MemoryBlock(state, (int) std::strlen(state)).toBase64Encoding();
    record.stripId = instances.strips.front().id;
    instances.records.push_back(std::move(record));
    return instances;
}

int main()
{
    scenarios::Runner tests;
    tests.run("undo and redo restore order and a new edit clears redo", [] {
        ChainHistory history;
        auto first = chain("one");
        auto second = chain("two");
        auto third = chain("three");
        history.record(first);
        history.record(second);
        auto undone = history.undo(third);
        require(undone && undone->records[0].lastValidState == second.records[0].lastValidState, "undo");
        require(history.canRedo(), "redo available");
        history.record(first);
        require(!history.canRedo(), "redo cleared");
        require(history.undo(first)->records[0].lastValidState == first.records[0].lastValidState, "latest undo");
    });
    tests.run("limits drop the oldest entry and gains coalesce", [] {
        ChainHistory history;
        for (int index = 0; index < 60; ++index) history.record(chain("x"), {});
        require(history.canUndo(), "history kept");
        int steps = 0;
        auto current = chain("last");
        while (auto restored = history.undo(current)) { current = *restored; ++steps; }
        require(steps == (int) ChainHistory::maximumEntries, "entry cap");
        history.record(chain("big"), "gain:a");
        history.record(chain("bigger"), "gain:a");
        steps = 0; current = chain("now");
        while (history.undo(current)) ++steps;
        require(steps == 1, "same fader coalesced");
        history.clear();
        require(!history.canUndo() && !history.canRedo(), "clear");
    });
    tests.run("byte limit drops heavy states", [] {
        ChainHistory history;
        PluginInstances heavy;
        heavy.ensureStrips();
        PluginInstanceRecord record;
        record.id = Uuid().toString().removeCharacters("-").toLowerCase();
        record.lastValidState = String::repeatedString("abcdefg", 256 * 1024);
        record.stripId = heavy.strips.front().id;
        heavy.records.push_back(record);
        for (int index = 0; index < 50; ++index) history.record(heavy);
        int steps = 0;
        auto current = heavy;
        while (history.undo(current)) ++steps;
        require(steps < 50 && steps > 0, "byte cap");
    });
    std::cout << "Chain history tests finished\n";
    return tests.result();
}
