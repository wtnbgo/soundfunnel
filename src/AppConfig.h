// spsync - aggregate audio from several PCs on a LAN to one server PC.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include "ServerClaim.h"

/** Persistent settings. One PropertiesFile per profile (profiles allow several
    instances on one machine for testing). All access from the message thread. */
class AppConfig
{
public:
    explicit AppConfig (const juce::String& profile);

    juce::String getProfile() const        { return profile; }

    juce::String getNodeId() const         { return nodeId; }

    juce::String getNodeName() const;
    void setNodeName (const juce::String&);

    juce::String getGroupKey() const;
    void setGroupKey (const juce::String&);

    int getAudioPort() const;              // 0 = let the OS choose
    void setAudioPort (int);

    int getFormatIndex() const;
    void setFormatIndex (int);

    int getSendChannels() const;           // 1 or 2
    void setSendChannels (int);

    int getJitterBufferMs() const;
    void setJitterBufferMs (int);

    bool getMonitorLocalWhenServer() const;
    void setMonitorLocalWhenServer (bool);

    bool getSendPaused() const;
    void setSendPaused (bool);

    ServerClaim getServerClaim() const;
    void setServerClaim (const ServerClaim&);

    // per-client state kept by the server, keyed by node id
    bool isClientMuted (const juce::String& id) const;
    void setClientMuted (const juce::String& id, bool);
    float getClientGain (const juce::String& id) const;
    void setClientGain (const juce::String& id, float);

    std::unique_ptr<juce::XmlElement> getAudioDeviceState() const;
    void setAudioDeviceState (const juce::XmlElement*);

    void save();

private:
    juce::String profile, nodeId;
    std::unique_ptr<juce::PropertiesFile> props;
};
