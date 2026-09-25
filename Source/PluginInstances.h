#pragma once
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <juce_cryptography/juce_cryptography.h>
#include "PluginInstanceId.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <set>
#include <vector>

namespace lightHostModern
{
inline bool normalizeInstanceName(const juce::String& input, juce::String& normalized)
{
    // JUCE counts Unicode code points, including supplementary characters.
    for (const auto character : input)
        if (character < 0x20 || (character >= 0x7f && character <= 0x9f)
            || character == 0x2028 || character == 0x2029) return false;
    normalized = input.trim();
    return normalized.length() <= 128;
}

// A class identity never contains instance position, display name, version or a
// modified vendor UID. Length prefixes make even unusual identifiers unambiguous.
inline juce::String knownPluginId(const juce::PluginDescription& description)
{
    const auto field = [](const juce::String& text) { return juce::String(text.length()) + ":" + text; };
    const auto identity = field(description.pluginFormatName) + field(description.fileOrIdentifier)
        + field(juce::String::toHexString(description.uniqueId != 0 ? description.uniqueId : description.deprecatedUid));
    return juce::SHA256(identity.toRawUTF8(), identity.getNumBytesAsUTF8()).toHexString();
}

inline constexpr int maximumStrips = 192;

struct ChainStrip
{
    juce::String id, name = "Main";
    bool allInputs = true, allOutputs = true;
    bool stereo = false;
    std::vector<int> inputs, outputs;
    float gainDb = 0.0f;
    float pan = 0.0f;
    int color = 0;
    juce::String colour;
    bool muted = false;
    bool solo = false;
    juce::String group;
};

inline juce::String defaultStripId()
{
    const auto seed = juce::String("LightHostModern default strip");
    return juce::SHA256(seed.toRawUTF8(), seed.getNumBytesAsUTF8()).toHexString().substring(0, 32);
}

inline ChainStrip defaultStrip()
{
    ChainStrip strip;
    strip.id = defaultStripId();
    strip.name = "Main";
    return strip;
}

inline float clampGainDb(float db)
{
    return juce::jlimit(-60.0f, 12.0f, db);
}

inline float clampPan(float pan)
{
    return juce::jlimit(-1.0f, 1.0f, pan);
}

inline float gainFromDb(float db)
{
    db = clampGainDb(db);
    return db <= -60.0f ? 0.0f : std::pow(10.0f, db / 20.0f);
}

struct PluginInstanceRecord
{
    PluginInstanceId id;
    juce::String originalIdentity;
    juce::PluginDescription description;
    juce::String legacyDescription; // exact imported description, including a possibly synthetic UID
    juce::String customName;
    juce::String lastValidState;
    juce::String recoveryState; // undecodable/failed state must remain recoverable
    juce::String loading = "unloaded";
    juce::String error;
    juce::String stripId;
    bool bypassed = false;
    bool identityResolved = true;
    bool stateCaptureAllowed = true;
    bool editorOpen = false;
    bool hasEditorPosition = false;
    bool hasEditorSize = false;
    int editorX = 0, editorY = 0;
    int editorW = 0, editorH = 0;

    juce::String displayName() const { return customName.isEmpty() ? description.name : customName; }
};

class PluginInstances
{
public:
    std::vector<ChainStrip> strips;
    std::vector<PluginInstanceRecord> records;
    float masterGainDb = 0.0f;
    juce::String recoveryError;
    bool writable = true;

    void ensureStrips()
    {
        if (strips.empty()) strips.push_back(defaultStrip());
        if (strips.size() > (size_t) maximumStrips) strips.resize((size_t) maximumStrips);
        for (auto& record : records)
            if (record.stripId.isEmpty() || std::none_of(strips.begin(), strips.end(), [&](const auto& strip) { return strip.id == record.stripId; }))
                record.stripId = strips.front().id;
    }

    const ChainStrip* findStrip(const juce::String& id) const
    {
        for (const auto& strip : strips) if (strip.id == id) return &strip;
        return nullptr;
    }

    static juce::String legacyBaseKey(const juce::String& type, const juce::PluginDescription& plugin)
    {
        return "plugin-" + type.toLowerCase() + "-" + juce::String::toHexString(plugin.createIdentifierString().hashCode64());
    }
    static juce::String legacyKey(const juce::String& type, const juce::PluginDescription& plugin)
    {
        return legacyBaseKey(type, plugin) + "-" + juce::String::toHexString(plugin.deprecatedUid);
    }

