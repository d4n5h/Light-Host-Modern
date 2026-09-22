#include "PreferenceMigration.h"
#include <iostream>
#include <stdexcept>

static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main()
{
    using namespace juce;
    const auto root = File::getSpecialLocation(File::tempDirectory).getNonexistentChildFile("lhm-migration-test", "", false);
    struct Cleanup { File root; ~Cleanup() { root.deleteRecursively(); } } cleanup{root};
    try {
        root.createDirectory();
        const auto old = root.getChildFile("old/Light Host Modern.settings"), current = root.getChildFile("new/LightHostModern.settings");
        old.getParentDirectory().createDirectory(); current.getParentDirectory().createDirectory();
        old.replaceWithText("preferences-byte-for-byte");
        for (const auto* suffix : {".session.json", ".session.json.bak", ".session.json.pending", ".session.json.backup-pending", ".session.json.damaged-123", ".pre-session.bak"})
            old.getSiblingFile(old.getFileName() + suffix).replaceWithText(String("session ") + suffix);
        require(lightHostModern::migratePreferences(old, current).wasOk(), "Migration failed");
        require(current.loadFileAsString() == old.loadFileAsString(), "Preferences changed");
        require(current.getSiblingFile(current.getFileName() + ".session.json.pending").loadFileAsString() == "session .session.json.pending", "Pending recovery lost");
        require(old.existsAsFile(), "Legacy data removed");
        current.replaceWithText("newer-canonical-even-if-invalid");
        require(lightHostModern::migratePreferences(old, current).wasOk() && current.loadFileAsString() == "newer-canonical-even-if-invalid", "Canonical data overwritten");
        // Simulate interruption after the durable manifest, before all files publish.
        const auto stage = current.getSiblingFile(current.getFileName() + ".identity-migration");
        current.replaceWithText(old.loadFileAsString());
        require(stage.getChildFile("completed.json").moveFileTo(stage.getChildFile("manifest.json")), "Fixture manifest failed");
        const auto recovery = current.getSiblingFile(current.getFileName() + ".session.json.bak");
        require(recovery.deleteFile(), "Fixture delete failed");
        recovery.getSiblingFile(recovery.getFileName() + ".copy-pending").replaceWithText("interrupted partial copy");
        require(lightHostModern::migratePreferences(old, current).wasOk() && recovery.loadFileAsString() == "session .session.json.bak", "Interrupted publication not resumed");
        for (const auto& file : current.getParentDirectory().findChildFiles(File::findFiles, false, current.getFileName() + "*"))
            require(file.deleteFile(), "Fixture reset failed");
        require(lightHostModern::migratePreferences(old, current).wasOk() && !current.existsAsFile(), "Completed migration repeated after reset");
        std::cout << "Migration, recovery family, canonical precedence and interrupted copy passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
