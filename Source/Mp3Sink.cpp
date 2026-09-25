#include "Mp3Sink.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>

namespace
{
struct Release { template <typename T> void operator()(T* p) const { if (p) p->Release(); } };

int& mediaUsers()
{
    static int users = 0;
    return users;
}

void emit(IMFTransform* transform, juce::FileOutputStream* file, const std::function<void(const void*, size_t)>& onBytes)
{
    MFT_OUTPUT_STREAM_INFO info{};
    transform->GetOutputStreamInfo(0, &info);
    IMFSample* sample = nullptr;
    IMFMediaBuffer* buffer = nullptr;
    const DWORD bytes = info.cbSize > 4096u ? info.cbSize : 4096u;
    if (FAILED(MFCreateSample(&sample)) || FAILED(MFCreateMemoryBuffer(bytes, &buffer)))
    {
        if (sample) sample->Release();
        return;
    }
    sample->AddBuffer(buffer);
    buffer->Release();
    MFT_OUTPUT_DATA_BUFFER output{};
    output.pSample = sample;
    DWORD status = 0;
    while (transform->ProcessOutput(0, 1, &output, &status) == S_OK)
    {
        IMFMediaBuffer* out = nullptr;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&out)) && out)
        {
            BYTE* data = nullptr;
            DWORD length = 0;
            if (SUCCEEDED(out->Lock(&data, nullptr, &length)) && data && length)
            {
                if (file) file->write(data, length);
                if (onBytes) onBytes(data, length);
                out->Unlock();
            }
            out->Release();
        }
        sample->Release();
        sample = nullptr;
        const DWORD bytes = info.cbSize > 4096u ? info.cbSize : 4096u;
    if (FAILED(MFCreateSample(&sample)) || FAILED(MFCreateMemoryBuffer(bytes, &buffer))) break;
        sample->AddBuffer(buffer);
        buffer->Release();
        output = {};
        output.pSample = sample;
    }
    if (sample) sample->Release();
}
}

Mp3Sink::~Mp3Sink() { close(); }

bool Mp3Sink::open(const juce::File& target, int bitsPerSecond, double rate, std::function<void(const void*, size_t)> bytes)
{
    close();
    sourceRate = rate > 0 ? rate : 48000.0;
    onBytes = std::move(bytes);
    if (target != juce::File())
    {
        target.deleteFile();
        file = new juce::FileOutputStream(target);
        if (!file->openedOk()) { delete file; file = nullptr; return false; }
    }
    if (mediaUsers()++ == 0) MFStartup(MF_VERSION);
    started = true;
    MFT_REGISTER_TYPE_INFO inInfo{ MFMediaType_Audio, MFAudioFormat_PCM };
    MFT_REGISTER_TYPE_INFO outInfo{ MFMediaType_Audio, MFAudioFormat_MP3 };
    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    if (FAILED(MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER, &inInfo, &outInfo, &activates, &count)) || count == 0)
    {
        close();
        return false;
    }
    IMFTransform* transform = nullptr;
    activates[0]->ActivateObject(IID_PPV_ARGS(&transform));
    for (UINT32 i = 0; i < count; ++i) activates[i]->Release();
    CoTaskMemFree(activates);
    if (!transform) { close(); return false; }
    IMFMediaType* outType = nullptr;
    IMFMediaType* inType = nullptr;
    MFCreateMediaType(&outType);
    outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_MP3);
    outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32) (juce::jlimit(32000, 320000, bitsPerSecond) / 8));
    const auto outOk = transform->SetOutputType(0, outType, 0);
    outType->Release();
    MFCreateMediaType(&inType);
    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
    inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 48000 * 4);
    const auto inOk = transform->SetInputType(0, inType, 0);
    inType->Release();
    if (FAILED(outOk) || FAILED(inOk)) { transform->Release(); close(); return false; }
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    encoder = transform;
    started = true;
    phase = 0;
    timestamp = 0;
    return true;
}

void Mp3Sink::write(const float* left, const float* right, int frames)
{
    if (!encoder || frames <= 0) return;
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
    auto* transform = static_cast<IMFTransform*>(encoder);
    const DWORD bytes = (DWORD) frames * 4;
    IMFSample* sample = nullptr;
    IMFMediaBuffer* buffer = nullptr;
    if (FAILED(MFCreateSample(&sample)) || FAILED(MFCreateMemoryBuffer(bytes, &buffer))) { if (sample) sample->Release(); return; }
    BYTE* data = nullptr;
    buffer->Lock(&data, nullptr, nullptr);
    auto* pcm = reinterpret_cast<int16_t*>(data);
    for (int i = 0; i < frames; ++i)
    {
        pcm[i * 2] = (int16_t) juce::jlimit(-32768, 32767, (int) std::lrint(left[i] * 32767.0f));
        pcm[i * 2 + 1] = (int16_t) juce::jlimit(-32768, 32767, (int) std::lrint(right[i] * 32767.0f));
    }
    buffer->Unlock();
    buffer->SetCurrentLength(bytes);
    sample->AddBuffer(buffer);
    buffer->Release();
    sample->SetSampleTime(timestamp);
    const auto duration = (LONGLONG) frames * 10000000 / 48000;
    sample->SetSampleDuration(duration);
    timestamp += duration;
    if (SUCCEEDED(transform->ProcessInput(0, sample, 0)))
        emit(transform, file, onBytes);
    sample->Release();
}

void Mp3Sink::close()
{
    if (auto* transform = static_cast<IMFTransform*>(encoder))
    {
        transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
        emit(transform, file, onBytes);
        transform->Release();
        encoder = nullptr;
    }
    if (file) { file->flush(); delete file; file = nullptr; }
    if (started)
    {
        started = false;
        if (--mediaUsers() <= 0) MFShutdown();
    }
}
