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
    return runner.result();
}
