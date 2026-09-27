#include "MixWriter.h"
#include <algorithm>

namespace
{
juce::String safeName(juce::String name)
{
    name = name.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-");
    return name.trim().isEmpty() ? "channel" : name.trim();
}
}

MixWriter::MixWriter(MixCapture& source) : juce::Thread("LightHostMixWriter"), capture(source) {}
MixWriter::~MixWriter() { stop(); }

juce::AudioFormatWriter* MixWriter::openWav(const juce::File& file, int channels) const
{
    file.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream>(file);
    if (!stream->openedOk()) return nullptr;
    juce::WavAudioFormat format;
    return format.createWriterFor(stream.release(), request.sampleRate, (unsigned int) channels, 24, {}, 0);
}

juce::String MixWriter::start(const TakeRequest& incoming)
{
    stop();
    request = incoming;
    if (!request.mixdown && !request.multitrack && request.icecastHost.isEmpty()) return "Nothing to record";
    if (request.mp3) request.interleaved = false;
    auto take = request.folder.getChildFile("take-" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S"));
    if (request.raw) take = take.getChildFile("raw");
    if ((request.mixdown || request.multitrack) && !take.createDirectory()) return "Could not create " + take.getFullPathName();
    request.folder = take;
    if (request.mixdown && !request.mp3)
        if (auto* writer = openWav(take.getChildFile("Master.wav"), 2)) wavs.push_back(std::unique_ptr<juce::AudioFormatWriter>(writer));
    if (request.armed.size() != request.names.size()) request.armed.assign(request.names.size(), 1);
    const auto armedCount = (int) std::count(request.armed.begin(), request.armed.end(), 1);
    if (request.multitrack && request.interleaved && armedCount > 0)
    {
        juce::String list;
        int channel = 1;
        for (int i = 0; i < (int) request.names.size(); ++i)
        {
            if (!request.armed[(size_t) i]) continue;
            list += juce::String(channel) + " " + request.names[(size_t) i] + " L\n" + juce::String(channel + 1) + " " + request.names[(size_t) i] + " R\n";
            channel += 2;
        }
        take.getChildFile("tracks.txt").replaceWithText(list);
        if (auto* writer = openWav(take.getChildFile("Multitrack.wav"), juce::jmax(2, armedCount * 2)))
            interleaved.reset(writer);
    }
    else if (request.multitrack)
    {
        stemWavs.clear();
        stemMp3.clear();
        stemWavs.resize(request.names.size());
        stemMp3.resize(request.names.size());
        int fileIndex = 1;
        for (int i = 0; i < (int) request.names.size(); ++i)
        {
            if (!request.armed[(size_t) i]) continue;
            const auto file = take.getChildFile(juce::String(fileIndex++).paddedLeft('0', 2) + " " + safeName(request.names[(size_t) i]) + (request.mp3 ? ".mp3" : ".wav"));
            if (request.mp3)
            {
                stemMp3[(size_t) i] = std::make_unique<Mp3Sink>();
                if (!stemMp3[(size_t) i]->open(file, request.bitrate, request.sampleRate, {})) return "MP3 encoder failed";
            }
            else if (auto* writer = openWav(file, 2))
                stemWavs[(size_t) i].reset(writer);
        }
    }
    const auto feed = [this](const void* data, size_t bytes) { icecast.write(data, bytes); };
    if (request.mixdown && request.mp3)
    {
        masterMp3 = std::make_unique<Mp3Sink>();
        if (!masterMp3->open(request.folder.getChildFile("Master.mp3"), request.bitrate, request.sampleRate, request.icecastHost.isNotEmpty() ? feed : std::function<void(const void*, size_t)>{}))
            return "MP3 encoder failed";
    }
    else if (request.icecastHost.isNotEmpty())
    {
        masterMp3 = std::make_unique<Mp3Sink>();
        if (!masterMp3->open({}, request.bitrate, request.sampleRate, feed)) return "MP3 encoder failed";
    }
    if (request.icecastHost.isNotEmpty())
    {
        for (int attempt = 0; attempt < 3 && !icecast.connected(); ++attempt)
        {
            if (attempt) juce::Thread::sleep(2000);
            icecast.connect(request.icecastHost, request.icecastPort, request.icecastMount, request.icecastUser, request.icecastPassword, request.icecastName);
        }
        { const juce::ScopedLock lock(statusLock); statusText = icecast.connected() ? icecastLogLine(true) + ". Password is not encrypted." : icecastLogLine(false); }
        if (!icecast.connected() && !request.mixdown && !request.multitrack) return statusText;
    }
    framesWritten = 0;
    startThread();
    return {};
}

void MixWriter::stop()
{
    if (isThreadRunning()) { signalThreadShouldExit(); stopThread(4000); }
    wavs.clear();
    stemWavs.clear();
    mp3.clear();
    stemMp3.clear();
    masterMp3.reset();
    interleaved.reset();
    icecast.close();
}

juce::String MixWriter::status() const
{
    const juce::ScopedLock lock(statusLock);
    return statusText + (framesWritten > 0 ? " " + juce::String(framesWritten / juce::jmax(1.0, request.sampleRate), 1) + " s" : juce::String())
        + " dropped " + juce::String((juce::int64) capture.dropped());
}

void MixWriter::run()
{
    const int block = 2048;
    std::vector<float> left((size_t) block), right((size_t) block);
    std::vector<std::vector<float>> stripL(request.names.size()), stripR(request.names.size());
    for (auto& channel : stripL) channel.resize((size_t) block);
    for (auto& channel : stripR) channel.resize((size_t) block);
    juce::AudioBuffer<float> stereo(2, block);
    const int armedChannels = (int) std::count(request.armed.begin(), request.armed.end(), 1) * 2;
    juce::AudioBuffer<float> wide(juce::jmax(2, armedChannels), block);
    while (!threadShouldExit())
    {
        int masterFrames = 0;
        if (request.mixdown || request.icecastHost.isNotEmpty())
            masterFrames = capture.popMaster(left.data(), right.data(), block);
        int multiFrames = 0;
        if (request.multitrack && !request.names.empty())
        {
            multiFrames = block;
            for (int i = 0; i < (int) request.names.size(); ++i)
                multiFrames = juce::jmin(multiFrames, capture.availableStrip(i));
            for (int i = 0; i < (int) request.names.size(); ++i)
                capture.popStereo(i, stripL[(size_t) i].data(), stripR[(size_t) i].data(), multiFrames);
        }
        if (masterFrames == 0 && multiFrames == 0) { wait(5); continue; }
        if (masterFrames > 0)
        {
            for (int i = 0; i < masterFrames; ++i) { stereo.setSample(0, i, left[(size_t) i]); stereo.setSample(1, i, right[(size_t) i]); }
            if (!wavs.empty() && request.mixdown && !request.mp3) wavs.front()->writeFromAudioSampleBuffer(stereo, 0, masterFrames);
            if (masterMp3) masterMp3->write(left.data(), right.data(), masterFrames);
            framesWritten += masterFrames;
        }
        if (multiFrames > 0 && request.interleaved && interleaved)
        {
            int dest = 0;
            for (int i = 0; i < (int) request.names.size(); ++i)
            {
                if (i >= (int) request.armed.size() || !request.armed[(size_t) i]) continue;
                for (int sample = 0; sample < multiFrames; ++sample)
                {
                    wide.setSample(dest * 2, sample, stripL[(size_t) i][(size_t) sample]);
                    wide.setSample(dest * 2 + 1, sample, stripR[(size_t) i][(size_t) sample]);
                }
                ++dest;
            }
            interleaved->writeFromAudioSampleBuffer(wide, 0, multiFrames);
        }
        else if (multiFrames > 0)
            for (int i = 0; i < (int) request.names.size(); ++i)
            {
                if (i < (int) request.armed.size() && !request.armed[(size_t) i]) continue;
                if (request.mp3 && i < (int) stemMp3.size() && stemMp3[(size_t) i])
                    stemMp3[(size_t) i]->write(stripL[(size_t) i].data(), stripR[(size_t) i].data(), multiFrames);
                else if (i < (int) stemWavs.size() && stemWavs[(size_t) i])
                {
                    for (int sample = 0; sample < multiFrames; ++sample)
                    {
                        stereo.setSample(0, sample, stripL[(size_t) i][(size_t) sample]);
                        stereo.setSample(1, sample, stripR[(size_t) i][(size_t) sample]);
                    }
                    stemWavs[(size_t) i]->writeFromAudioSampleBuffer(stereo, 0, multiFrames);
                }
            }
    }
}
