#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <map>
#include <set>

class PluginScanController
{
public:
    struct Failure
    {
        juce::String path, format, reason, id, kind = "probe";
        int attempt = 1;
        bool resolved = false;
    };
    struct Status
    {
        bool active = false;
        bool cancelled = false;
        int completed = 0, total = 0, cached = 0;
        int enumerations = 0, examined = 0;
        juce::String scanId;
        uint64_t revision = 0;
        size_t failureCount = 0;
        juce::String currentFile;
        std::vector<Failure> failures;
    };
    explicit PluginScanController(juce::File scannerExecutable = {}, unsigned timeoutMs = 60000, juce::File cacheDirectory = {});
    ~PluginScanController();
    bool begin();
    void enqueue(juce::FileSearchPath paths, juce::String format,
                 juce::Array<juce::PluginDescription> known, bool force = false);
    void cancel();
    // Bounded display snapshot; retry retains the complete internal failure list.
    Status status() const;
    std::pair<juce::String, uint64_t> version() const
    { std::lock_guard<std::mutex> lock(mutex); return {progress.scanId, progress.revision}; }
    struct FailurePage { bool stale = false; size_t total = 0; std::vector<Failure> failures; };
    FailurePage failures(const juce::String& scanId, uint64_t revision, size_t offset, size_t limit = 100) const;
    juce::String metadata(const juce::String& knownId) const;
    std::vector<juce::PluginDescription> takeResults();
    bool retryFailures(const juce::StringArray& ids = {});
private:
    struct Work
    {
        juce::FileSearchPath paths;
        juce::String format;
        juce::Array<juce::PluginDescription> known;
        bool force;
        uint64_t generation;
        int attempt = 1;
    };
    void run();
    void scan(const Work&);
    void addFailure(const Work&, const juce::String& path, const juce::String& reason, const juce::String& kind);
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Work> queue;
    std::vector<juce::PluginDescription> results;
    Status progress;
    bool working = false;
    std::atomic<bool> stopping { false };
    std::atomic<uint64_t> generation { 0 };
    juce::File scannerExecutable;
    juce::File cacheDirectory;
    std::map<juce::String, juce::String> pluginMetadata;
    std::set<juce::String> seenModules;
    unsigned timeoutMs;
    std::thread worker;
};
