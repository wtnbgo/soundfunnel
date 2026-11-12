// SPDX-License-Identifier: GPL-3.0-or-later
#if defined (_WIN32)
 #define NOMINMAX
 #include <winsock2.h>
 #include <ws2tcpip.h>
#else
 #include <sys/socket.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>
#endif

#include "AudioEngine.h"
#include "Codecs.h"
#include "aoo/aoo_opus.h"
#include "aoo/aoo_pcm.h"

using namespace juce;

namespace
{
    constexpr int kPacketSize = 1400;        // LAN: larger packets, less overhead
    constexpr uint32 kAliveTimeoutMs = 3000;

    struct AooInit
    {
        AooInit()  { aoo_initialize(); }
        ~AooInit() { aoo_terminate(); }
    };
}

//==============================================================================
struct AudioEngine::Endpoint
{
    AudioEngine* owner = nullptr;
    String ip;
    int port = 0;
    sockaddr_in addr {};
};

struct AudioEngine::Sink
{
    String nodeId;
    int32_t id = 0;
    Endpoint* endpoint = nullptr;
    aoo::isink::pointer sink;
    std::atomic<bool> muted { false };
    std::atomic<float> gain { 1.0f };
    float currentGain = 0.0f;                 // audio thread only (ramped)
    std::atomic<float> level { 0.0f };
    std::atomic<uint32> lastAudioMs { 0 };
};

static int32_t endpointSend (void* e, const char* data, int32_t size)
{
    auto* ep = static_cast<AudioEngine::Endpoint*> (e);
    return ep->owner != nullptr ? ep->owner->sendRaw (*ep, data, size) : -1;
}

//==============================================================================
class AudioEngine::NetThread : public Thread
{
public:
    NetThread (const String& name, std::function<void()> body) : Thread (name), fn (std::move (body)) {}
    void run() override
    {
        while (! threadShouldExit()) fn();
    }
    std::function<void()> fn;
};

//==============================================================================
AudioEngine::AudioEngine()
{
    static AooInit init;
    formatIndex = Codecs::defaultIndex();
}

AudioEngine::~AudioEngine()
{
    if (recvThread) recvThread->signalThreadShouldExit();
    if (sendThread) sendThread->signalThreadShouldExit();
    sendEvent.signal();
    if (socket) socket->shutdown();
    if (recvThread) recvThread->stopThread (2000);
    if (sendThread) sendThread->stopThread (2000);
}

int32_t AudioEngine::idForNode (const String& nodeId)
{
    // stable, positive id shared by a client's source and the server's sink for it
    return (int32_t) ((uint32) nodeId.hashCode64() & 0x3fffffff) + 1;
}

int AudioEngine::open (int port)
{
    socket = std::make_unique<DatagramSocket> (false);
    if (! socket->bindToPort (port) && ! (port != 0 && socket->bindToPort (0)))
    {
        socket.reset();
        return boundPort = -1;
    }
    boundPort = socket->getBoundPort();
   #ifdef SO_NOSIGPIPE
    {
        int one = 1;
        setsockopt (socket->getRawSocketHandle(), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof (one));
    }
   #endif

    // bigger OS buffers help the server when many clients stream at once
    int sz = 1 << 20;
    setsockopt ((decltype (socket->getRawSocketHandle())) socket->getRawSocketHandle(), SOL_SOCKET, SO_RCVBUF, (const char*) &sz, sizeof (sz));

    recvThread = std::make_unique<NetThread> ("SoundFunnel recv", [this] {
        if (socket->waitUntilReady (true, 20) == 1) doReceive();
    });
    sendThread = std::make_unique<NetThread> ("SoundFunnel send", [this] {
        sendEvent.wait (20);
        doSend();
        handleEvents();
    });
    recvThread->startThread (Thread::Priority::highest);
    sendThread->startThread (Thread::Priority::highest);
    return boundPort;
}

int AudioEngine::sendRaw (const Endpoint& ep, const char* data, int size)
{
    if (socket == nullptr) return -1;
    ++tx;
    return (int) ::sendto ((decltype (socket->getRawSocketHandle())) socket->getRawSocketHandle(),
                           data, (size_t) size, 0, (const sockaddr*) &ep.addr, sizeof (ep.addr));
}

