// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppConfig.h"
#include "Codecs.h"

using namespace juce;

AppConfig::AppConfig (const String& p) : profile (p)
{
    PropertiesFile::Options o;
    o.applicationName     = profile.isEmpty() ? "SoundFunnel" : "SoundFunnel-" + profile;
    o.filenameSuffix      = ".settings";
   #if JUCE_LINUX || JUCE_BSD
    o.folderName          = ".config/SoundFunnel";   // JUCE resolves this relative to $HOME on Linux
   #else
    o.folderName          = "SoundFunnel";
   #endif
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat       = PropertiesFile::storeAsXML;
    props = std::make_unique<PropertiesFile> (o);

    nodeId = props->getValue ("nodeId");
    if (nodeId.isEmpty())
    {
        nodeId = Uuid().toDashedString();
        props->setValue ("nodeId", nodeId);
        props->saveIfNeeded();
    }
}

String AppConfig::getNodeName() const
{
    auto n = props->getValue ("nodeName");
    if (n.isEmpty())
    {
        n = SystemStats::getComputerName();
        if (profile.isNotEmpty()) n << " (" << profile << ")";
    }
    return n;
}
void AppConfig::setNodeName (const String& v)        { props->setValue ("nodeName", v); }

String AppConfig::getGroupKey() const                { return props->getValue ("groupKey"); }
void AppConfig::setGroupKey (const String& v)        { props->setValue ("groupKey", v); }

int AppConfig::getAudioPort() const                  { return props->getIntValue ("audioPort", profile.isEmpty() ? 47701 : 0); }
void AppConfig::setAudioPort (int v)                 { props->setValue ("audioPort", v); }

int AppConfig::getFormatIndex() const                { return props->getIntValue ("formatIndex", Codecs::defaultIndex()); }
void AppConfig::setFormatIndex (int v)               { props->setValue ("formatIndex", v); }

int AppConfig::getSendChannels() const               { return jlimit (1, 2, props->getIntValue ("sendChannels", 2)); }
void AppConfig::setSendChannels (int v)              { props->setValue ("sendChannels", jlimit (1, 2, v)); }

int AppConfig::getJitterBufferMs() const             { return jlimit (5, 1000, props->getIntValue ("jitterBufferMs", 40)); }
void AppConfig::setJitterBufferMs (int v)            { props->setValue ("jitterBufferMs", v); }

bool AppConfig::getMonitorLocalWhenServer() const    { return props->getBoolValue ("monitorLocal", false); }
void AppConfig::setMonitorLocalWhenServer (bool v)   { props->setValue ("monitorLocal", v); }

AppConfig::Source AppConfig::getSendSource() const   { return props->getIntValue ("sendSource", 0) == 1 ? Source::loopback : Source::input; }
void AppConfig::setSendSource (Source s)             { props->setValue ("sendSource", (int) s); }
String AppConfig::getLoopbackDevice() const          { return props->getValue ("loopbackDevice"); }
void AppConfig::setLoopbackDevice (const String& d)  { props->setValue ("loopbackDevice", d); }

bool AppConfig::getSendPaused() const                { return props->getBoolValue ("sendPaused", false); }
void AppConfig::setSendPaused (bool v)               { props->setValue ("sendPaused", v); }

ServerClaim AppConfig::getServerClaim() const
{
    ServerClaim c;
    c.epoch  = props->getValue ("claimEpoch", "0").getLargeIntValue();
    c.nodeId = props->getValue ("claimNode");
    return c;
}
void AppConfig::setServerClaim (const ServerClaim& c)
{
    props->setValue ("claimEpoch", String (c.epoch));
    props->setValue ("claimNode", c.nodeId);
}

bool AppConfig::isClientMuted (const String& id) const      { return props->getBoolValue ("mute." + id, false); }
void AppConfig::setClientMuted (const String& id, bool v)   { props->setValue ("mute." + id, v); }
float AppConfig::getClientGain (const String& id) const     { return (float) props->getDoubleValue ("gain." + id, 1.0); }
void AppConfig::setClientGain (const String& id, float v)   { props->setValue ("gain." + id, v); }

std::unique_ptr<XmlElement> AppConfig::getAudioDeviceState() const { return props->getXmlValue ("audioDevice"); }
void AppConfig::setAudioDeviceState (const XmlElement* x)
{
    if (x != nullptr) props->setValue ("audioDevice", x);
}

void AppConfig::save() { props->saveIfNeeded(); }