    // Called before any description normalization. Never deduplicate the XML.
    void migrate(const juce::XmlElement& legacy, juce::PropertySet& settings,
                 const juce::Array<juce::PluginDescription>& known, const juce::String& migrationId = {})
    {
        records.clear();
        std::vector<juce::PluginDescription> descriptions;
        for (const auto* child : legacy.getChildIterator())
        {
            juce::PluginDescription description;
            if (description.loadFromXml(*child)) descriptions.push_back(std::move(description));
            else { recoveryError = "Unrecognized legacy session entry"; writable = false; }
        }
        juce::String importStateError;
        const auto read = [&](const juce::String& type, const juce::PluginDescription& description) {
            const auto exact = legacyKey(type, description);
            if (settings.containsKey(exact)) return settings.getValue(exact);
            const auto base = legacyBaseKey(type, description);
            if (settings.containsKey(base))
            {
                const auto count = std::count_if(descriptions.begin(), descriptions.end(), [&](const auto& other) {
                    return legacyBaseKey(type, other) == base;
                });
                if (type != "state" || count == 1) return settings.getValue(base);
                recoveryError = "Ambiguous legacy state retained in the original settings";
                importStateError = recoveryError;
                return juce::String();
            }
            const auto oldKey = "plugin-" + type.toLowerCase() + "-" + description.name + description.version + description.pluginFormatName;
            const auto count = std::count_if(descriptions.begin(), descriptions.end(), [&](const auto& other) {
                return other.name + other.version + other.pluginFormatName == description.name + description.version + description.pluginFormatName;
            });
            if (settings.containsKey(oldKey) && type == "state" && count > 1)
            {
                recoveryError = "Ambiguous legacy state retained in the original settings";
                importStateError = recoveryError;
                return juce::String();
            }
            return settings.getValue(oldKey);
        };
        std::stable_sort(descriptions.begin(), descriptions.end(), [&](const auto& a, const auto& b) {
            const auto orderA = read("order", a).getIntValue(), orderB = read("order", b).getIntValue();
            if (orderA == orderB) return a.name.compareNatural(b.name) < 0;
            return orderA > 0 && (orderB <= 0 || orderA < orderB);
        });
        juce::StringArray assigned;
        for (const auto& legacyDescription : descriptions)
        {
            PluginInstanceRecord record;
            const auto idKey = legacyKey("instance-id", legacyDescription);
            auto id = settings.getValue(idKey);
            if (migrationId.isNotEmpty() && (id.length() != 32 || !id.containsOnly("0123456789abcdef")
                || juce::Uuid(id).isNull() || assigned.contains(id)))
            {
                const auto seed = migrationId + ":" + juce::String(static_cast<int>(records.size())) + ":" + legacyDescription.createXml()->toString();
                id = juce::SHA256(seed.toRawUTF8(), seed.getNumBytesAsUTF8()).toHexString().substring(0, 32);
                settings.setValue(idKey, id);
            }
            record.id = ensurePluginInstanceId(settings, idKey, assigned);
            record.bypassed = read("bypass", legacyDescription).getIntValue() != 0;
            importStateError.clear();
            record.lastValidState = read("state", legacyDescription);
            record.error = read("failed", legacyDescription);
            if (importStateError.isNotEmpty()) { record.error = importStateError; record.stateCaptureAllowed = false; }
            record.legacyDescription = legacyDescription.createXml()->toString();
            record.description = legacyDescription;
            const juce::PluginDescription* original = nullptr;
            int matches = 0;
            for (const auto& candidate : known)
                if (candidate.pluginFormatName == legacyDescription.pluginFormatName
                    && candidate.fileOrIdentifier == legacyDescription.fileOrIdentifier
                    && (legacyDescription.uniqueId != 0 ? candidate.uniqueId == legacyDescription.uniqueId
                        : candidate.uniqueId == 0 && candidate.deprecatedUid == legacyDescription.deprecatedUid))
                { original = &candidate; ++matches; }
            record.identityResolved = matches == 1;
            if (record.identityResolved) record.description = *original;
            record.originalIdentity = knownPluginId(record.description);
            validateState(record);
            if (record.error.isNotEmpty()) record.loading = "failed";
            records.push_back(std::move(record));
        }
        strips = { defaultStrip() };
        masterGainDb = 0.0f;
        for (auto& record : records) record.stripId = strips.front().id;
    }

