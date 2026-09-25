#include "PluginInstances.h"
#include "KnownPluginNames.h"
#include "ScenarioRunner.h"

using namespace juce;
using namespace lightHostModern;
using scenarios::require;

static PluginDescription plugin(int uid = 42)
{
    PluginDescription description;
    description.name = "Effect";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = "C:\\Plugins\\Effect.vst3";
    description.uniqueId = uid;
    description.deprecatedUid = 100;
    return description;
}

int main(int argc, char** argv)
{
    const bool legacyLoadedFixture = argc == 5 && String(argv[1]) == "--write-legacy-loaded-ui-fixture";
    const bool loadedFixture = legacyLoadedFixture || (argc == 5 && String(argv[1]) == "--write-loaded-ui-fixture");
    if ((argc == 4 && String(argv[1]) == "--write-ui-fixture") || loadedFixture)
    {
        const int count = String(argv[3]).getIntValue();
        if (count < 1 || count > 1000) return 2;
        const File destination(String::fromUTF8(argv[2]));
        PluginDescription simulatedProcessor;
        if (loadedFixture)
        {
            const auto cache = XmlDocument::parse(File(String::fromUTF8(argv[4])));
            const auto* entry = cache ? cache->getChildByName("ENTRY") : nullptr;
            const auto* description = entry ? entry->getChildByName("PLUGIN") : nullptr;
            if (!description || entry->getStringAttribute("verifiedMetadata") != "verified"
                || !simulatedProcessor.loadFromXml(*description)
                || simulatedProcessor.name != "LightHostModern Scenario Fixture" || simulatedProcessor.manufacturerName != "LightHostModern Tests"
                || !File(simulatedProcessor.fileOrIdentifier).existsAsFile()) return 3;
        }
        XmlElement properties("PROPERTIES");
        auto* known = properties.createNewChildElement("VALUE");
        known->setAttribute("name", "pluginList");
        auto* database = known->createNewChildElement("KNOWNPLUGINS");
        XmlElement* legacy = nullptr;
        if (legacyLoadedFixture)
        {
            auto* active = properties.createNewChildElement("VALUE");
            active->setAttribute("name", "pluginListActive");
            legacy = active->createNewChildElement("KNOWNPLUGINS");
        }
        PluginInstances instances;
        for (int index = 0; index < count; ++index)
        {
            PluginInstanceRecord record;
            record.id = Uuid().toString();
            record.description = plugin(index + 1);
            record.description.name = "Test Effect " + String(index + 1).paddedLeft('0', 4);
            record.description.manufacturerName = index % 2 ? "Factory A" : String::fromUTF8("Fábrica B");
            record.description.fileOrIdentifier = destination.getParentDirectory().getChildFile("Simulated")
                .getChildFile(String(index + 1) + ".vst3").getFullPathName();
            database->addChildElement((loadedFixture && index == 0 ? simulatedProcessor : record.description).createXml().release());
            if (loadedFixture)
            {
                record.customName = record.description.name;
                record.description = simulatedProcessor;
            }
            record.originalIdentity = knownPluginId(record.description);
            record.loading = loadedFixture ? "unloaded" : "missing";
            // A VST3 host state wraps the processor state. Let the real host
            // capture that wrapper after loading each independent instance.
            if (!loadedFixture) record.lastValidState = MemoryBlock(&index, sizeof(index)).toBase64Encoding();
            if (legacy)
            {
                auto description = simulatedProcessor;
                description.deprecatedUid += index;
                legacy->addChildElement(description.createXml().release());
                auto* id = properties.createNewChildElement("VALUE");
                id->setAttribute("name", PluginInstances::legacyKey("instance-id", description));
                id->setAttribute("val", record.id);
                auto* order = properties.createNewChildElement("VALUE");
                order->setAttribute("name", PluginInstances::legacyKey("order", description));
                order->setAttribute("val", index + 1);
            }
            instances.records.push_back(std::move(record));
        }
        if (!legacy)
        {
            auto* session = properties.createNewChildElement("VALUE");
            session->setAttribute("name", "pluginInstancesV1");
            session->addChildElement(instances.serialize(1).release());
        }
        return properties.writeTo(destination) ? 0 : 1;
    }
    if (argc == 3 && String(argv[1]) == "--write-legacy-fixture")
    {
        auto original = plugin();
        auto duplicate = original;
        ++duplicate.deprecatedUid;
        XmlElement properties("PROPERTIES");
        auto* known = properties.createNewChildElement("VALUE");
        known->setAttribute("name", "pluginList");
        known->createNewChildElement("KNOWNPLUGINS")->addChildElement(original.createXml().release());
        auto* active = properties.createNewChildElement("VALUE");
        active->setAttribute("name", "pluginListActive");
        auto* legacy = active->createNewChildElement("KNOWNPLUGINS");
        legacy->addChildElement(original.createXml().release());
        legacy->addChildElement(duplicate.createXml().release());
        const auto value = [&](const String& key, const String& text) {
            auto* item = properties.createNewChildElement("VALUE");
            item->setAttribute("name", key); item->setAttribute("val", text);
        };
        value(PluginInstances::legacyKey("instance-id", original), "11111111111111111111111111111111");
        value(PluginInstances::legacyKey("instance-id", duplicate), "22222222222222222222222222222222");
        value(PluginInstances::legacyKey("order", original), "2");
        value(PluginInstances::legacyKey("order", duplicate), "1");
        value(PluginInstances::legacyKey("state", original), MemoryBlock("one", 3).toBase64Encoding());
        value(PluginInstances::legacyKey("state", duplicate), MemoryBlock("two", 3).toBase64Encoding());
        return properties.writeTo(File(String::fromUTF8(argv[2]))) ? 0 : 1;
    }
    scenarios::Runner runner;
    runner.run("exact legacy keys precede normalization and preserve distinct duplicate states", [] {
        const auto original = plugin();
        auto duplicate = original;
        ++duplicate.deprecatedUid;
        PropertySet settings;
        const auto firstId = Uuid().toString(), secondId = Uuid().toString();
        const auto firstState = MemoryBlock("one", 3).toBase64Encoding(), secondState = MemoryBlock("two", 3).toBase64Encoding();
        settings.setValue(PluginInstances::legacyKey("instance-id", original), firstId);
        settings.setValue(PluginInstances::legacyKey("instance-id", duplicate), secondId);
        settings.setValue(PluginInstances::legacyKey("state", original), firstState);
        settings.setValue(PluginInstances::legacyKey("state", duplicate), secondState);
        settings.setValue(PluginInstances::legacyKey("order", original), 2);
        settings.setValue(PluginInstances::legacyKey("order", duplicate), 1);
        settings.setValue(PluginInstances::legacyKey("bypass", duplicate), 1);
        XmlElement legacy("KNOWNPLUGINS");
        legacy.addChildElement(original.createXml().release());
        legacy.addChildElement(duplicate.createXml().release());
        PluginInstances instances;
        instances.migrate(legacy, settings, {original});
        require(instances.records.size() == 2, "duplicate was collapsed");
        const auto& first = instances.records[0];
        const auto& second = instances.records[1];
        require(first.id == secondId && second.id == firstId, "legacy order or UUID changed");
        require(first.lastValidState == secondState && second.lastValidState == firstState && first.bypassed, "states were reassigned");
        require(first.description.deprecatedUid == original.deprecatedUid && first.originalIdentity == second.originalIdentity, "original identity was not restored");
        require(first.legacyDescription != second.legacyDescription, "exact recovery material lost");
        PluginInstances reloaded;
        require(reloaded.deserialize(*instances.serialize(9)) && reloaded.records.size() == 2, "adapter lost duplicates");
        require(reloaded.records[0].id == first.id && reloaded.records[0].lastValidState == first.lastValidState, "adapter changed state");
        instances.migrate(legacy, settings, {original});
        require(instances.records.size() == 2 && instances.records[0].id == secondId, "migration is not idempotent");
    });
    runner.run("ambiguous identities and states remain recoverable", [] {
        const auto original = plugin(0);
        auto unknown = original;
        ++unknown.deprecatedUid;
        PropertySet settings;
        const auto exact = PluginInstances::legacyKey("state", unknown);
        settings.setValue(exact, "not valid base64 !");
        XmlElement legacy("KNOWNPLUGINS");
        legacy.addChildElement(unknown.createXml().release());
        PluginInstances instances;
        instances.migrate(legacy, settings, {original});
        const auto& record = instances.records.front();
        require(!record.identityResolved && record.description.deprecatedUid == unknown.deprecatedUid, "unknown identity guessed");
        require(record.recoveryState == settings.getValue(exact) && !record.stateCaptureAllowed, "corrupt original discarded");
        require(settings.containsKey(exact), "legacy state was deleted");
    });
    runner.run("identity is independent of metadata and installed sort order", [] {
        const auto original = plugin();
        auto changed = original;
        changed.name = "New display name";
        changed.version = "9";
        changed.deprecatedUid++;
        require(knownPluginId(original) == knownPluginId(changed), "metadata changed class identity");
        changed.uniqueId++;
        require(knownPluginId(original) != knownPluginId(changed), "different class got same identity");
        changed = original;
        changed.pluginFormatName = "VST";
        require(knownPluginId(original) != knownPluginId(changed), "formats got same identity");
    });
    runner.run("invalid adapter load is transactional and rejects duplicate UUIDs", [] {
        PluginInstances instances;
        PluginInstanceRecord record;
        record.id = Uuid().toString();
        record.description = plugin();
        record.originalIdentity = knownPluginId(record.description);
        record.customName = String::fromUTF8("  Voz • 日本語  ");
        instances.records.push_back(record);
        auto xml = instances.serialize();
        xml->addChildElement(new XmlElement(*xml->getFirstChildElement()));
        require(!instances.deserialize(*xml) && instances.records.size() == 1, "duplicate UUID accepted or existing records lost");
        xml = instances.serialize();
        xml->setAttribute("version", 99);
        require(!instances.deserialize(*xml) && instances.records.front().id == record.id, "unsupported data overwrote session");
    });
    runner.run("custom names count Unicode code points and reject multiline input", [] {
        String normalized;
        require(normalizeInstanceName(String::fromUTF8("  Voz • 日本語 🎵  "), normalized)
            && normalized == String::fromUTF8("Voz • 日本語 🎵"), "Unicode name or trim changed");
        require(normalizeInstanceName("   ", normalized) && normalized.isEmpty(), "Empty name must restore original");
        require(normalizeInstanceName(String::repeatedString(String::fromUTF8("🎵"), 128), normalized), "128 supplementary characters rejected");
        require(!normalizeInstanceName(String::repeatedString("x", 129), normalized), "Overlong name accepted");
        for (const auto& bad : {String("a\nb"), String("a\rb"), String::fromUTF8("a\xe2\x80\xa8" "b"), String("a\tb")})
            require(!normalizeInstanceName(bad, normalized), "Multiline/control name accepted");
    });
    runner.run("catalogue aliases survive persistence and seed independent instances", [] {
        PropertySet settings;
        const auto original = plugin();
        const auto identity = knownPluginId(original);
        const auto alias = String::fromUTF8("  Voz \xe2\x80\xa2 \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e  ");
        require(setKnownPluginCustomName(settings, original, alias), "valid catalogue alias rejected");
        PropertySet reopened;
        reopened.restoreFromXml(*settings.createXml("SETTINGS"));
        auto updatedMetadata = original; updatedMetadata.version = "2"; updatedMetadata.name = "Vendor renamed effect";
        require(knownPluginCustomName(reopened, updatedMetadata) == alias.trim(), "rescan or reload lost alias");
        auto first = newKnownPluginInstance(reopened, original), second = newKnownPluginInstance(reopened, original);
        require(first.id != second.id && first.displayName() == alias.trim() && first.originalIdentity == identity,
                "add did not inherit alias or changed identity/UUID");
        require(first.description.name == original.name && second.description.name == original.name, "original description was renamed");
        require(!setKnownPluginCustomName(reopened, original, "bad\nname") && knownPluginCustomName(reopened, original) == alias.trim(),
                "invalid rename damaged the existing alias");
        require(knownPluginCustomName(reopened, plugin(999)).isEmpty(), "alias leaked to another class");
        require(setKnownPluginCustomName(reopened, original, original.name) && knownPluginCustomName(reopened, original).isEmpty(),
                "original name was retained as a custom alias");
        require(first.displayName() == alias.trim() && newKnownPluginInstance(reopened, original).displayName() == original.name,
                "catalogue restore changed a running instance or affected new additions");
    });
    runner.run("version 1 opens as Main and version 2 keeps strips and editors", [] {
        PluginInstances instances;
        PluginInstanceRecord record;
        record.id = "11111111111111111111111111111111";
        record.description = plugin();
        record.originalIdentity = knownPluginId(record.description);
        record.lastValidState = MemoryBlock("state", 5).toBase64Encoding();
        instances.records.push_back(record);
        auto legacy = instances.serialize();
        legacy->setAttribute("version", 1);
        Array<XmlElement*> strips;
        for (auto* child : legacy->getChildIterator())
            if (child->hasTagName("STRIP")) strips.add(child);
        for (auto* child : strips) legacy->removeChildElement(child, true);
        PluginInstances migrated;
        migrated.records.push_back(record);
        require(migrated.deserialize(*legacy), "v1 rejected");
        require(migrated.strips.size() == 1 && migrated.strips[0].name == "Main" && migrated.strips[0].id == defaultStripId()
            && migrated.strips[0].allInputs && migrated.strips[0].allOutputs && migrated.strips[0].gainDb == 0.0f, "v1 strip");
        require(migrated.records[0].id == record.id && migrated.records[0].stripId == defaultStripId(), "v1 record");
        instances.ensureStrips();
        instances.strips[0].name = "Mic";
        instances.strips[0].allInputs = false;
        instances.strips[0].inputs = {1, 3};
        instances.strips[0].gainDb = 100.0f;
        instances.masterGainDb = -3.0f;
        instances.records[0].stripId = instances.strips[0].id;
        instances.records[0].editorOpen = true;
        instances.records[0].hasEditorPosition = true;
        instances.records[0].editorX = 40;
        instances.records[0].editorY = -12;
        instances.records[0].hasEditorSize = true;
        instances.records[0].editorW = 640;
        instances.records[0].editorH = 480;
        ChainStrip extra;
        extra.id = "abcdefabcdefabcdefabcdefabcdefab";
        extra.name = "Guitar";
        extra.allOutputs = false;
        extra.outputs = {0, 1};
        instances.strips.push_back(extra);
        PluginInstances round;
        require(round.deserialize(*instances.serialize()) && round.strips.size() == 2, "v2 round trip");
        require(round.strips[0].name == "Mic" && round.strips[0].gainDb == 12.0f && round.strips[0].inputs.size() == 2, "routing and clamp");
        require(round.masterGainDb == -3.0f && round.records[0].editorOpen && round.records[0].editorX == 40 && round.records[0].editorY == -12
            && round.records[0].hasEditorSize && round.records[0].editorW == 640 && round.records[0].editorH == 480, "master and editor");
        require(round.records[0].stripId == round.strips[0].id, "strip link");
        auto missingSize = instances.serialize();
        missingSize->getChildByName("INSTANCE")->removeAttribute("editorW");
        missingSize->getChildByName("INSTANCE")->removeAttribute("editorH");
        PluginInstances unsized;
        require(unsized.deserialize(*missingSize) && !unsized.records[0].hasEditorSize, "missing editor size");
        instances.strips[0].pan = -0.5f;
        instances.strips[1].pan = 2.0f;
        PluginInstances panned;
        require(panned.deserialize(*instances.serialize()), "pan round trip");
        require(std::abs(panned.strips[0].pan + 0.5f) < 0.0001f && panned.strips[1].pan == 1.0f, "pan restored and clamped");
        instances.strips[0].color = 4;
        instances.strips[0].group = "Drums";
        instances.strips[1].color = 99;
        instances.strips[0].colour = "ff9f43";
        PluginInstances colored;
        require(colored.deserialize(*instances.serialize()), "color round trip");
        require(colored.strips[0].color == 4 && colored.strips[0].colour == "ff9f43" && colored.strips[0].group == "Drums" && colored.strips[1].color == 8, "color clamped and group kept");
        auto legacyColour = instances.serialize();
        legacyColour->getChildByName("STRIP")->removeAttribute("colour");
        PluginInstances indexed;
        require(indexed.deserialize(*legacyColour) && indexed.strips[0].color == 4 && indexed.strips[0].colour.isEmpty(), "old swatch");
        auto missingColor = instances.serialize();
        missingColor->getChildByName("STRIP")->removeAttribute("color");
        missingColor->getChildByName("STRIP")->removeAttribute("group");
        require(colored.deserialize(*missingColor) && colored.strips[0].color == 0 && colored.strips[0].group.isEmpty(), "missing color and group");
        auto missingPan = instances.serialize();
        missingPan->getChildByName("STRIP")->removeAttribute("pan");
        require(panned.deserialize(*missingPan) && panned.strips[0].pan == 0.0f, "missing pan stays centered");
        auto broken = instances.serialize();
        broken->getChildByName("INSTANCE")->setAttribute("strip", "0123456789abcdef0123456789abcdef");
        require(!round.deserialize(*broken) && round.records[0].stripId == instances.strips[0].id, "unknown strip");
        broken = instances.serialize();
        broken->getChildByName("STRIP")->setAttribute("id", extra.id);
        require(!round.deserialize(*broken) && round.strips.size() == 2, "duplicate strip id");
        broken = instances.serialize();
        for (int index = 0; index < 190; ++index)
        {
            auto* strip = broken->createNewChildElement("STRIP");
            strip->setAttribute("id", String::toHexString(index + 3).paddedLeft('0', 32));
            strip->setAttribute("name", "S" + String(index));
        }
        require(round.deserialize(*broken) && round.strips.size() == 192, "192 strips");
        broken = instances.serialize();
        for (int index = 0; index < 191; ++index)
        {
            auto* strip = broken->createNewChildElement("STRIP");
            strip->setAttribute("id", String::toHexString(index + 3).paddedLeft('0', 32));
            strip->setAttribute("name", "S" + String(index));
        }
        require(!round.deserialize(*broken) && round.strips.size() == 192, "193 strips");
        instances.strips[0].stereo = true;
        instances.strips[0].allInputs = false;
        instances.strips[0].inputs = { 1, 12 };
        PluginInstances paired;
        require(paired.deserialize(*instances.serialize()) && paired.strips[0].stereo
            && paired.strips[0].inputs.size() == 2 && paired.strips[0].inputs[0] == 1 && paired.strips[0].inputs[1] == 12, "stereo pair");
        broken = instances.serialize();
        broken->setAttribute("version", 3);
        require(!round.deserialize(*broken) && round.records[0].id == record.id, "version 3");
    });
    return runner.result();
}
