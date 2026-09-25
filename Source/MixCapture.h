#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>

// Single-producer ring. The audio callback pushes. The writer and stream callbacks pop.
class MixCapture
{
public:
    void prepare(int blockSize, int strips);
    void arm(bool master, bool multi, bool rawTake);
    void disarm();
    void setStream(bool enabled);
    void setRecordPaused(bool paused) noexcept { recordPaused.store(paused, std::memory_order_release); }
    void setStreamPaused(bool paused) noexcept { streamPaused.store(paused, std::memory_order_release); }
    bool isRecordPaused() const noexcept { return recordPaused.load(std::memory_order_acquire); }
    bool isStreamPaused() const noexcept { return streamPaused.load(std::memory_order_acquire); }

    bool armed() const noexcept { return on.load(std::memory_order_acquire); }
    bool raw() const noexcept { return rawOn.load(std::memory_order_acquire); }
    bool recordMaster() const noexcept { return masterOn.load(std::memory_order_acquire); }
    bool recordMulti() const noexcept { return multiOn.load(std::memory_order_acquire); }
    bool streamOn() const noexcept { return stream.load(std::memory_order_acquire); }
    uint64_t dropped() const noexcept { return drops.load(std::memory_order_relaxed); }
    int strips() const noexcept { return stripCount; }
    int blockLimit() const noexcept { return maxBlock; }

    void copyDry(const juce::AudioBuffer<float>& bus, int frames);
    const float* dry(int channel) const noexcept { return drySamples.data() + channel * maxBlock; }
    float* stem(int channel) noexcept { return stemSamples.data() + channel * maxBlock; }

    void clearRawMaster(int frames);
    void addRawMaster(const float* left, const float* right, int frames);
    void commitRawMaster(float masterGain, int frames);

    bool pushStereo(int stripIndex, const float* left, const float* right, int frames);
    bool pushMaster(const float* left, const float* right, int frames);
    bool pushStream(const float* left, const float* right, int frames);

    int popStereo(int stripIndex, float* left, float* right, int frames);
    int popMaster(float* left, float* right, int frames);
    int popStream(float* left, float* right, int frames);
    int availableStrip(int stripIndex) const;
    int availableMaster() const;

private:
    struct Ring
    {
        void assign(int frames);
        bool push(const float* left, const float* right, int frames, std::atomic<uint64_t>& drops);
        int pop(float* left, float* right, int frames);
        int available() const;
        std::vector<float> left, right;
        int capacity = 0;
        std::atomic<int> read{ 0 }, write{ 0 };
    };

    Ring master, streamRing;
    std::vector<std::unique_ptr<Ring>> stripRings;
    std::vector<float> drySamples, stemSamples, rawMaster;
    int maxBlock = 0;
    int stripCount = 0;
    std::atomic<bool> on{ false }, rawOn{ false }, masterOn{ false }, multiOn{ false }, stream{ false };
    std::atomic<bool> recordPaused{ false }, streamPaused{ false };
    std::vector<float> silence;
    std::atomic<uint64_t> drops{ 0 };
};
