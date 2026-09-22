#include "PluginScanController.h"
#include "ScanProcess.h"
#include "ScannerProtocol.h"
#include "RuntimeProfile.h"
#include "PluginInstances.h"
#include <algorithm>

using namespace juce;
namespace
{
File defaultCacheDirectory()
{
    const auto& profile = lightHostModern::RuntimeProfile::current();
    return profile.test ? File((profile.directory / L"Cache" / L"Plugins").wstring().c_str())
        : File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("LightHostModern/Cache/Plugins");
}
struct ScanFiles
{
    File folder, request, response;
    int batches = 0;
    ScanFiles()
    {
        const auto& profile = lightHostModern::RuntimeProfile::current();
        const auto root = profile.test ? File((profile.directory / L"Temp").wstring().c_str())
            : File::getSpecialLocation(File::tempDirectory);
        folder = root.getChildFile("LightHostModernScan-" + Uuid().toString());
        request = folder.getChildFile("request.xml");
        response = folder.getChildFile("response.xml");
    }
    ~ScanFiles()
    {
        request.deleteFile(); response.deleteFile();
        for (int index = 0; index <= batches; ++index) lightHostModern::scan::batchFile(response, index).deleteFile();
        folder.deleteFile(); // Never recursively remove worker-created paths.
    }
};
String workerFailure(lightHostModern::scan::Result result)
{
    using Exit = lightHostModern::scan::Exit;
    if (result.outcome == Exit::timeout) return "timeout";
    if (result.outcome == Exit::launchFailed) return "launch_failed";
    if (result.outcome == Exit::cancelled) return "cancelled";
    if (result.outcome == Exit::success) return {};
    if (result.code == 7) return "changed";
    if (result.code == 8) return "missing";
    if (result.code == 9) return "metadata_unavailable";
    return "crash";
}
std::wstring workerArguments(const ScanFiles& files)
{
    return lightHostModern::scan::quoteArgument(files.request.getFullPathName().toWideCharPointer()) + L" "
        + lightHostModern::scan::quoteArgument(files.response.getFullPathName().toWideCharPointer());
}
}

PluginScanController::PluginScanController(File executable, unsigned timeout, File cache)
    : scannerExecutable(executable == File() ? File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("LightHostModernScanner.exe") : executable),
      cacheDirectory(cache == File() ? defaultCacheDirectory() : cache), timeoutMs(timeout), worker([this] { run(); }) {}
PluginScanController::~PluginScanController()
{
    stopping.store(true);
    cancel(); wake.notify_all();
    if (worker.joinable()) worker.join();
}

