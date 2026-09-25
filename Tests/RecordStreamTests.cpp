#include "MixCapture.h"
#include "MackieSurface.h"
#include "IcecastSource.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

static void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

int main()
{
    try
    {
        juce::ScopedJuceInitialiser_GUI library;
        MixCapture capture;
        capture.prepare(512, 1);
        std::vector<float> block(1000, 0.25f);
        int pushed = 0;
        while (capture.pushMaster(block.data(), block.data(), 1000)) ++pushed;
        require(pushed > 0 && capture.dropped() >= 1, "full ring did not drop");
        std::vector<float> out(1000);
        require(capture.popMaster(out.data(), out.data(), 1000) == 1000 && out[0] == 0.25f, "oldest block was not kept");

        juce::WavAudioFormat wav;
        auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("lighthost-record-test.wav");
        auto stream = std::make_unique<juce::FileOutputStream>(file);
        auto writer = std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(stream.release(), 48000.0, 2, 24, {}, 0));
        require(writer != nullptr, "24-bit writer");
        juce::AudioBuffer<float> audio(2, 32);
        audio.setSample(0, 0, 0.5f);
        writer->writeFromAudioSampleBuffer(audio, 0, 32);
        writer.reset();
        auto reader = std::unique_ptr<juce::AudioFormatReader>(wav.createReaderFor(new juce::FileInputStream(file), true));
        require(reader && reader->numChannels == 2 && reader->sampleRate == 48000.0 && reader->bitsPerSample == 24, "wav header");
        juce::AudioBuffer<float> back(2, 32);
        reader->read(&back, 0, 32, 0, true, true);
        require(std::abs(back.getSample(0, 0) - 0.5f) < 0.001f, "wav sample");
        file.deleteFile();

        auto wideStream = std::make_unique<juce::FileOutputStream>(file);
        auto wide = std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(wideStream.release(), 48000.0, 8, 24, {}, 0));
        require(wide != nullptr, "8 channel wav");
        wide.reset();
        file.deleteFile();

        require(std::abs(mackieFaderToDb(0) + 60.0f) < 0.001f && std::abs(mackieFaderToDb(16383) - 12.0f) < 0.02f, "fader ends");
        require(dbToMackieFader(-60.0f) == 0 && dbToMackieFader(12.0f) == 16383, "fader reverse ends");
        require(std::abs(dbToMackieFader(mackieFaderToDb(1000)) - 1000) <= 1, "fader round trip");
        require(nextBank(23, 192, 1) == 0, "bank wrap");
        require(applyVpot(0.0f, 1) > 0.0f && applyVpot(0.0f, 65) < 0.0f, "vpot direction");

        const auto request = icecastRequest(true, "live", "source", "secret-password", "Show");
        require(request.startsWith("PUT ") && request.contains("audio/mpeg") && request.contains("Basic "), "icecast request");
        require(!icecastLogLine(true).contains("secret-password") && !icecastLogLine(false).contains("secret-password"), "password stayed out of the log");
        std::cout << "Record, stream, and Mackie helpers passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
