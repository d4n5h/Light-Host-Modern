#include "TemplateStore.h"
#include "PluginInstances.h"
#include "ScenarioRunner.h"

using namespace juce;
using namespace lightHostModern;
using scenarios::require;

int main()
{
    scenarios::Runner runner;
    runner.run("template file keeps the chain and the channel mask", [] {
        const auto folder = File::getSpecialLocation(File::tempDirectory).getChildFile("light-host-template-test-" + Uuid().toString());
        folder.deleteRecursively();
        TemplateStore store(folder.getChildFile("prefs.xml"));
        PluginInstances instances;
        instances.ensureStrips();
        instances.strips[0].name = "Mic";
        instances.strips[0].gainDb = -3.0f;
        ChainStrip extra;
        extra.id = "abcdefabcdefabcdefabcdefabcdefab";
        extra.name = "Guitar";
        instances.strips.push_back(extra);
        TemplateSnapshot snapshot;
        snapshot.session = instances.serialize();
        snapshot.channels = std::make_unique<XmlElement>("CHANNELS");
        snapshot.channels->setAttribute("inputChannels", "11");
        snapshot.persistence = "last";
        String id;
        require(store.create("Sunday", snapshot, id).isEmpty() && id.isNotEmpty(), "create");
        TemplateSnapshot loaded;
        require(store.read(id, loaded).isEmpty(), "read");
        PluginInstances round;
        require(round.deserialize(*loaded.session), "session");
        require(round.strips.size() == 2 && round.strips[0].name == "Mic" && std::abs(round.strips[0].gainDb + 3.0f) < 0.001f, "strip");
        require(round.strips[1].name == "Guitar", "second strip");
        require(loaded.channels != nullptr && loaded.channels->getStringAttribute("inputChannels") == "11", "channels");
        require(loaded.persistence == "last", "persistence");
        require(store.create("Sunday", snapshot, id) == "template_name_taken", "duplicate name");
        store.root().getChildFile("junk.xml").replaceWithText("<ROOT/>");
        require(store.list().size() == 1, "bad file skipped");
        folder.deleteRecursively();
    });
    return runner.result();
}