void PluginScanController::enqueue(FileSearchPath paths, String format, Array<PluginDescription> known, bool force)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (progress.scanId.isEmpty()) progress.scanId = Uuid().toString();
    progress.cancelled = false;
    queue.push_back({std::move(paths), std::move(format), std::move(known), force, generation.load(), 1});
    progress.active = true; ++progress.revision;
    wake.notify_one();
}
void PluginScanController::cancel()
{
    std::lock_guard<std::mutex> lock(mutex);
    ++generation;
    progress.cancelled = true; queue.clear(); progress.active = working; ++progress.revision;
    wake.notify_all();
}
bool PluginScanController::begin()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (working || !queue.empty()) return false;
    ++generation;
    progress = {}; progress.scanId = Uuid().toString(); progress.revision = 1;
    seenModules.clear();
    return true;
}
PluginScanController::Status PluginScanController::status() const
{
    std::lock_guard<std::mutex> lock(mutex);
    Status snapshot;
    snapshot.active = progress.active; snapshot.cancelled = progress.cancelled;
    snapshot.completed = progress.completed; snapshot.total = progress.total; snapshot.cached = progress.cached;
    snapshot.currentFile = progress.currentFile; snapshot.scanId = progress.scanId; snapshot.revision = progress.revision;
    snapshot.enumerations = progress.enumerations; snapshot.examined = progress.examined;
    for (const auto& failure : progress.failures) if (!failure.resolved)
    {
        ++snapshot.failureCount;
        if (snapshot.failures.size() < 100) snapshot.failures.push_back(failure);
    }
    return snapshot;
}
PluginScanController::FailurePage PluginScanController::failures(const String& id, uint64_t revision, size_t offset, size_t limit) const
{
    std::lock_guard<std::mutex> lock(mutex);
    FailurePage page;
    page.stale = id != progress.scanId || revision != progress.revision;
    size_t index = 0;
    size_t bytes = 0;
    bool full = false;
    for (const auto& failure : progress.failures) if (!failure.resolved)
    {
        ++page.total;
        if (!page.stale && index++ >= offset && !full && page.failures.size() < jmin(size_t(100), limit))
        {
            // Conservative JSON escaping allowance keeps every page below the
            // command/event message limit even for long Unicode paths.
            const auto size = 512 + 6 * (failure.path.getNumBytesAsUTF8() + failure.reason.getNumBytesAsUTF8() + failure.format.getNumBytesAsUTF8());
            if (!page.failures.empty() && bytes + size > 3 * 1024 * 1024) full = true;
            else { page.failures.push_back(failure); bytes += size; }
        }
    }
    return page;
}
String PluginScanController::metadata(const String& id) const
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto found = pluginMetadata.find(id);
    return found == pluginMetadata.end() ? String() : found->second;
}
std::vector<PluginDescription> PluginScanController::takeResults()
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<PluginDescription> batch; batch.swap(results); return batch;
}
bool PluginScanController::retryFailures(const StringArray& ids)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (working || !queue.empty()) return false;
    for (const auto& id : ids)
        if (std::none_of(progress.failures.begin(), progress.failures.end(), [&](const auto& failure) { return failure.id == id && !failure.resolved; })) return false;
    std::map<std::pair<String, int>, FileSearchPath> groups;
    for (const auto& failure : progress.failures)
        if (!failure.resolved && (ids.isEmpty() || ids.contains(failure.id))) groups[{failure.format, failure.attempt + 1}].add(File(failure.path));
    if (groups.empty()) return false;
    ++generation; seenModules.clear();
    for (auto& group : groups) queue.push_back({std::move(group.second), group.first.first, {}, true, generation.load(), group.first.second});
    progress.cancelled = false; progress.active = true; ++progress.revision;
    wake.notify_one(); return true;
}
void PluginScanController::addFailure(const Work& work, const String& path, const String& reason, const String& kind)
{
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& failure : progress.failures)
        if (failure.path == path && failure.format == work.format && failure.kind == kind)
        {
            failure.reason = reason; failure.attempt = work.attempt; failure.resolved = false; ++progress.revision; return;
        }
    progress.failures.push_back({path, work.format, reason, Uuid().toString(), kind, work.attempt, false});
    ++progress.revision;
}
void PluginScanController::run()
{
    // Restore persisted metadata on this worker, never on a snapshot/UI call.
    for (const auto& file : cacheDirectory.findChildFiles(File::findFiles, false, "*.xml"))
    {
        if (stopping.load()) return;
        const auto cached = file.getSize() <= lightHostModern::scan::maximumResponseBytes ? XmlDocument::parse(file) : nullptr;
        if (!cached || cached->getIntAttribute("cacheVersion") != lightHostModern::scan::metadataCacheVersion) continue;
        for (const auto* item : cached->getChildIterator())
            if (item->hasTagName("ENTRY") && item->getStringAttribute("verifiedMetadata") == "verified")
            {
                const auto id = item->getStringAttribute("knownId");
                if (id.length() != 64 || !id.containsOnly("0123456789abcdef")) continue;
                std::lock_guard<std::mutex> lock(mutex); pluginMetadata[id] = item->toString();
            }
    }
    for (;;)
    {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait(lock, [this] { return stopping.load() || !queue.empty(); });
        if (stopping.load()) return;
        auto work = std::move(queue.front()); queue.pop_front(); working = true;
        lock.unlock();
        try { scan(work); }
        catch (...) { addFailure(work, work.paths.toString(), "internal_error", "enumeration"); }
        lock.lock(); working = false; progress.active = !queue.empty(); progress.currentFile.clear(); ++progress.revision;
    }
}