    std::unique_ptr<juce::XmlElement> serialize(juce::uint64 revision = 0) const
    {
        auto written = strips.empty() ? std::vector<ChainStrip>{ defaultStrip() } : strips;
        auto root = std::make_unique<juce::XmlElement>("LIGHTHOSTSESSION");
        root->setAttribute("version", 2);
        root->setAttribute("revision", juce::String(revision));
        root->setAttribute("recoveryError", recoveryError);
        root->setAttribute("masterGainDb", clampGainDb(masterGainDb));
        const auto channels = [](const std::vector<int>& values) {
            juce::String text;
            for (size_t i = 0; i < values.size(); ++i) text += (i ? "," : "") + juce::String(values[i]);
            return text;
        };
        for (const auto& strip : written)
        {
            auto* item = root->createNewChildElement("STRIP");
            item->setAttribute("id", strip.id);
            item->setAttribute("name", strip.name);
            item->setAttribute("allInputs", strip.allInputs);
            item->setAttribute("allOutputs", strip.allOutputs);
            item->setAttribute("stereo", strip.stereo);
            item->setAttribute("gainDb", clampGainDb(strip.gainDb));
            item->setAttribute("pan", clampPan(strip.pan));
            item->setAttribute("color", juce::jlimit(0, 8, strip.color));
            if (strip.colour.isNotEmpty()) item->setAttribute("colour", strip.colour);
            item->setAttribute("muted", strip.muted);
            item->setAttribute("solo", strip.solo);
            item->setAttribute("group", strip.group);
            item->setAttribute("inputs", channels(strip.inputs));
            item->setAttribute("outputs", channels(strip.outputs));
        }
        for (const auto& record : records)
        {
            auto* item = root->createNewChildElement("INSTANCE");
            item->setAttribute("id", record.id);
            item->setAttribute("identity", record.originalIdentity);
            item->setAttribute("identityResolved", record.identityResolved);
            item->setAttribute("bypassed", record.bypassed);
            item->setAttribute("customName", record.customName);
            item->setAttribute("error", record.error);
            item->setAttribute("loading", record.loading);
            item->setAttribute("stateCaptureAllowed", record.stateCaptureAllowed);
            item->setAttribute("strip", record.stripId.isEmpty() ? written.front().id : record.stripId);
            item->setAttribute("editorOpen", record.editorOpen);
            if (record.hasEditorPosition)
            {
                item->setAttribute("editorX", record.editorX);
                item->setAttribute("editorY", record.editorY);
            }
            if (record.hasEditorSize)
            {
                item->setAttribute("editorW", record.editorW);
                item->setAttribute("editorH", record.editorH);
            }
            item->addChildElement(record.description.createXml().release());
            item->createNewChildElement("STATE")->addTextElement(record.lastValidState);
            item->createNewChildElement("RECOVERYSTATE")->addTextElement(record.recoveryState);
            item->createNewChildElement("LEGACY")->addTextElement(record.legacyDescription);
        }
        return root;
    }

