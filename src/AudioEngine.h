// SPDX-License-Identifier: GPL-3.0-or-later
// The AOO send/receive structure follows SonoBus's SonobusAudioProcessor
// (Copyright (C) Jesse Chappell, GPLv3), reduced to one direction:
// every client streams its input to the server, and only the server plays.
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "aoo/aoo.hpp"
#include "LoopbackCapture.h"

class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    struct ClientEndpoint
    {
        juce::String nodeId, name, ip;
        int port = 0;
    };

    struct ClientStatus
    {
        juce::String nodeId;
        bool receiving = false;   // audio arrived recently
        float level = 0.0f;       // peak, linear
    };

    AudioEngine();
    ~AudioEngine() override;

    /** Opens the AOO UDP socket. port 0 = any free port. Returns the bound port, or -1. */
    int open (int port);
    int getPort() const { return boundPort; }

    // --- role (message thread) -----------------------------------------------
    void setIdle();
    void setClient (const juce::String& myNodeId, const juce::String& serverIp, int serverPort);
    void setServer (const std::vector<ClientEndpoint>& clients);

    // --- settings (message thread) -------------------------------------------
    void setFormat (int formatIndex, int sendChannels);
    void setJitterBufferMs (int ms);
    void setSendEnabled (bool);                         // false = paused locally or muted by the server
    void setMonitorLocal (bool);
    void setTestTone (bool b)           { testTone = b; }   // send a sine instead of the input (testing)
    void setClientMuted (const juce::String& nodeId, bool);
    void setClientGain (const juce::String& nodeId, float);

    /** Client: send what this PC plays (system audio) instead of the input device.
        deviceName empty = the default output. */
    void setLoopback (bool enabled, const juce::String& deviceName);
    bool isLoopbackEnabled() const      { return loopbackOn.load(); }
    juce::String getLoopbackStatus() const;   // device description, or the error; empty when off
    juce::String getLoopbackError() const     { return loopbackOn.load() ? loopback.getLastError() : juce::String(); }

    // --- status (any thread) -------------------------------------------------
    std::vector<ClientStatus> getClientStatus() const;
    float getSendLevel() const          { return sendLevel.load(); }
    bool isServerReachable() const;     // client: pings from the server sink arrived recently

    struct Counters { juce::uint64 rx = 0, rxRouted = 0, rxDropped = 0, tx = 0, callbacks = 0; };
    Counters getCounters() const { return { rx.load(), rxRouted.load(), rxDropped.load(), tx.load(), callbacks.load() }; }

    static int32_t idForNode (const juce::String& nodeId);

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn,
                                           float* const* out, int numOut,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    struct Endpoint;
    int sendRaw (const Endpoint&, const char* data, int size);   // used by the AOO reply callback

private:
    struct Sink;
    class NetThread;

    Endpoint* endpointFor (const juce::String& ip, int port);   // call with write lock held
    void setupSource();                                         // call with write lock held
    void setupSink (Sink&);                                     // call with write lock held
    void processClientBlock (const float* const* in, int numIn, int numSamples, uint64_t t);   // read lock held
    void onLoopbackAudio (const float* const* data, int numChannels, int numSamples, double sr);
    void doReceive();
    void doSend();
    void handleEvents();
    void retryServerHandshake();                                // send thread

    enum class Role { Idle, Client, Server };

    juce::ReadWriteLock coreLock;
    Role role = Role::Idle;

    std::unique_ptr<juce::DatagramSocket> socket;
    int boundPort = -1;
    juce::OwnedArray<Endpoint> endpoints;    // never removed while running (sinks keep raw pointers)

    // client side
    aoo::isource::pointer source;
    Endpoint* serverEndpoint = nullptr;
    int32_t sourceSinkId = 0;
    std::atomic<bool> sendEnabled { true };
    bool sourcePlaying = false;
    int stopBlocksLeft = 0;                             // audio thread only
    static constexpr int kStopFlushBlocks = 16;
    std::atomic<juce::uint32> lastServerPingMs { 0 };
    std::atomic<juce::uint32> lastSinkAddMs { 0 };
    juce::AudioBuffer<float> sendBuffer;

    // server side
    juce::OwnedArray<Sink> sinks;
    juce::AudioBuffer<float> sinkBuffer;
    std::atomic<bool> monitorLocal { false };
    std::atomic<bool> testTone { false };
    double tonePhase = 0.0;

    // shared settings
    int formatIndex = 0, sendChannels = 2, jitterMs = 40;
    double sampleRate = 48000.0;          // audio device
    double sourceRate = 48000.0;          // what the AOO source is set up for (device or loopback)
    int blockSize = 512;
    int outChannels = 2;

    // loopback (system audio) source: its own thread drives the AOO source
    LoopbackCapture loopback;
    std::atomic<bool> loopbackOn { false };
    juce::String loopbackDevice;
    double lbRate = 0.0;                    // 0 until the first audio arrives
    int lbBlock = 480;
    juce::AudioBuffer<float> lbAccum;
    int lbFill = 0;

    std::atomic<float> sendLevel { 0.0f };
    std::atomic<juce::uint64> rx { 0 }, rxRouted { 0 }, rxDropped { 0 }, tx { 0 }, callbacks { 0 };

    juce::WaitableEvent sendEvent;
    std::unique_ptr<NetThread> recvThread, sendThread;
};
