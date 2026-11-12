// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include "AppConfig.h"
#include "AudioEngine.h"
#include "Discovery.h"

/** Ties discovery, server election and the audio engine together.
    Lives on the message thread; UI listens via ChangeBroadcaster. */
class AppCore : public juce::ChangeBroadcaster,
                private Discovery::Listener
{
public:
    enum class Role { NoGroup, NoServer, ServerOffline, Client, Server };

    struct Member
    {
        juce::String id, name, ip;
        bool isSelf = false, isServer = false;
        bool muted = false;        // server-side mute
        float gain = 1.0f;
        bool receiving = false;    // server: audio from this member is arriving
        float level = 0.0f;
    };

    struct Options
    {
        juce::String profile;
        bool openInput = true;      // false: never open an input device (no microphone prompt)
        bool openOutput = true;     // false: never open an output device
    };

    explicit AppCore (const Options&);
    ~AppCore() override;

    AppConfig& getConfig()                          { return config; }
    juce::AudioDeviceManager& getDeviceManager()    { return deviceManager; }
    AudioEngine& getEngine()                        { return engine; }
    const Discovery& getDiscovery() const           { return discovery; }

    Role getRole() const                            { return role; }
    juce::String getServerName() const;
    juce::String getStatusText() const;
    juce::String getProblem() const                 { return problem; }
    bool isMutedByServer() const                    { return mutedByServer; }

    std::vector<Member> getMembers() const;

    void becomeServer();
    void setPaused (bool);
    bool isPaused() const                           { return config.getSendPaused(); }

    void setClientMuted (const juce::String& id, bool);
    void setClientGain (const juce::String& id, float);

    // settings changed in the UI
    void applyIdentity (const juce::String& name, const juce::String& groupKey);
    void applyFormat (int formatIndex, int channels, int jitterMs, bool monitorLocal);
    /** persist = false: only for this run (command line testing). */
    void applySendSource (AppConfig::Source, const juce::String& loopbackDevice, bool persist = true);
    bool isLoopbackSource() const;
    void saveAudioDeviceState();
    bool isAudioDeviceOpened() const                { return audioOpened; }

private:
    void discoveryChanged() override;
    void reconcile();
    void pushLocalInfo();
    ServerClaim highestKnownClaim() const;
    void openAudioDevice (bool micAllowed);

    AppConfig config;
    Options opts;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    bool audioOpened = false;
    juce::AudioDeviceManager deviceManager;
    AudioEngine engine;
    Discovery discovery;

    Role role = Role::NoGroup;
    bool mutedByServer = false;
    juce::String problem;
};