    bool deserialize(const juce::XmlElement& root)
    {
        // Transactional: malformed data cannot turn a previously loaded session into an empty one.
        const int version = root.getIntAttribute("version");
        if (!root.hasTagName("LIGHTHOSTSESSION") || (version != 1 && version != 2)) return false;
        std::vector<ChainStrip> loadedStrips;
        std::vector<PluginInstanceRecord> loaded;
        std::set<juce::String> ids, stripIds;
        const auto channels = [](const juce::String& text, std::vector<int>& values) {
            values.clear();
            if (text.isEmpty()) return true;
            for (auto part : juce::StringArray::fromTokens(text, ",", ""))
            {
                part = part.trim();
                if (part.isEmpty() || !part.containsOnly("0123456789")) return false;
                const int channel = part.getIntValue();
                if (channel > 255 || std::find(values.begin(), values.end(), channel) != values.end()) return false;
                values.push_back(channel);
            }
            return true;
        };
        for (const auto* item : root.getChildIterator())
        {
            if (item->hasTagName("STRIP"))
            {
                if (version != 2) return false;
                ChainStrip strip;
                strip.id = item->getStringAttribute("id").toLowerCase();
                juce::String name;
                if (!validStripId(strip.id) || !stripIds.insert(strip.id).second
                    || !normalizeInstanceName(item->getStringAttribute("name"), name) || name.isEmpty()) return false;
                strip.name = name;
                strip.allInputs = item->getBoolAttribute("allInputs", true);
                strip.allOutputs = item->getBoolAttribute("allOutputs", true);
                strip.stereo = item->getBoolAttribute("stereo", false);
                strip.gainDb = clampGainDb(static_cast<float>(item->getDoubleAttribute("gainDb")));
                strip.pan = clampPan(static_cast<float>(item->getDoubleAttribute("pan", 0.0)));
                strip.color = juce::jlimit(0, 8, item->getIntAttribute("color", 0));
                strip.colour = item->getStringAttribute("colour").trim().toLowerCase();
                if (strip.colour.length() != 6 || !strip.colour.containsOnly("0123456789abcdef")) strip.colour.clear();
                strip.muted = item->getBoolAttribute("muted", false);
                strip.solo = item->getBoolAttribute("solo", false);
                strip.group = item->getStringAttribute("group").trim();
                if (!channels(item->getStringAttribute("inputs"), strip.inputs)
                    || !channels(item->getStringAttribute("outputs"), strip.outputs)) return false;
                if ((!strip.allInputs && strip.inputs.empty()) || (!strip.allOutputs && strip.outputs.empty())) return false;
                strip.stereo = !strip.allInputs && strip.inputs.size() == 2;
                loadedStrips.push_back(std::move(strip));
                continue;
            }
            PluginInstanceRecord record;
            record.id = item->getStringAttribute("id");
            const auto* description = item->getChildByName("PLUGIN");
            if (!item->hasTagName("INSTANCE") || record.id.length() != 32
                || !record.id.containsOnly("0123456789abcdef") || juce::Uuid(record.id).isNull()
                || !ids.insert(record.id).second || !description || !record.description.loadFromXml(*description)) return false;
            record.originalIdentity = item->getStringAttribute("identity");
            record.identityResolved = item->getBoolAttribute("identityResolved", false);
            if (record.originalIdentity != knownPluginId(record.description)) return false;
            record.bypassed = item->getBoolAttribute("bypassed");
            record.customName = item->getStringAttribute("customName");
            record.error = item->getStringAttribute("error");
            record.stateCaptureAllowed = item->getBoolAttribute("stateCaptureAllowed", true);
            record.stripId = item->getStringAttribute("strip").toLowerCase();
            record.editorOpen = item->getBoolAttribute("editorOpen");
            if (item->hasAttribute("editorX") && item->hasAttribute("editorY"))
            {
                const int x = item->getIntAttribute("editorX"), y = item->getIntAttribute("editorY");
                if (std::abs(x) > 32000 || std::abs(y) > 32000) return false;
                record.hasEditorPosition = true;
                record.editorX = x;
                record.editorY = y;
            }
            if (item->hasAttribute("editorW") && item->hasAttribute("editorH"))
            {
                const int width = item->getIntAttribute("editorW"), height = item->getIntAttribute("editorH");
                if (width < 80 || height < 40 || width > 8000 || height > 8000) return false;
                record.hasEditorSize = true;
                record.editorW = width;
                record.editorH = height;
            }
            if (auto* state = item->getChildByName("STATE")) record.lastValidState = state->getAllSubText();
            if (auto* state = item->getChildByName("RECOVERYSTATE")) record.recoveryState = state->getAllSubText();
            if (auto* legacy = item->getChildByName("LEGACY")) record.legacyDescription = legacy->getAllSubText();
            validateState(record);
            if (record.error.isNotEmpty()) record.loading = item->getStringAttribute("loading") == "missing" ? "missing" : "failed";
            loaded.push_back(std::move(record));
        }
        if (version == 1) loadedStrips = { defaultStrip() };
        if (loadedStrips.empty() || (int) loadedStrips.size() > maximumStrips) return false;
        for (auto& record : loaded)
        {
            if (version == 1) record.stripId = loadedStrips.front().id;
            if (std::none_of(loadedStrips.begin(), loadedStrips.end(), [&](const auto& strip) { return strip.id == record.stripId; })) return false;
        }
        strips = std::move(loadedStrips);
        records = std::move(loaded);
        masterGainDb = clampGainDb(static_cast<float>(root.getDoubleAttribute("masterGainDb")));
        recoveryError = root.getStringAttribute("recoveryError");
        return true;
    }

    int indexOf(const PluginInstanceId& id) const
    {
        for (size_t i = 0; i < records.size(); ++i) if (records[i].id == id) return static_cast<int>(i);
        return -1;
    }

private:
    static bool validStripId(const juce::String& id)
    {
        return id.length() == 32 && id.containsOnly("0123456789abcdef");
    }
    static void validateState(PluginInstanceRecord& record)
    {
        juce::MemoryBlock binary;
        if (record.lastValidState.isNotEmpty() && !binary.fromBase64Encoding(record.lastValidState))
        {
            record.recoveryState = record.lastValidState;
            record.lastValidState.clear();
            record.error = "Invalid saved plugin state preserved for recovery";
            record.stateCaptureAllowed = false;
        }
    }
};
}
