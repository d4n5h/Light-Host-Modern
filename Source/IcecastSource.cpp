#include "IcecastSource.h"

bool IcecastSource::connect(const juce::String& hostIn, int portIn, const juce::String& mountIn, const juce::String& userIn, const juce::String& passwordIn, const juce::String& nameIn)
{
    close();
    host = hostIn;
    port = portIn;
    mount = mountIn;
    user = userIn;
    password = passwordIn;
    streamName = nameIn;
    if (send(true) || send(false)) { live = true; return true; }
    close();
    return false;
}

bool IcecastSource::send(bool put)
{
    socket.close();
    if (!socket.connect(host, port, 3000)) return false;
    const auto request = icecastRequest(put, mount, user, password, streamName);
    if (!socket.write(request.toRawUTF8(), (int) request.getNumBytesAsUTF8())) return false;
    char reply[512] = {};
    const int got = socket.read(reply, (int) sizeof(reply) - 1, false);
    if (got <= 0) return false;
    const auto text = juce::String(reply);
    return text.contains("200") || text.contains("100");
}

bool IcecastSource::write(const void* data, size_t bytes)
{
    if (!live || bytes == 0) return live;
    const int wrote = socket.write(data, (int) bytes);
    if (wrote != (int) bytes) { live = false; return false; }
    return true;
}

void IcecastSource::close()
{
    live = false;
    socket.close();
}
