#include "ChainProfileStore.h"
#include "ScenarioRunner.h"
#include <algorithm>
#include <cstring>
#include <string>

using namespace juce;
using namespace lightHostModern;
using scenarios::require;

namespace
{
PluginInstances chain(const char* state, bool bypassed, const char* custom, int uid = 42)
{
    PluginInstances instances;
    PluginInstanceRecord record;
    record.id = Uuid().toString().removeCharacters("-").toLowerCase();
    record.description.name = "Effect";
    record.description.pluginFormatName = "VST3";
    record.description.fileOrIdentifier = "C:\\Plugins\\Effect.vst3";
    record.description.uniqueId = uid;
    record.originalIdentity = knownPluginId(record.description);
    record.bypassed = bypassed;
    record.customName = custom;
    record.lastValidState = MemoryBlock(state, (int) strlen(state)).toBase64Encoding();
    instances.records.push_back(std::move(record));
    return instances;
}
String profileBytes(const ChainProfileStore& store, const String& id)
{
    return store.root().getChildFile(id + ".json").loadFileAsString();
}
struct TempPreferences
{
    File preferences;
    explicit TempPreferences(const String& name)
        : preferences(File::getSpecialLocation(File::tempDirectory).getChildFile("lhm-profiles-" + name + "-" + Uuid().toString())
            .getChildFile("LightHostModern.settings"))
    {
    }
    ~TempPreferences() { preferences.getParentDirectory().deleteRecursively(); }
};
}

int main()
{
    scenarios::Runner tests;
    tests.run("session codec round trip", [] {
        const auto instances = chain("block", true, "Lead");
        const auto encoded = SessionCodec::encode(SessionDocument{instances, 1, "mig", false});
        String error;
        const auto decoded = SessionCodec::decode(encoded.bytes, error);
        require(decoded.has_value() && error.isEmpty(), "decode");
        require(decoded->instances.records.size() == 1, "one record");
        require(decoded->instances.records[0].id == instances.records[0].id, "id");
        require(decoded->instances.records[0].customName == "Lead", "name");
        require(decoded->instances.records[0].bypassed, "bypass");
        require(decoded->instances.records[0].lastValidState == instances.records[0].lastValidState, "state");
        require(decoded->migrationId == "mig", "migration");
    });
    tests.run("create rename duplicate delete and limits", [] {
        TempPreferences temp("crud");
        const auto first = chain("a", false, "Clean");
        const auto second = chain("b", true, "Lead", 43);
        ChainProfileStore store(temp.preferences);
        String id;
        require(store.ensureDefault(first, "mig", {}).isEmpty(), "default");
        require(store.catalog().profiles.size() == 1 && store.catalog().profiles[0].name == "Default", "default name");
        require(store.catalog().activeId == store.catalog().profiles[0].id, "default active");
        const auto defaultId = store.catalog().activeId;
        PluginInstances loaded;
        String migration;
        require(store.switchTo(defaultId, loaded, migration).isEmpty(), "switch decode");
        require(store.catalog().activeId == defaultId, "switch does not retarget");
        require(loaded.records.size() == 1 && loaded.records[0].customName == "Clean" && !loaded.records[0].bypassed, "decoded chain");
        require(loaded.strips.size() == 1 && loaded.strips[0].name == "Main" && loaded.records[0].stripId == loaded.strips[0].id, "profile strip");
        require(loaded.records[0].lastValidState == first.records[0].lastValidState, "decoded state");
        require(migration == "mig", "decoded migration");
        require(store.create(second, "mig", "default", id) == "profile_name_taken", "case-insensitive name");
        require(store.create(second, "mig", "  ", id) == "profile_name_invalid", "empty name");
        require(store.create(second, "mig", String(std::string(129, 'n')), id) == "profile_name_invalid", "long name");
        require(store.create(second, "mig", "Lead", id).isEmpty(), "create");
        require(store.catalog().activeId == id && store.catalog().profiles.size() == 2, "create activates");
        require(store.rename(id, "Stage").isEmpty(), "rename");
        require(store.rename(id, "Default") == "profile_name_taken", "rename clash");
        String copy;
        require(store.duplicate(id, copy).isEmpty(), "duplicate");
        require(store.catalog().activeId == id, "duplicate stays put");
        const auto catalog = store.catalog();
        const auto copied = std::find_if(catalog.profiles.begin(), catalog.profiles.end(),
            [&](const ChainProfile& profile) { return profile.id == copy; });
        require(copied != catalog.profiles.end() && copied->name == "Stage 2", "copy name");
        require(profileBytes(store, copy) == profileBytes(store, id), "copy bytes");
        require(store.remove(id) == "profile_active", "active delete refused");
        require(store.remove(defaultId).isEmpty(), "delete inactive");
        require(store.remove(copy).isEmpty(), "delete copy");
        require(store.catalog().profiles.size() == 1, "one remains");
        require(store.remove(id) == "last_profile", "last profile");
    });
    tests.run("profile cap", [] {
        TempPreferences temp("cap");
        ChainProfileStore store(temp.preferences);
        for (int index = 0; index < ChainProfileStore::maximumProfiles; ++index)
        {
            String id;
            require(store.create(chain("x", false, "Slot", index + 1), "mig", "P" + String(index), id).isEmpty(), "fill");
        }
        String extra;
        require(store.create(chain("x", false, "Slot"), "mig", "Overflow", extra) == "profile_limit", "33rd profile");
        require(store.duplicate(store.catalog().activeId, extra) == "profile_limit", "duplicate at cap");
        require((int) store.catalog().profiles.size() == ChainProfileStore::maximumProfiles, "cap held");
    });
    tests.run("startup hash match and abandoned catalog", [] {
        TempPreferences temp("adopt");
        const auto first = chain("keep", false, "A", 7);
        const auto second = chain("other", true, "B", 8);
        ChainProfileStore store(temp.preferences);
        String firstId, secondId;
        require(store.create(first, "mig", "A", firstId).isEmpty(), "profile A");
        require(store.create(second, "mig", "B", secondId).isEmpty(), "profile B");
        const auto catalogBytes = store.root().getChildFile("catalog.json").loadFileAsString();
        const auto firstBytes = profileBytes(store, firstId);
        const auto secondBytes = profileBytes(store, secondId);
        store.root().getChildFile("catalog.json.pending").replaceWithText("not-a-catalog");
        ChainProfileStore reloaded(temp.preferences);
        require(reloaded.catalog().activeId == secondId, "pending file is ignored");
        require(reloaded.root().getChildFile("catalog.json").loadFileAsString() == catalogBytes, "catalog bytes");
        const auto hash = ChainProfileStore::contentHash(first, "mig");
        require(reloaded.ensureDefault(first, "mig", hash).isEmpty(), "adopt");
        require(reloaded.catalog().activeId == firstId, "matching profile becomes active");
        require(profileBytes(reloaded, firstId) == firstBytes && profileBytes(reloaded, secondId) == secondBytes, "adopt keeps bytes");
        ChainProfileStore unmatched(temp.preferences);
        require(unmatched.ensureDefault(first, "mig", "deadbeef").isEmpty(), "unsaved");
        require(unmatched.catalog().activeId.isEmpty(), "active cleared");
        require(profileBytes(unmatched, firstId) == firstBytes && profileBytes(unmatched, secondId) == secondBytes, "mismatch keeps bytes");
    });
    std::cout << "Chain profile tests finished\n";
    return tests.result();
}
