#include "SessionStore.h"
#include "PluginStateCapture.h"
#include "ScenarioRunner.h"
#include <Windows.h>
#include <map>
#include <atomic>

using namespace juce;
using namespace lightHostModern;
using scenarios::require;
using Slot = SessionStorage::Slot;

class MemoryStorage final : public SessionStorage
{
public:
    Read read(Slot slot) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = files.find(slot);
        return found == files.end() ? Read{} : Read{true, found->second, {}};
    }
    String writeFlushed(Slot slot, const std::string& bytes) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++writes;
        if (checkpoint()) return "Simulated file creation failure";
        files[slot] = bytes.substr(0, bytes.size() / 2);
        if (checkpoint()) return "Simulated disk full during write";
        files[slot] = bytes;
        if (checkpoint()) return "Simulated flush failure";
        return {};
    }
    String replace(Slot source, Slot target) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (checkpoint()) return "Simulated replacement failure";
        files[target] = files.at(source); files.erase(source);
        if (checkpoint()) return "Simulated interruption after replacement";
        return {};
    }
    String preserve(Slot slot) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        archives.push_back(files.at(slot));
        return {};
    }
    void put(Slot slot, const std::string& bytes) { std::lock_guard<std::mutex> lock(mutex); files[slot] = bytes; }
    void failAt(int step) { std::lock_guard<std::mutex> lock(mutex); failureStep = step; steps = 0; }
    std::atomic<unsigned> writes{0};
    std::vector<std::string> archives;
private:
    bool checkpoint() { return ++steps >= failureStep && failureStep > 0; }
    int steps = 0, failureStep = -1;
    std::mutex mutex;
    std::map<Slot, std::string> files;
};

static SessionDocument fixture(uint64 revision = 1)
{
    SessionDocument result;
    result.revision = revision; result.migrationId = "fixture";
    PluginInstanceRecord record;
    record.id = "11111111111111111111111111111111";
    record.description.name = "Missing effect";
    record.description.manufacturerName = "Factory";
    record.description.pluginFormatName = "VST3";
    record.description.fileOrIdentifier = "C:\\Missing\\Effect.vst3";
    record.description.uniqueId = 42;
    record.description.deprecatedUid = 100;
    record.originalIdentity = knownPluginId(record.description);
    record.loading = "missing"; record.error = "Plugin is absent";
    record.lastValidState = MemoryBlock("first-state", 11).toBase64Encoding();
    result.instances.records.push_back(record);
    record.id = "22222222222222222222222222222222";
    record.bypassed = true; record.customName = String::fromUTF8("Voz 日本語");
    record.lastValidState = MemoryBlock("second-state", 12).toBase64Encoding();
    result.instances.records.push_back(record);
    return result;
}
static void assertRecords(const SessionDocument& saved, const SessionDocument& expected)
{
    require(saved.instances.records.size() == expected.instances.records.size(), "Instance count changed");
    for (size_t i = 0; i < expected.instances.records.size(); ++i)
    {
        const auto& a = saved.instances.records[i]; const auto& b = expected.instances.records[i];
        require(a.id == b.id && a.originalIdentity == b.originalIdentity && a.lastValidState == b.lastValidState
            && a.bypassed == b.bypassed && a.customName == b.customName, "UUID, identity, order or distinct state changed");
    }
}