void PluginScanController::scan(const Work& work)
{
    using namespace lightHostModern::scan;
    const auto cancelled = [&] { return stopping.load() || generation.load() != work.generation; };
    struct Candidate { String path, fingerprint; Time stamp; };
    std::vector<Candidate> candidates;
    int currentRootIndex = -1;
    ScanFiles enumeration;
    XmlElement request("SCAN");
    const auto enumerationId = Uuid().toString();
    request.setAttribute("version", scannerProtocolVersion); request.setAttribute("id", enumerationId);
    request.setAttribute("mode", "enumerate"); request.setAttribute("format", work.format);
    for (int index = 0; index < work.paths.getNumPaths(); ++index)
    {
        auto* root = request.createNewChildElement("ROOT");
        root->setAttribute("path", work.paths[index].getFullPathName()); root->setAttribute("index", index);
    }
    if (enumeration.folder.createDirectory().failed() || !request.writeTo(enumeration.request))
    { addFailure(work, work.paths.toString(), "write_failed", "enumeration"); return; }
    { std::lock_guard<std::mutex> lock(mutex); ++progress.enumerations; progress.currentFile = work.paths.toString(); ++progress.revision; }
    const auto consume = [&] {
        for (;;)
        {
            const auto file = batchFile(enumeration.response, enumeration.batches);
            if (!file.existsAsFile()) break; // Only the local per-operation directory is read by the host.
            auto batch = file.getSize() <= maximumResponseBytes ? XmlDocument::parse(file) : nullptr;
            if (!batch || !batch->hasTagName("BATCH") || batch->getStringAttribute("id") != enumerationId
                || batch->getIntAttribute("version") != scannerProtocolVersion || batch->getIntAttribute("sequence") != enumeration.batches
                || batch->getNumChildElements() > batchItems) throw std::runtime_error("invalid_enumeration_result");
            for (const auto* item : batch->getChildIterator())
            {
                const auto path = item->getStringAttribute("path");
                if (item->hasTagName("ROOT"))
                {
                    const int index = item->getIntAttribute("index", -1);
                    if (index <= currentRootIndex || index >= work.paths.getNumPaths() || work.paths[index].getFullPathName() != path)
                        throw std::runtime_error("invalid_root_progress");
                    currentRootIndex = index;
                    std::lock_guard<std::mutex> lock(mutex); progress.currentFile = path; ++progress.revision;
                    continue;
                }
                if (item->hasTagName("FAILURE")) { addFailure(work, path, item->getStringAttribute("reason"), "enumeration"); continue; }
                if (!item->hasTagName("CANDIDATE") || !File::isAbsolutePath(path)
                    || !File(path).hasFileExtension(work.format == "VST3" ? "vst3" : "dll")
                    || item->getStringAttribute("fingerprint").length() != 64) throw std::runtime_error("invalid_candidate");
                const auto key = work.format + "\n" + item->getStringAttribute("canonicalPath", path).toLowerCase() + "\n" + item->getStringAttribute("fingerprint");
                bool accepted = false;
                { std::lock_guard<std::mutex> lock(mutex); accepted = seenModules.insert(key).second; if (accepted) { ++progress.total; ++progress.revision; } }
                if (accepted) candidates.push_back({path, item->getStringAttribute("fingerprint"), Time(item->getStringAttribute("stamp").getLargeIntValue())});
            }
            file.deleteFile(); ++enumeration.batches;
        }
    };
    const auto enumerated = lightHostModern::scan::run(scannerExecutable.getFullPathName().toWideCharPointer(), workerArguments(enumeration), cancelled, timeoutMs, consume,
        [&] { return static_cast<uint64_t>(currentRootIndex + 1); });
    consume();
    auto enumerationError = workerFailure(enumerated);
    if (enumerationError.isEmpty())
    {
        const auto final = enumeration.response.getSize() <= maximumResponseBytes ? XmlDocument::parse(enumeration.response) : nullptr;
        if (!final || !final->hasTagName("SCAN") || final->getStringAttribute("id") != enumerationId
            || final->getIntAttribute("version") != scannerProtocolVersion || final->getStringAttribute("mode") != "enumerate"
            || final->getIntAttribute("batches", -1) != enumeration.batches) enumerationError = "invalid_result";
    }
    if (enumerationError.isNotEmpty() && enumerationError != "cancelled")
    {
        if (currentRootIndex < 0)
            for (int index = 0; index < work.paths.getNumPaths(); ++index) addFailure(work, work.paths[index].getFullPathName(), enumerationError, "enumeration");
        else
        {
            addFailure(work, work.paths[currentRootIndex].getFullPathName(), enumerationError, "enumeration");
            // A blocked root must not prevent later roots from being evaluated.
            FileSearchPath remaining;
            for (int index = currentRootIndex + 1; index < work.paths.getNumPaths(); ++index) remaining.add(work.paths[index]);
            if (remaining.getNumPaths() > 0 && !cancelled())
            {
                std::lock_guard<std::mutex> lock(mutex);
                queue.push_front({remaining, work.format, work.known, work.force, work.generation, work.attempt});
            }
        }
    }
    if (cancelled()) return;
    // Deterministic examination order also makes overlapping-root scenarios reproducible.
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.path.compareNatural(b.path) < 0; });
    for (const auto& candidate : candidates)
    {
        if (cancelled()) break;
        { std::lock_guard<std::mutex> lock(mutex); progress.currentFile = candidate.path; ++progress.revision; }
        const auto moduleKey = work.format + "\n" + candidate.path.toLowerCase();
        const auto cacheFile = cacheDirectory.getChildFile(SHA256(moduleKey.toRawUTF8(), moduleKey.getNumBytesAsUTF8()).toHexString() + ".xml");
        auto response = !work.force && cacheFile.getSize() <= maximumResponseBytes ? XmlDocument::parse(cacheFile) : nullptr;
        const auto validResponse = [&](const XmlElement* xml) {
            return xml && xml->hasTagName("SCAN") && xml->getIntAttribute("version") == scannerProtocolVersion
                && xml->getStringAttribute("mode") == "probe" && xml->getStringAttribute("path") == candidate.path
                && xml->getStringAttribute("format") == work.format && xml->getStringAttribute("fingerprint") == candidate.fingerprint;
        };
        const bool cached = validResponse(response.get()) && response->getIntAttribute("cacheVersion") == metadataCacheVersion;
        String error;
        if (!cached)
        {
            response.reset();
            ScanFiles files;
            XmlElement probe("SCAN");
            const auto id = Uuid().toString();
            probe.setAttribute("version", scannerProtocolVersion); probe.setAttribute("id", id); probe.setAttribute("mode", "probe");
            probe.setAttribute("path", candidate.path); probe.setAttribute("format", work.format); probe.setAttribute("fingerprint", candidate.fingerprint);
            if (files.folder.createDirectory().failed() || !probe.writeTo(files.request)) error = "write_failed";
            else
            {
                { std::lock_guard<std::mutex> lock(mutex); ++progress.examined; ++progress.revision; }
                const auto result = lightHostModern::scan::run(scannerExecutable.getFullPathName().toWideCharPointer(), workerArguments(files), cancelled, timeoutMs);
                error = workerFailure(result);
                if (error == "cancelled") break;
                if (error.isEmpty())
                {
                    response = files.response.getSize() <= maximumResponseBytes ? XmlDocument::parse(files.response) : nullptr;
                    if (!validResponse(response.get()) || response->getStringAttribute("id") != id) { error = "invalid_result"; response.reset(); }
                }
            }
        }
        std::vector<PluginDescription> validated;
        std::map<String, String> metadata;
        if (response)
        {
            std::set<String> identities;
            for (const auto* entry : response->getChildIterator())
            {
                const auto* description = entry->getChildByName("PLUGIN");
                PluginDescription plugin;
                if (!entry->hasTagName("ENTRY") || !description || !plugin.loadFromXml(*description)
                    || plugin.name.isEmpty() || plugin.pluginFormatName != work.format
                    || !belongsToModule(plugin.fileOrIdentifier, candidate.path, work.format)
                    || plugin.numInputChannels < 0 || plugin.numOutputChannels < 0
                    || entry->getStringAttribute("knownId") != lightHostModern::knownPluginId(plugin)
                    || !identities.insert(lightHostModern::knownPluginId(plugin)).second)
                { error = "invalid_result"; continue; }
                if (entry->getStringAttribute("error").isNotEmpty()) { error = entry->getStringAttribute("error"); continue; }
                if (entry->getStringAttribute("verifiedMetadata") != "verified") { error = "unverified_metadata"; continue; }
                bool busesValid = true;
                for (const auto* bus : entry->getChildIterator()) if (bus->hasTagName("BUS"))
                    if ((bus->getStringAttribute("direction") != "input" && bus->getStringAttribute("direction") != "output")
                        || bus->getIntAttribute("channels", -1) < 0 || bus->getIntAttribute("defaultChannels", -1) < 0) busesValid = false;
                if (!busesValid) { error = "invalid_buses"; continue; }
                plugin.lastFileModTime = candidate.stamp;
                metadata[lightHostModern::knownPluginId(plugin)] = entry->toString();
                validated.push_back(std::move(plugin));
            }
            if (validated.empty() && error.isEmpty()) error = "no_plugins";
        }
        if (response && error.isEmpty() && !cached && cacheDirectory.createDirectory().wasOk())
        {
            response->setAttribute("cacheVersion", metadataCacheVersion);
            TemporaryFile temporary(cacheFile);
            if (response->writeTo(temporary.getFile())) temporary.overwriteTargetFileWithTemporary();
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++progress.completed; if (cached) ++progress.cached; ++progress.revision;
            for (auto& item : metadata) pluginMetadata[item.first] = std::move(item.second);
            // Cache also restores entries removed from memory after a controller restart.
            if (!cached || std::any_of(validated.begin(), validated.end(), [&](const auto& plugin) {
                return std::none_of(work.known.begin(), work.known.end(), [&](const auto& known) { return lightHostModern::knownPluginId(known) == lightHostModern::knownPluginId(plugin); });
            })) results.insert(results.end(), validated.begin(), validated.end());
            if (error.isEmpty()) for (auto& failure : progress.failures)
                if (failure.path == candidate.path && failure.format == work.format) failure.resolved = true;
        }
        if (error.isNotEmpty()) addFailure(work, candidate.path, error, "probe");
    }
}
