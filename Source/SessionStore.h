#pragma once
#include "PluginInstances.h"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace lightHostModern
{
struct SessionDocument
{
    PluginInstances instances;
    juce::uint64 revision = 0;
    juce::String migrationId;
    bool intentionalEmpty = false;
};

struct EncodedSession { std::string bytes; juce::String digest; };

class SessionCodec
{
public:
    static constexpr size_t maximumFileBytes = 256 * 1024 * 1024;
    static EncodedSession encode(const SessionDocument&);
    static std::optional<SessionDocument> decode(const std::string&, juce::String& error);
    static juce::String digest(const std::string&);
};

// Fixed slots keep writes inside this preferences/profile directory. Tests can
// substitute disk-full, partial writes, failed flushes and interrupted renames.
class SessionStorage
{
public:
    enum class Slot { primary, backup, pending, backupPending, preferences, legacyBackup, legacyBackupPending };
    struct Read { bool exists = false; std::string bytes; juce::String error; };
    virtual ~SessionStorage() = default;
    virtual Read read(Slot) = 0;
    virtual juce::String writeFlushed(Slot, const std::string&) = 0;
    virtual juce::String replace(Slot source, Slot destination) = 0;
    virtual juce::String preserve(Slot) = 0;
};

class DiskSessionStorage final : public SessionStorage
{
public:
    explicit DiskSessionStorage(const juce::File& preferencesFile);
    Read read(Slot) override;
    juce::String writeFlushed(Slot, const std::string&) override;
    juce::String replace(Slot source, Slot destination) override;
    juce::String preserve(Slot) override;
    const juce::File& file(Slot slot) const { return files[static_cast<size_t>(slot)]; }
private:
    std::array<juce::File, 7> files;
};

struct SessionRecovery
{
    bool found = false;
    std::optional<SessionDocument> document;
    SessionStorage::Slot source = SessionStorage::Slot::primary;
    juce::String warning;
    std::string bytes;
};

struct SessionSaveStatus
{
    juce::uint64 requestedRevision = 0, savedRevision = 0, changeSerial = 0, writes = 0;
    bool pending = false, saving = false;
    juce::String error;
};

class SessionStore
{
public:
    using Clock = std::chrono::steady_clock;
    explicit SessionStore(std::shared_ptr<SessionStorage>, const SessionRecovery&,
        std::chrono::milliseconds debounce = std::chrono::seconds(1),
        std::function<Clock::time_point()> clock = [] { return std::chrono::steady_clock::now(); });
    ~SessionStore();
    static SessionRecovery recover(SessionStorage&);
    static juce::String backupLegacy(SessionStorage&, juce::String& migrationId);
    static juce::String commit(SessionStorage&, const EncodedSession&);
    juce::uint64 submit(PluginInstances, bool intentionalEmpty, const juce::String& migrationId);
    bool flush(std::chrono::milliseconds timeout = std::chrono::seconds(30));
    void shutdown();
    SessionSaveStatus status() const;
    void clockChanged() { changed.notify_all(); }
private:
    void run();
    std::shared_ptr<SessionStorage> storage;
    const std::chrono::milliseconds debounce;
    std::function<Clock::time_point()> clock;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::optional<SessionDocument> pending;
    SessionSaveStatus current;
    juce::String savedDigest;
    Clock::time_point due;
    juce::uint64 attemptSerial = 0;
    bool force = false, failed = false, stopping = false, joined = false, writable = true;
    std::thread worker;
};
}
