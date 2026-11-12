// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppCore.h"

using namespace juce;

AppCore::AppCore (const String& profile) : config (profile)
{
    // audio device: restore saved state or open defaults (2 in / 2 out)
    auto saved = config.getAudioDeviceState();
    auto err = deviceManager.initialise (2, 2, saved.get(), true);
    if (err.isNotEmpty()) DBG ("audio device: " << err);
    deviceManager.addAudioCallback (&engine);

    engine.setFormat (config.getFormatIndex(), config.getSendChannels());
    engine.setJitterBufferMs (config.getJitterBufferMs());
    engine.setMonitorLocal (config.getMonitorLocalWhenServer());

    if (engine.open (config.getAudioPort()) < 0)
        problem = TRANS ("Audio port could not be opened");

    discovery.setListener (this);
    pushLocalInfo();
    if (! discovery.start())
        problem = TRANS ("Discovery port is in use");

    reconcile();
}

AppCore::~AppCore()
{
    discovery.setListener (nullptr);
    discovery.stop();
    deviceManager.removeAudioCallback (&engine);
    deviceManager.closeAudioDevice();
    config.save();
}

ServerClaim AppCore::highestKnownClaim() const
{
    auto best = config.getServerClaim();
    for (auto& p : discovery.getPeers())
        if (best < p.claim) best = p.claim;
    return best;
}

void AppCore::pushLocalInfo()
{
    Discovery::LocalInfo li;
    li.nodeId    = config.getNodeId();
    li.name      = config.getNodeName();
    li.groupKey  = config.getGroupKey();
    li.audioPort = engine.getPort();
    li.claim     = config.getServerClaim();

    if (li.claim.nodeId == li.nodeId)
        for (auto& p : discovery.getPeers())
            if (config.isClientMuted (p.id))
                li.muted.add (p.id);

    discovery.setLocalInfo (li);
}

void AppCore::discoveryChanged() { reconcile(); }

void AppCore::reconcile()
{
    // adopt the most recent claim anyone knows about
    auto claim = highestKnownClaim();
    if (claim != config.getServerClaim())
    {
        config.setServerClaim (claim);
        config.save();
    }
    pushLocalInfo();

    const auto me = config.getNodeId();
    const auto peers = discovery.getPeers();
    mutedByServer = false;

    if (config.getGroupKey().isEmpty())
    {
        role = Role::NoGroup;
        engine.setIdle();
    }
    else if (! claim.isValid())
    {
        role = Role::NoServer;
        engine.setIdle();
    }
    else if (claim.nodeId == me)
    {
        role = Role::Server;
        std::vector<AudioEngine::ClientEndpoint> clients;
        for (auto& p : peers)
            clients.push_back ({ p.id, p.name, p.ip, p.audioPort });
        engine.setServer (clients);
        for (auto& p : peers)
        {
            engine.setClientMuted (p.id, config.isClientMuted (p.id));
            engine.setClientGain (p.id, config.getClientGain (p.id));
        }
    }
    else
    {
        auto it = std::find_if (peers.begin(), peers.end(), [&] (auto& p) { return p.id == claim.nodeId; });
        if (it == peers.end() || it->audioPort <= 0)
        {
            role = Role::ServerOffline;
            engine.setIdle();
        }
        else
        {
            role = Role::Client;
            mutedByServer = it->muted.contains (me);
            engine.setClient (me, it->ip, it->audioPort);
            engine.setSendEnabled (! mutedByServer && ! config.getSendPaused());
        }
    }

    sendChangeMessage();
}

String AppCore::getServerName() const
{
    auto claim = config.getServerClaim();
    if (claim.nodeId == config.getNodeId()) return config.getNodeName();
    for (auto& p : discovery.getPeers())
        if (p.id == claim.nodeId) return p.name;
    return {};
}

String AppCore::getStatusText() const
{
    if (problem.isNotEmpty()) return problem;
    switch (role)
    {
        case Role::NoGroup:       return TRANS ("Set a group key to start");
        case Role::NoServer:      return TRANS ("No server yet");
        case Role::ServerOffline: return TRANS ("Server is offline");
        case Role::Server:        return TRANS ("This PC is the server");
        case Role::Client:
        {
            String s = TRANS ("Server: ") + getServerName();
            if (mutedByServer)                 s << " - " << TRANS ("Muted by the server");
            else if (config.getSendPaused())   s << " - " << TRANS ("Paused");
            else                               s << " - " << (engine.isServerReachable() ? TRANS ("Connected") : TRANS ("Connecting..."));
            return s;
        }
    }
    return {};
}

std::vector<AppCore::Member> AppCore::getMembers() const
{
    const auto claim = config.getServerClaim();
    std::vector<Member> r;

    Member self;
    self.id = config.getNodeId();
    self.name = config.getNodeName();
    self.isSelf = true;
    self.isServer = claim.nodeId == self.id;
    self.level = engine.getSendLevel();
    r.push_back (self);

    auto status = engine.getClientStatus();
    for (auto& p : discovery.getPeers())
    {
        Member m;
        m.id = p.id;
        m.name = p.name;
        m.ip = p.ip;
        m.isServer = claim.nodeId == p.id;
        m.muted = config.isClientMuted (p.id);
        m.gain = config.getClientGain (p.id);
        for (auto& s : status)
            if (s.nodeId == p.id) { m.receiving = s.receiving; m.level = s.level; }
        r.push_back (m);
    }
    return r;
}

void AppCore::becomeServer()
{
    auto c = highestKnownClaim();
    if (c.nodeId == config.getNodeId() && role == Role::Server) return;
    config.setServerClaim ({ c.epoch + 1, config.getNodeId() });
    config.save();
    reconcile();
    discovery.sendBeaconSoon();
}

void AppCore::setPaused (bool p)
{
    config.setSendPaused (p);
    config.save();
    reconcile();
}

void AppCore::setClientMuted (const String& id, bool m)
{
    config.setClientMuted (id, m);
    config.save();
    engine.setClientMuted (id, m);
    pushLocalInfo();              // tells the client to stop sending
    discovery.sendBeaconSoon();
    sendChangeMessage();
}

void AppCore::setClientGain (const String& id, float g)
{
    config.setClientGain (id, g);
    engine.setClientGain (id, g);
}

void AppCore::applyIdentity (const String& name, const String& key)
{
    config.setNodeName (name.trim());
    if (key != config.getGroupKey())
        config.setServerClaim ({});   // claims from another group mean nothing here
    config.setGroupKey (key);
    config.save();
    reconcile();
}

void AppCore::applyFormat (int formatIndex, int channels, int jitterMs, bool monitorLocal)
{
    config.setFormatIndex (formatIndex);
    config.setSendChannels (channels);
    config.setJitterBufferMs (jitterMs);
    config.setMonitorLocalWhenServer (monitorLocal);
    config.save();
    engine.setFormat (formatIndex, channels);
    engine.setJitterBufferMs (jitterMs);
    engine.setMonitorLocal (monitorLocal);
}

void AppCore::saveAudioDeviceState()
{
    auto xml = deviceManager.createStateXml();
    config.setAudioDeviceState (xml.get());
    config.save();
}