AudioEngine::Endpoint* AudioEngine::endpointFor (const String& ip, int port)
{
    for (auto* e : endpoints)
        if (e->port == port && e->ip == ip)
            return e;

    auto* e = endpoints.add (new Endpoint());
    e->owner = this;
    e->ip = ip;
    e->port = port;
    e->addr.sin_family = AF_INET;
    e->addr.sin_port = htons ((uint16) port);
    inet_pton (AF_INET, ip.toRawUTF8(), &e->addr.sin_addr);
    return e;
}

//==============================================================================
void AudioEngine::setupSource()
{
    if (! source) return;
    aoo_format_storage f;
    Codecs::toAooFormat (formatIndex, sendChannels, sampleRate, blockSize, f);
    source->setup ((int32_t) sampleRate, blockSize, sendChannels);
    source->set_format (f.header);
    source->set_buffersize (jmax (10, (int) (4000.0 * blockSize / sampleRate)));
    source->set_packetsize (kPacketSize);
    sendBuffer.setSize (sendChannels, blockSize, false, true, true);
}

void AudioEngine::setupSink (Sink& s)
{
    s.sink->setup ((int32_t) sampleRate, blockSize, outChannels);
    s.sink->set_buffersize (jitterMs);
    s.sink->set_packetsize (kPacketSize);
    int32_t flags = AOO_PROTOCOL_FLAG_COMPACT_DATA;
    s.sink->set_option (aoo_opt_protocol_flags, &flags, sizeof (flags));
    int32_t resample = 1;
    s.sink->set_option (aoo_opt_dynamic_resampling, &resample, sizeof (resample));
}

void AudioEngine::setIdle()
{
    const ScopedWriteLock sl (coreLock);
    role = Role::Idle;
    if (source) source->remove_all();
    source.reset();
    serverEndpoint = nullptr;
    sinks.clear();
}

void AudioEngine::setClient (const String& myNodeId, const String& serverIp, int serverPort)
{
    const ScopedWriteLock sl (coreLock);
    sinks.clear();

    auto id = idForNode (myNodeId);
    auto* ep = endpointFor (serverIp, serverPort);

    if (role == Role::Client && source && ep == serverEndpoint && id == sourceSinkId)
        return;

    role = Role::Client;
    if (! source)
    {
        source.reset (aoo::isource::create (id));
        setupSource();
        sourcePlaying = false;
    }
    source->remove_all();
    serverEndpoint = ep;
    sourceSinkId = id;
    source->add_sink (ep, id, endpointSend);
    // protocol flags are a per-sink option on the source side (as in SonoBus)
    int32_t flags = AOO_PROTOCOL_FLAG_COMPACT_DATA;
    source->set_sinkoption (ep, id, aoo_opt_protocol_flags, &flags, sizeof (flags));
    lastServerPingMs = 0;
}

void AudioEngine::setServer (const std::vector<ClientEndpoint>& clients)
{
    const ScopedWriteLock sl (coreLock);

    if (role != Role::Server)
    {
        if (source) source->remove_all();
        source.reset();
        serverEndpoint = nullptr;
        role = Role::Server;
    }

    // drop sinks for clients that went away or moved
    for (int i = sinks.size(); --i >= 0;)
    {
        auto* s = sinks[i];
        auto it = std::find_if (clients.begin(), clients.end(), [s] (auto& c) {
            return c.nodeId == s->nodeId && c.ip == s->endpoint->ip && c.port == s->endpoint->port;
        });
        if (it == clients.end()) sinks.remove (i);
    }

    for (auto& c : clients)
    {
        bool exists = false;
        for (auto* s : sinks) exists |= s->nodeId == c.nodeId;
        if (exists || c.port <= 0) continue;

        auto* s = sinks.add (new Sink());
        s->nodeId = c.nodeId;
        s->id = idForNode (c.nodeId);
        s->endpoint = endpointFor (c.ip, c.port);
        s->sink.reset (aoo::isink::create (s->id));
        setupSink (*s);
    }
}

