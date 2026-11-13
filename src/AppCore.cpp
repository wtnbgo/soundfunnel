// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppCore.h"
#include "MicPermission.h"
#include "DefaultDevices.h"

using namespace juce;

AppCore::AppCore (const Options& options) : config (options.profile), opts (options)
{
    deviceManager.addAudioCallback (&engine);

    engine.setFormat (config.getFormatIndex(), config.getSendChannels());
    engine.setJitterBufferMs (config.getJitterBufferMs());
    engine.setMonitorLocal (config.getMonitorLocalWhenServer());
    applySendSource (config.getSendSource(), config.getLoopbackDevice(), false);

    if (engine.open (config.getAudioPort()) < 0)
        problem = TRANS ("Audio port could not be opened");

    discovery.setListener (this);
    pushLocalInfo();
    if (! discovery.start())
        problem = TRANS ("Discovery port is in use");

    reconcile();

    // The audio device is opened once microphone access is settled (see MicPermission.h).
    // Discovery and the UI run meanwhile, so a pending prompt never freezes the app.
    if (MicPermission::current() == MicPermission::Status::undetermined)
        Logger::writeToLog ("waiting for microphone permission");

    std::weak_ptr<bool> weak = alive;
    MicPermission::request ([this, weak] (bool granted)
    {
        if (weak.expired()) return;
        if (! granted) Logger::writeToLog ("microphone permission denied: input stays closed");
        micSettled = true;
        micAllowed = granted;
        openAudioDeviceWhenReady();
    });
}

void AppCore::openAudioDeviceWhenReady()
{
    if (audioOpened || ! micSettled) return;

   #if JUCE_MAC
    // The system audio tap creates a private aggregate device. JUCE rescans the device list on
    // the message thread when it appears, and querying that device blocks inside CoreAudio
    // until the "System Audio Recording" prompt is answered, freezing the whole app.
    // So the CoreAudio device layer is only created once the capture has started (or failed).
    if (! engine.isLoopbackSettled())
    {
        if (! isTimerRunning())
        {
            Logger::writeToLog ("waiting for system audio capture to start");
            startTimer (200);
        }
        return;
    }
   #endif

    stopTimer();
    openAudioDevice (micAllowed);
}

void AppCore::timerCallback()
{
    openAudioDeviceWhenReady();
}

void AppCore::openAudioDevice (bool micAllowed)
{
    // restore the saved state or open defaults (2 in / 2 out);
    // with 0 channels requested JUCE ignores the saved device of that direction
    auto saved = config.getAudioDeviceState();
    // sending system audio needs no input device (and no microphone access)
    const bool in = opts.openInput && micAllowed && ! isLoopbackSource();
    auto err = deviceManager.initialise (in ? 2 : 0, opts.openOutput ? 2 : 0, saved.get(), true);
    if (err.isNotEmpty()) Logger::writeToLog ("audio device: " + err);

    if (auto* dev = deviceManager.getCurrentAudioDevice())
        Logger::writeToLog ("audio device: " + dev->getName() + " " + String (dev->getCurrentSampleRate()) + " Hz");
    else
        Logger::writeToLog ("audio device: none");

    audioOpened = true;

    if (DefaultDevices::isSupported())
    {
        applyDefaultDevices();
        defaultWatcher.tick = [this] { applyDefaultDevices(); };
        defaultWatcher.startTimer (2000);
    }
    sendChangeMessage();
}