int main(int argc, char** argv)
{
    if (argc == 3 && String(argv[1]) == "--mark-session-failed")
    {
        DiskSessionStorage storage(File(String::fromUTF8(argv[2])));
        auto recovered = SessionStore::recover(storage);
        if (!recovered.document || recovered.document->instances.records.empty()) return 2;
        auto& document = *recovered.document;
        ++document.revision;
        document.instances.records.front().error = "Simulated previous load failure";
        document.instances.records.front().loading = "failed";
        return SessionStore::commit(storage, SessionCodec::encode(document)).isEmpty() ? 0 : 3;
    }
    scenarios::Runner runner;
    runner.run("Versioned envelope retains duplicates, Unicode names and unavailable plugins", [] {
        const auto original = fixture();
        const auto encoded = SessionCodec::encode(original);
        String error; auto restored = SessionCodec::decode(encoded.bytes, error);
        require(restored.has_value(), error.toRawUTF8()); assertRecords(*restored, original);
        require(restored->instances.strips.size() == 1 && restored->instances.strips[0].name == "Main"
            && restored->instances.records[0].stripId == restored->instances.strips[0].id, "v2 session strip");
        require(static_cast<int>(JSON::parse(String(encoded.bytes))["formatVersion"]) == 1, "envelope stays version 1");
        auto changed = original; changed.revision = 100;
        require(SessionCodec::encode(changed).digest == encoded.digest, "Revision defeated content deduplication");
        auto corrupt = JSON::parse(String(encoded.bytes)); corrupt.getDynamicObject()->setProperty("sessionXml", "<bad/>");
        require(!SessionCodec::decode(JSON::toString(corrupt).toStdString(), error), "Corrupt content checksum accepted");
        corrupt = JSON::parse(String(encoded.bytes)); corrupt.getDynamicObject()->setProperty("formatVersion", 999);
        require(!SessionCodec::decode(JSON::toString(corrupt).toStdString(), error), "Unknown storage version accepted");
    });
    runner.run("Empty requires explicit intent and bad existing files stay read-only", [] {
        auto empty = fixture(); empty.instances.records.clear();
        bool rejected = false; try { SessionCodec::encode(empty); } catch (...) { rejected = true; }
        require(rejected, "Unloaded chain was serialized as intentionally empty");
        empty.intentionalEmpty = true; String error;
        require(SessionCodec::decode(SessionCodec::encode(empty).bytes, error).has_value(), "Explicitly empty chain was rejected");
        auto disk = std::make_shared<MemoryStorage>(); disk->put(Slot::primary, "corrupt original");
        const auto recovered = SessionStore::recover(*disk);
        require(recovered.found && !recovered.document, "Corrupt session was mistaken for no session");
        SessionStore store(disk, recovered, std::chrono::milliseconds(0));
        store.submit(empty.instances, true, "should-not-write");
        require(!store.flush(std::chrono::seconds(1)), "Unrecoverable session accepted replacement");
        require(disk->read(Slot::primary).bytes == "corrupt original", "Original corrupted material was overwritten");
    });
    runner.run("Every write, flush and replacement interruption recovers a complete revision", [] {
        const auto original = fixture(); auto next = fixture(2);
        std::swap(next.instances.records[0], next.instances.records[1]); next.instances.records[1].customName = "Changed";
        for (int step = 1; step <= 10; ++step)
        {
            MemoryStorage disk; disk.put(Slot::primary, SessionCodec::encode(original).bytes); disk.failAt(step);
            require(SessionStore::commit(disk, SessionCodec::encode(next)).isNotEmpty(), "Fault injection did not interrupt the commit");
            const auto recovered = SessionStore::recover(disk);
            require(recovered.document.has_value(), "Interrupted commit lost all valid files");
            assertRecords(*recovered.document, recovered.document->revision == 2 ? next : original);
            disk.failAt(-1);
            require(SessionStore::commit(disk, SessionCodec::encode(next)).isEmpty(), "Retry could not repair interrupted commit");
            assertRecords(*SessionStore::recover(disk).document, next);
        }
    });
    runner.run("Completed first-save pending file survives a second interrupted save", [] {
        auto first = fixture(), next = fixture(2);
        MemoryStorage disk; disk.put(Slot::pending, SessionCodec::encode(first).bytes);
        disk.failAt(7); // backup is durable; the new pending file is incomplete.
        require(SessionStore::commit(disk, SessionCodec::encode(next)).isNotEmpty(), "Expected interrupted write");
        const auto recovered = SessionStore::recover(disk);
        require(recovered.document.has_value(), "Only valid pending file was overwritten without backup");
        assertRecords(*recovered.document, first);
    });
    runner.run("Disk-full keeps revision pending and retries a failed first save", [] {
        auto disk = std::make_shared<MemoryStorage>(); disk->failAt(2);
        SessionStore store(disk, {}, std::chrono::milliseconds(0));
        auto source = fixture(); store.submit(source.instances, false, source.migrationId);
        require(!store.flush(std::chrono::seconds(2)), "Disk-full was reported as saved");
        require(store.status().pending && store.status().savedRevision == 0 && store.status().error.isNotEmpty(), "Failed revision lost pending status");
        disk->failAt(-1);
        require(store.flush(std::chrono::seconds(2)), "Failed first save could not be retried");
        require(!store.status().pending && store.status().error.isEmpty(), "Successful retry did not clear error");
        assertRecords(*SessionStore::recover(*disk).document, source);
    });
    runner.run("One-second debounce, immutable submission and content deduplication", [] {
        auto disk = std::make_shared<MemoryStorage>();
        std::atomic<int64_t> now{0};
        SessionStore store(disk, {}, std::chrono::seconds(1), [&] { return SessionStore::Clock::time_point(std::chrono::milliseconds(now.load())); });
        auto source = fixture(); store.submit(source.instances, false, source.migrationId);
        now = 999; store.clockChanged(); std::this_thread::sleep_for(std::chrono::milliseconds(30));
        require(disk->writes == 0, "Session was written before the one-second debounce");
        source.instances.records[0].customName = "Latest";
        store.submit(source.instances, false, source.migrationId);
        source.instances.records[0].customName = "Unsubmitted change";
        now = 1998; store.clockChanged(); std::this_thread::sleep_for(std::chrono::milliseconds(30));
        require(disk->writes == 0, "New snapshot did not reset debounce");
        now = 1999; store.clockChanged();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (store.status().savedRevision < 2 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
        require(store.status().savedRevision == 2, "Worker did not commit the debounced revision");
        auto recovered = SessionStore::recover(*disk);
        require(recovered.document->instances.records[0].customName == "Latest", "Worker observed a mutable controller snapshot");
        const auto writes = disk->writes.load();
        store.submit(recovered.document->instances, false, recovered.document->migrationId);
        require(store.flush(std::chrono::seconds(2)) && disk->writes == writes, "Identical content was written again");
    });
    runner.run("Bad primary is archived and latest valid backup is recovered", [] {
        MemoryStorage disk; const auto source = fixture();
        disk.put(Slot::primary, "original invalid bytes"); disk.put(Slot::backup, SessionCodec::encode(source).bytes);
        const auto recovered = SessionStore::recover(disk);
        require(recovered.source == Slot::backup && recovered.warning.isNotEmpty(), "Recovery source and warning missing");
        require(SessionStore::commit(disk, SessionCodec::encode(source)).isEmpty(), "Could not repair primary");
        require(disk.archives.size() == 1 && disk.archives[0] == "original invalid bytes", "Damaged original was not retained exactly");
        assertRecords(*SessionStore::recover(disk).document, source);
    });
    runner.run("Legacy copy activation and repeated migration are idempotent", [] {
        MemoryStorage disk; disk.put(Slot::preferences, "exact original bytes");
        disk.failAt(2); String migration;
        require(SessionStore::backupLegacy(disk, migration).isNotEmpty(), "Legacy copy fault did not surface");
        require(!disk.read(Slot::legacyBackup).exists, "Partial legacy copy was activated");
        disk.failAt(-1);
        require(SessionStore::backupLegacy(disk, migration).isEmpty(), "Legacy copy retry failed");
        disk.put(Slot::preferences, "later unrelated preferences");
        String again; require(SessionStore::backupLegacy(disk, again).isEmpty() && again == migration, "Migration identity changed");
        require(disk.read(Slot::legacyBackup).bytes == "exact original bytes", "Legacy copy was overwritten");
        const auto source = fixture(); XmlElement legacy("KNOWNPLUGINS");
        auto first = source.instances.records[0].description, second = first; second.deprecatedUid += 1;
        legacy.addChildElement(first.createXml().release()); legacy.addChildElement(second.createXml().release());
        PropertySet propertiesA, propertiesB;
        for (auto* properties : {&propertiesA, &propertiesB})
        {
            properties->setValue(PluginInstances::legacyKey("state", first), source.instances.records[0].lastValidState);
            properties->setValue(PluginInstances::legacyKey("state", second), source.instances.records[1].lastValidState);
        }
        PluginInstances a, b; a.migrate(legacy, propertiesA, {first}, migration); b.migrate(legacy, propertiesB, {first}, migration);
        require(a.records.size() == 2 && a.records[0].id == b.records[0].id && a.records[1].id == b.records[1].id
            && a.records[0].id != a.records[1].id && a.records[0].lastValidState != a.records[1].lastValidState,
            "Repeated migration changed UUIDs or mixed duplicate state");
    });
    runner.run("Failed plugin capture retains the previous state", [] {
        auto record = fixture().instances.records[0]; const auto saved = record.lastValidState;
        require(!capturePluginState(record, [](MemoryBlock& bytes) { bytes.append("partial", 7); throw std::runtime_error("plugin failure"); }), "Capture failure was hidden");
        require(record.lastValidState == saved, "Failed capture erased previous state");
        require(capturePluginState(record, [](MemoryBlock& bytes) { bytes.append("valid", 5); }), "Valid capture failed");
        require(record.lastValidState != saved, "Valid capture was not adopted");
    });
    runner.run("Saved state never reaches a different loaded class or module", [] {
        const auto original = fixture().instances.records[0];
        for (int mismatch = 0; mismatch < 4; ++mismatch)
        {
            auto record = original;
            auto actual = record.description;
            if (mismatch == 0) ++actual.uniqueId;
            if (mismatch == 1) actual.pluginFormatName = "VST";
            if (mismatch == 2) actual.fileOrIdentifier = "C:\\Other\\Effect.vst3";
            if (mismatch == 3) record.description.uniqueId = record.description.deprecatedUid = actual.uniqueId = actual.deprecatedUid = 0;
            bool called = false, rejected = false;
            try { restorePluginState(record, actual, [&](const void*, int) { called = true; }); }
            catch (const std::exception& error) { rejected = String(error.what()) == "plugin_identity_mismatch"; }
            require(rejected && !called && record.lastValidState == original.lastValidState && record.id == original.id,
                "State was applied by approximation or original data was lost");
        }
    });
    runner.run("Exact legacy class IDs and VST3 bundle association preserve restoration", [] {
        auto record = fixture().instances.records[0];
        record.description.uniqueId = 0; record.description.deprecatedUid = 42;
        auto actual = record.description; actual.uniqueId = 42;
        actual.fileOrIdentifier = "C:\\Missing\\Effect.vst3\\Contents\\x86_64-win\\Effect.vst3";
        actual.name = "Updated display name"; actual.version = "new version";
        int calls = 0;
        restorePluginState(record, actual, [&](const void* data, int size) {
            require(size == 11 && std::memcmp(data, "first-state", 11) == 0, "Restored bytes changed"); ++calls;
        });
        require(calls == 1 && record.description.uniqueId == 0, "Legacy description changed or exact state was omitted");
        bool rejected = false;
        record.description.deprecatedUid = 43;
        try { restorePluginState(record, actual, [&](const void*, int) { ++calls; }); }
        catch (const std::exception&) { rejected = true; }
        require(rejected && calls == 1, "A synthetic legacy UID was guessed");
    });
    runner.run("Restoration failures retain the last valid bytes for recovery", [] {
        auto record = fixture().instances.records[0]; const auto original = record.lastValidState;
        bool failed = false;
        try { restorePluginState(record, record.description, [](const void*, int) { throw std::runtime_error("plugin error"); }); }
        catch (const std::exception& error) { failed = String(error.what()) == "plugin_state_restore_failed"; }
        require(failed && record.lastValidState == original && record.recoveryState == original && !record.stateCaptureAllowed,
            "Failed restoration lost its recovery data or allowed capture to replace it");
    });
    runner.run("Real NTFS flushed replacement, backup and locked-primary recovery", [] {
        const auto directory = File::getCurrentWorkingDirectory().getChildFile("session-test-" + Uuid().toString());
        require(directory.createDirectory().wasOk(), "Could not create session test directory");
        DiskSessionStorage disk(directory.getChildFile(String::fromUTF8("Sessão.settings")));
        auto source = fixture(); require(SessionStore::commit(disk, SessionCodec::encode(source)).isEmpty(), "Initial disk write failed");
        source.revision = 2; source.instances.records[1].customName = "Disk revision 2";
        require(SessionStore::commit(disk, SessionCodec::encode(source)).isEmpty(), "Atomic replacement failed");
        String error; require(SessionCodec::decode(disk.read(Slot::backup).bytes, error)->revision == 1, "Backup did not retain previous version");
        const auto handle = CreateFileW(disk.file(Slot::primary).getFullPathName().toWideCharPointer(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(handle != INVALID_HANDLE_VALUE, "Could not lock primary for replacement failure");
        source.revision = 3; source.instances.records[0].customName = "Pending revision 3";
        const auto failure = SessionStore::commit(disk, SessionCodec::encode(source));
        CloseHandle(handle);
        require(failure.isNotEmpty(), "Locked primary was unexpectedly replaced");
        const auto recovered = SessionStore::recover(disk);
        require(recovered.document && recovered.document->revision == 3, "Completed pending revision was not recovered");
        require(SessionStore::commit(disk, SessionCodec::encode(source)).isEmpty(), "Real disk recovery failed");
        assertRecords(*SessionStore::recover(disk).document, source);
        std::cout << "Session disk evidence: " << directory.getFullPathName() << '\n';
    });
    return runner.result();
}
