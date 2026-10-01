#include "Mp3Sink.h"
#include <lame.h>
#include <cmath>
#include <vector>

Mp3Sink::~Mp3Sink() { close(); }

bool Mp3Sink::open(const juce::File& target, int bitsPerSecond, double rate, std::function<void(const void*, size_t)> bytes)
{
    close();
    onBytes = std::move(bytes);
    sourceRate = rate > 0 ? rate : 48000.0;
    phase = 0.0;
    if (!onBytes)
    {
        target.deleteFile();
        file = new juce::FileOutputStream(target);
        if (!file->openedOk()) { delete file; file = nullptr; return false; }
    }
    auto* flags = lame_init();
    if (flags == nullptr) { close(); return false; }
    const int kbps = juce::jlimit(32, 320, bitsPerSecond / 1000);
    lame_set_in_samplerate(flags, 48000);
    lame_set_out_samplerate(flags, 48000);
    lame_set_num_channels(flags, 2);
    lame_set_mode(flags, STEREO);
    lame_set_brate(flags, kbps);
    lame_set_quality(flags, 2);
    if (lame_init_params(flags) < 0) { lame_close(flags); close(); return false; }
    encoder = flags;
    return true;
}

void Mp3Sink::write(const float* left, const float* right, int frames)
{
    if (!encoder || frames <= 0 || left == nullptr || right == nullptr) return;
    if (std::abs(sourceRate - 48000.0) < 1.0)
    {
        encode(left, right, frames);
        return;
    }
    const double step = sourceRate / 48000.0;
    std::vector<float> outL, outR;
    double cursor = phase;
    while (cursor < frames - 1)
    {
        const int index = (int) cursor;
        const float frac = (float) (cursor - index);
        outL.push_back(left[index] * (1.0f - frac) + left[index + 1] * frac);
        outR.push_back(right[index] * (1.0f - frac) + right[index + 1] * frac);
        cursor += step;
    }
    phase = cursor - frames;
    if (!outL.empty()) encode(outL.data(), outR.data(), (int) outL.size());
}

void Mp3Sink::encode(const float* left, const float* right, int frames)
{
    auto* flags = static_cast<lame_global_flags*>(encoder);
    const int capacity = (int) (1.25 * frames) + 7200;
    std::vector<unsigned char> mp3((size_t) capacity);
    const int written = lame_encode_buffer_ieee_float(flags, left, right, frames, mp3.data(), capacity);
    if (written <= 0) return;
    if (onBytes) onBytes(mp3.data(), (size_t) written);
    else if (file) file->write(mp3.data(), (size_t) written);
}

void Mp3Sink::close()
{
    if (auto* flags = static_cast<lame_global_flags*>(encoder))
    {
        unsigned char tail[7200];
        const int written = lame_encode_flush(flags, tail, (int) sizeof(tail));
        if (written > 0)
        {
            if (onBytes) onBytes(tail, (size_t) written);
            else if (file) file->write(tail, (size_t) written);
        }
        lame_close(flags);
        encoder = nullptr;
    }
    if (file) { file->flush(); delete file; file = nullptr; }
}
