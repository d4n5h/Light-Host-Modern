#pragma once
#include "SessionStore.h"

namespace lightHostModern
{
struct ChainProfile
{
    juce::String id, name;
};

struct ChainProfileCatalog
{
    int formatVersion = 1;
    juce::String activeId;
    std::vector<ChainProfile> profiles;
};

// Named copies of the running chain, stored beside the preferences file.
// The live session file stays the recovery source. Profile bytes are replaced
// only for the profile that was explicitly saved or that is currently active.
class ChainProfileStore
{
public:
    static constexpr int maximumProfiles = 32;
    explicit ChainProfileStore(const juce::File& preferencesFile);
    ChainProfileCatalog catalog() const { return current; }
    const juce::File& root() const { return directory; }
    bool catalogBroken() const { return broken; }
    static juce::String contentHash(const PluginInstances&, const juce::String& migrationId);
    juce::String ensureDefault(const PluginInstances&, const juce::String& migrationId, const juce::String& sessionContentHash);
    juce::String create(const PluginInstances&, const juce::String& migrationId, const juce::String& name, juce::String& id);
    juce::String switchTo(const juce::String& id, PluginInstances& destination, juce::String& migrationId) const;
    juce::String writeActive(const PluginInstances&, const juce::String& migrationId);
    juce::String setActive(const juce::String& id);
    juce::String rename(const juce::String& id, const juce::String& name);
    juce::String duplicate(const juce::String& id, juce::String& newId);
    juce::String remove(const juce::String& id);
    juce::String move(const juce::String& id, int delta);
private:
    juce::File fileFor(const juce::String& id) const { return directory.getChildFile(id + ".json"); }
    const ChainProfile* find(const juce::String& id) const;
    juce::String validateName(const juce::String& name, const juce::String& exceptId, juce::String& normalized) const;
    juce::String copyName(const juce::String& base) const;
    juce::String newId() const;
    juce::String readContentHash(const juce::String& id) const;
    juce::String writeProfile(const juce::String& id, const PluginInstances&, const juce::String& migrationId);
    juce::String saveCatalog();
    juce::String reconcile(const juce::String& sessionContentHash);
    void loadCatalog();
    juce::File directory;
    juce::File catalogFile;
    ChainProfileCatalog current;
    bool broken = false;
};
}
