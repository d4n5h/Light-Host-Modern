#include "MixCapture.h"

void MixCapture::Ring::assign(int frames)
{
    capacity = frames;
    left.assign((size_t) frames, 0.0f);
    right.assign((size_t) frames, 0.0f);
    read.store(0);
    write.store(0);
}

bool MixCapture::Ring::push(const float* inL, const float* inR, int frames, std::atomic<uint64_t>& drops)
{
    if (capacity <= 0 || frames <= 0) return false;
    const int w = write.load(std::memory_order_relaxed);
    const int r = read.load(std::memory_order_acquire);
    const int used = w >= r ? w - r : capacity - r + w;
    if (used + frames >= capacity)
    {
        drops.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    for (int i = 0; i < frames; ++i)
    {
        const int at = (w + i) % capacity;
        left[(size_t) at] = inL[i];
        right[(size_t) at] = inR[i];
    }
    write.store((w + frames) % capacity, std::memory_order_release);
    return true;
}

int MixCapture::Ring::available() const
{
    const int r = read.load(std::memory_order_acquire);
    const int w = write.load(std::memory_order_acquire);
    return w >= r ? w - r : capacity - r + w;
}

int MixCapture::Ring::pop(float* outL, float* outR, int frames)
{
    const int r = read.load(std::memory_order_relaxed);
    const int w = write.load(std::memory_order_acquire);
    const int have = w >= r ? w - r : capacity - r + w;
    const int n = juce::jmin(frames, have);
    for (int i = 0; i < n; ++i)
    {
        const int at = (r + i) % capacity;
        if (outL) outL[i] = left[(size_t) at];
        if (outR) outR[i] = right[(size_t) at];
    }
    read.store((r + n) % capacity, std::memory_order_release);
    return n;
}

void MixCapture::prepare(int blockSize, int strips)
{
    maxBlock = juce::jmax(1, blockSize);
    stripCount = juce::jmax(0, strips);
    const int frames = juce::jmax(65536, maxBlock * 8);
    master.assign(frames);
    streamRing.assign(frames);
    stripRings.clear();
    stripRings.reserve((size_t) stripCount);
    for (int i = 0; i < stripCount; ++i)
    {
        stripRings.push_back(std::make_unique<Ring>());
        stripRings.back()->assign(frames);
    }
    drySamples.assign((size_t) maxBlock * 2, 0.0f);
    silence.assign((size_t) maxBlock, 0.0f);
    stemSamples.assign((size_t) maxBlock * 2, 0.0f);
    rawMaster.assign((size_t) maxBlock * 2, 0.0f);
    drops.store(0);
}

void MixCapture::arm(bool recordMasterIn, bool multi, bool rawTake)
{
    drops.store(0);
    master.read.store(0); master.write.store(0);
    for (auto& ring : stripRings) { ring->read.store(0); ring->write.store(0); }
    recordPaused.store(false);
    rawOn.store(rawTake);
    masterOn.store(recordMasterIn);
    multiOn.store(multi);
    on.store(true);
}

void MixCapture::disarm() { on.store(false); }
void MixCapture::setStream(bool enabled)
{
    if (enabled) { streamRing.read.store(0); streamRing.write.store(0); }
    streamPaused.store(false);
    stream.store(enabled);
}

void MixCapture::copyDry(const juce::AudioBuffer<float>& bus, int frames)
{
    frames = juce::jmin(frames, maxBlock);
    const float* inL = bus.getNumChannels() > 0 ? bus.getReadPointer(0) : nullptr;
    const float* inR = bus.getNumChannels() > 1 ? bus.getReadPointer(1) : inL;
    auto* dL = drySamples.data();
    auto* dR = drySamples.data() + maxBlock;
    for (int i = 0; i < frames; ++i)
    {
        dL[i] = inL ? inL[i] : 0.0f;
        dR[i] = inR ? inR[i] : dL[i];
    }
}

void MixCapture::clearRawMaster(int frames)
{
    frames = juce::jmin(frames, maxBlock);
    juce::FloatVectorOperations::clear(rawMaster.data(), frames);
    juce::FloatVectorOperations::clear(rawMaster.data() + maxBlock, frames);
}

void MixCapture::addRawMaster(const float* left, const float* right, int frames)
{
    frames = juce::jmin(frames, maxBlock);
    juce::FloatVectorOperations::add(rawMaster.data(), left, frames);
    juce::FloatVectorOperations::add(rawMaster.data() + maxBlock, right, frames);
}

void MixCapture::commitRawMaster(float masterGain, int frames)
{
    frames = juce::jmin(frames, maxBlock);
    auto* l = rawMaster.data();
    auto* r = rawMaster.data() + maxBlock;
    if (masterGain != 1.0f)
    {
        juce::FloatVectorOperations::multiply(l, masterGain, frames);
        juce::FloatVectorOperations::multiply(r, masterGain, frames);
    }
    pushMaster(l, r, frames);
}

bool MixCapture::pushStereo(int stripIndex, const float* left, const float* right, int frames)
{
    if (recordPaused.load(std::memory_order_acquire)) return true;
    if (stripIndex < 0 || stripIndex >= (int) stripRings.size()) return false;
    return stripRings[(size_t) stripIndex]->push(left, right, frames, drops);
}

bool MixCapture::pushMaster(const float* left, const float* right, int frames)
{
    if (recordPaused.load(std::memory_order_acquire)) return true;
    return master.push(left, right, frames, drops);
}

bool MixCapture::pushStream(const float* left, const float* right, int frames)
{
    if (streamPaused.load(std::memory_order_acquire))
    {
        frames = juce::jmin(frames, (int) silence.size());
        return streamRing.push(silence.data(), silence.data(), frames, drops);
    }
    return streamRing.push(left, right, frames, drops);
}

int MixCapture::popStereo(int stripIndex, float* left, float* right, int frames)
{
    if (stripIndex < 0 || stripIndex >= (int) stripRings.size()) return 0;
    return stripRings[(size_t) stripIndex]->pop(left, right, frames);
}

int MixCapture::availableStrip(int stripIndex) const
{
    return stripIndex >= 0 && stripIndex < (int) stripRings.size() ? stripRings[(size_t) stripIndex]->available() : 0;
}
int MixCapture::availableMaster() const { return master.available(); }
int MixCapture::popMaster(float* left, float* right, int frames) { return master.pop(left, right, frames); }
int MixCapture::popStream(float* left, float* right, int frames) { return streamRing.pop(left, right, frames); }
