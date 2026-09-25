#pragma once
#include "MixCapture.h"
#include "Mp3Sink.h"
#include "IcecastSource.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

struct TakeRequest
{
    juce::File folder;
    bool mp3 = false;
    int bitrate = 192000;
    bool mixdown = true;
    bool multitrack = false;
    bool interleaved = false;
    bool raw = false;
    double sampleRate = 48000.0;
    std::vector<juce::String> names;
    juce::String icecastHost;
    int icecastPort = 8000;
    juce::String icecastMount, icecastUser, icecastPassword, icecastName;
};

class MixWriter : private juce::Thread
{
public:
    explicit MixWriter(MixCapture& capture);
    ~MixWriter() override;
    juce::String start(const TakeRequest& request);
    void stop();
    bool running() const noexcept { return isThreadRunning(); }
    juce::String status() const;

private:
    void run() override;
    juce::AudioFormatWriter* openWav(const juce::File& file, int channels) const;
    MixCapture& capture;
    TakeRequest request;
    std::vector<std::unique_ptr<juce::AudioFormatWriter>> wavs;
    std::vector<std::unique_ptr<Mp3Sink>> mp3;
    std::unique_ptr<Mp3Sink> masterMp3;
    IcecastSource icecast;
    std::unique_ptr<juce::AudioFormatWriter> interleaved;
    mutable juce::CriticalSection statusLock;
    juce::String statusText;
    int64_t framesWritten = 0;
};
