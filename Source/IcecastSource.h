#pragma once
#include <juce_core/juce_core.h>
#include <functional>

inline juce::String icecastRequest(bool put, juce::String mount, const juce::String& user, const juce::String& password, const juce::String& name)
{
    if (!mount.startsWithChar('/')) mount = "/" + mount;
    const auto token = juce::Base64::toBase64(user + ":" + password);
    return juce::String(put ? "PUT " : "SOURCE ") + mount + " HTTP/1.0\r\n"
        "Authorization: Basic " + token + "\r\n"
        "Content-Type: audio/mpeg\r\n"
        "Ice-Name: " + name + "\r\n\r\n";
}

inline juce::String icecastLogLine(bool ok)
{
    return ok ? "Icecast connected" : "Icecast failed";
}

class IcecastSource
{
public:
    bool connect(const juce::String& host, int port, const juce::String& mount, const juce::String& user, const juce::String& password, const juce::String& name);
    bool write(const void* data, size_t bytes);
    void close();
    bool connected() const noexcept { return live; }

private:
    bool send(bool put);
    juce::StreamingSocket socket;
    juce::String host, mount, user, password, streamName;
    int port = 8000;
    bool live = false;
};