void AppCore::applyDefaultDevices()
{
    const bool followIn  = config.getFollowDefaultDevice (true);
    const bool followOut = config.getFollowDefaultDevice (false);
    if (! audioOpened || (! followIn && ! followOut)) return;

    // device names only match the WASAPI shared-mode type
    if (deviceManager.getCurrentAudioDeviceType() != DefaultDevices::deviceTypeName()) return;

    auto setup = deviceManager.getAudioDeviceSetup();
    bool changed = false;

    const bool wantInput = opts.openInput && micAllowed && ! isLoopbackSource();
    if (followIn && wantInput)
    {
        auto name = DefaultDevices::getName (true);
        if (name.isNotEmpty() && setup.inputDeviceName != name)
        {
            setup.inputDeviceName = name;
            setup.useDefaultInputChannels = true;
            changed = true;
        }
        appliedDefaultIn = name.isNotEmpty() ? name : setup.inputDeviceName;
    }
    if (followOut && opts.openOutput)
    {
        auto name = DefaultDevices::getName (false);
        if (name.isNotEmpty() && setup.outputDeviceName != name)
        {
            setup.outputDeviceName = name;
            setup.useDefaultOutputChannels = true;
            changed = true;
        }
        appliedDefaultOut = name.isNotEmpty() ? name : setup.outputDeviceName;
    }

    if (changed)
    {
        Logger::writeToLog ("following the default device: in \"" + setup.inputDeviceName + "\", out \"" + setup.outputDeviceName + "\"");
        auto err = deviceManager.setAudioDeviceSetup (setup, true);
        if (err.isNotEmpty()) Logger::writeToLog ("audio device: " + err);
        saveAudioDeviceState();
    }
}

void AppCore::setFollowDefaultDevice (bool input, bool on)
{
    config.setFollowDefaultDevice (input, on);
    config.save();
    (input ? appliedDefaultIn : appliedDefaultOut).clear();
    applyDefaultDevices();
    sendChangeMessage();
}

AppCore::~AppCore()
{
    stopTimer();
    defaultWatcher.stopTimer();
    alive.reset();
    discovery.setListener (nullptr);
    discovery.stop();
    deviceManager.removeAudioCallback (&engine);
    deviceManager.closeAudioDevice();
    config.save();
}

ServerClaim AppCore::highestKnownClaim() const
{
    auto best = config.getServerClaim();
    if (! best.isValid()) best = {};
    const auto own = best;

    for (auto& p : discovery.getPeers())
    {
        if (! p.claim.isValid()) continue;
        // don't let one authenticated peer push the epoch far ahead of the group (it would
        // stay adopted and saved everywhere); real claims only grow by one per press
        if (own.isValid() && p.claim.epoch > own.epoch + ServerClaim::kMaxEpochJump) continue;
        if (best < p.claim) best = p.claim;
    }
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
            const auto lbErr = engine.getLoopbackError();
            if (mutedByServer)                 s << " - " << TRANS ("Muted by the server");
            else if (lbErr.isNotEmpty())       s << " - " << TRANS ("System audio: ") << lbErr;
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
    self.muted = mutedByServer;   // a client's own row shows the server's mute
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
    config.setServerClaim ({ jmin (c.epoch, ServerClaim::kMaxEpoch - 1) + 1, config.getNodeId() });
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
    const bool keyChanged = key != config.getGroupKey();
    if (keyChanged)
        config.setServerClaim ({});   // claims from another group mean nothing here
    config.setGroupKey (key);
    config.save();
    // hand the new key to discovery first: that drops the old group's peers, so reconcile()
    // can't pick up (and save) a claim that was only valid in the old group
    if (keyChanged) pushLocalInfo();
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

void AppCore::applySendSource (AppConfig::Source src, const String& device, bool persist)
{
    if (persist)
    {
        config.setSendSource (src);
        config.setLoopbackDevice (device);
        config.save();
    }
    engine.setLoopback (src == AppConfig::Source::loopback && LoopbackCapture::isSupported(), device);
    sendChangeMessage();
}

bool AppCore::isLoopbackSource() const
{
    return engine.isLoopbackEnabled();
}

void AppCore::saveAudioDeviceState()
{
    // a device picked by hand (not by applyDefaultDevices) ends following for that direction
    auto setup = deviceManager.getAudioDeviceSetup();
    for (bool input : { true, false })
    {
        const auto& applied = input ? appliedDefaultIn : appliedDefaultOut;
        const auto& current = input ? setup.inputDeviceName : setup.outputDeviceName;
        if (config.getFollowDefaultDevice (input) && applied.isNotEmpty() && current.isNotEmpty() && current != applied)
        {
            config.setFollowDefaultDevice (input, false);
            Logger::writeToLog (String (input ? "input" : "output") + " device chosen by hand: no longer following the default");
            sendChangeMessage();
        }
    }

    auto xml = deviceManager.createStateXml();
    config.setAudioDeviceState (xml.get());
    config.save();
}