void AudioEngine::setFormat (int index, int channels)
{
    const ScopedWriteLock sl (coreLock);
    formatIndex = index;
    sendChannels = jlimit (1, 2, channels);
    setupSource();
}

void AudioEngine::setJitterBufferMs (int ms)
{
    const ScopedWriteLock sl (coreLock);
    jitterMs = ms;
    for (auto* s : sinks) s->sink->set_buffersize (jitterMs);
}

void AudioEngine::setSendEnabled (bool b)    { sendEnabled = b; }
void AudioEngine::setMonitorLocal (bool b)   { monitorLocal = b; }

void AudioEngine::setClientMuted (const String& nodeId, bool m)
{
    const ScopedReadLock sl (coreLock);
    for (auto* s : sinks) if (s->nodeId == nodeId) s->muted = m;
}

void AudioEngine::setClientGain (const String& nodeId, float g)
{
    const ScopedReadLock sl (coreLock);
    for (auto* s : sinks) if (s->nodeId == nodeId) s->gain = g;
}

std::vector<AudioEngine::ClientStatus> AudioEngine::getClientStatus() const
{
    const ScopedReadLock sl (coreLock);
    std::vector<ClientStatus> r;
    auto now = Time::getMillisecondCounter();
    for (auto* s : sinks)
        r.push_back ({ s->nodeId, now - s->lastAudioMs.load() < kAliveTimeoutMs, s->level.load() });
    return r;
}

bool AudioEngine::isServerReachable() const
{
    auto t = lastServerPingMs.load();
    return t != 0 && Time::getMillisecondCounter() - t < kAliveTimeoutMs;
}

//==============================================================================
void AudioEngine::doReceive()
{
    char buf[AOO_MAXPACKETSIZE];
    String ip;
    int port = 0;
    auto n = socket->read (buf, (int) sizeof (buf), false, ip, port);
    if (n <= 0) return;
    ++rx;

    int32_t type = 0, id = 0;
    if (aoo_parse_pattern (buf, n, &type, &id) <= 0) { ++rxDropped; return; }

    const ScopedReadLock sl (coreLock);

    if (type == AOO_TYPE_SINK && role == Role::Server)
    {
        for (auto* s : sinks)
        {
            // only accept audio from the endpoint that discovery told us about.
            // compact data messages carry no id (AOO_ID_NONE): the endpoint identifies the sink.
            const bool idOk = id == s->id || id == AOO_ID_WILDCARD || id == AOO_ID_NONE;
            if (idOk && s->endpoint->port == port && s->endpoint->ip == ip)
            {
                s->sink->handle_message (buf, n, s->endpoint, endpointSend);
                ++rxRouted;
            }
        }
    }
    else if (type == AOO_TYPE_SOURCE && role == Role::Client && source && serverEndpoint != nullptr)
    {
        if (serverEndpoint->port == port && serverEndpoint->ip == ip)
        {
            source->handle_message (buf, n, serverEndpoint, endpointSend);
            ++rxRouted;
        }
    }
}

void AudioEngine::doSend()
{
    const ScopedReadLock sl (coreLock);
    if (source) source->send();
    for (auto* s : sinks) s->sink->send();
}

