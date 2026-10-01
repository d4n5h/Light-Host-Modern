#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

class Mp3Sink
{
public:
    ~Mp3Sink();
    bool open(const juce::File& file, int bitsPerSecond, double sourceRate, std::function<void(const void*, size_t)> bytes);
    void write(const float* left, const float* right, int frames);
    void close();
    bool ok() const noexcept { return encoder != nullptr; }

private:
    void encode(const float* left, const float* right, int frames);
    void* encoder = nullptr;
    juce::FileOutputStream* file = nullptr;
    std::function<void(const void*, size_t)> onBytes;
    double sourceRate = 48000.0;
    double phase = 0.0;
};