void AudioEngine::handleEvents()
{
    const ScopedReadLock sl (coreLock);

    if (source && source->events_available())
    {
        source->handle_events ([] (void* user, const aoo_event** events, int32_t n) -> int32_t {
            auto* self = static_cast<AudioEngine*> (user);
            for (int i = 0; i < n; ++i)
                if (events[i]->type == AOO_PING_EVENT)
                    self->lastServerPingMs = Time::getMillisecondCounter();
            return 1;
        }, this);
    }

    for (auto* s : sinks)
        if (s->sink->events_available())
            s->sink->handle_events ([] (void*, const aoo_event**, int32_t) -> int32_t { return 1; }, nullptr);
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (AudioIODevice* device)
{
    const ScopedWriteLock sl (coreLock);
    sampleRate = device->getCurrentSampleRate();
    blockSize = device->getCurrentBufferSizeSamples();
    outChannels = jmax (1, device->getActiveOutputChannels().countNumberOfSetBits());
    sinkBuffer.setSize (jmax (2, outChannels), blockSize, false, true, true);
    setupSource();
    sourcePlaying = false;
    for (auto* s : sinks) setupSink (*s);
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn,
                                                    float* const* out, int numOut,
                                                    int numSamples,
                                                    const AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr) FloatVectorOperations::clear (out[c], numSamples);

    ++callbacks;
    const ScopedTryReadLock sl (coreLock);
    if (! sl.isLocked() || numSamples > blockSize) return;

    auto t = aoo_osctime_get();

    if (role == Role::Client && source)
    {
        bool want = sendEnabled.load();
        if (want != sourcePlaying)
        {
            source->set_option (want ? aoo_opt_start : aoo_opt_stop, nullptr, 0);
            sourcePlaying = want;
        }

        // map device inputs to the send channel count
        float peak = 0.0f;
        for (int c = 0; c < sendChannels; ++c)
        {
            auto* dst = sendBuffer.getWritePointer (c);
            if (testTone.load())
            {
                auto ph = tonePhase;
                const auto inc = MathConstants<double>::twoPi * 440.0 / sampleRate;
                for (int i = 0; i < numSamples; ++i, ph += inc) dst[i] = 0.1f * (float) std::sin (ph);
                if (c == sendChannels - 1) tonePhase = std::fmod (ph, MathConstants<double>::twoPi);
            }
            else if (numIn == 0)
                FloatVectorOperations::clear (dst, numSamples);
            else if (sendChannels == 1 && numIn >= 2)
            {
                FloatVectorOperations::copyWithMultiply (dst, in[0], 0.5f, numSamples);
                FloatVectorOperations::addWithMultiply (dst, in[1], 0.5f, numSamples);
            }
            else
                FloatVectorOperations::copy (dst, in[jmin (c, numIn - 1)], numSamples);

            auto r = FloatVectorOperations::findMinAndMax (dst, numSamples);
            peak = jmax (peak, std::abs (r.getStart()), std::abs (r.getEnd()));
        }
        sendLevel = want ? jmax (peak, sendLevel.load() * 0.9f) : 0.0f;

        source->process ((const aoo_sample**) sendBuffer.getArrayOfReadPointers(), numSamples, t);
    }
    else
    {
        sendLevel = 0.0f;
    }

    if (role == Role::Server)
    {
        for (auto* s : sinks)
        {
            sinkBuffer.clear (0, numSamples);
            bool got = s->sink->process ((aoo_sample**) sinkBuffer.getArrayOfWritePointers(), numSamples, t) != 0;

            const float target = s->muted.load() ? 0.0f : s->gain.load();
            float peak = 0.0f;
            if (got)
            {
                s->lastAudioMs = Time::getMillisecondCounter();
                for (int c = 0; c < sinkBuffer.getNumChannels(); ++c)
                {
                    auto r = FloatVectorOperations::findMinAndMax (sinkBuffer.getReadPointer (c), numSamples);
                    peak = jmax (peak, std::abs (r.getStart()), std::abs (r.getEnd()));
                }
                for (int c = 0; c < jmin (numOut, sinkBuffer.getNumChannels()); ++c)
                {
                    if (out[c] == nullptr) continue;
                    auto* src = sinkBuffer.getReadPointer (c);

                    // linear ramp to avoid clicks on mute / gain changes
                    const float step = (target - s->currentGain) / (float) numSamples;
                    float g = s->currentGain;
                    for (int i = 0; i < numSamples; ++i, g += step)
                        out[c][i] += src[i] * g;
                }
            }
            s->currentGain = target;
            s->level = jmax (peak, s->level.load() * 0.9f);
        }

        if (monitorLocal.load())
            for (int c = 0; c < numOut; ++c)
                if (out[c] != nullptr && numIn > 0)
                    FloatVectorOperations::add (out[c], in[jmin (c, numIn - 1)], numSamples);
    }

    sendEvent.signal();
}
