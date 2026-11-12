// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "ServerClaim.h"

/** LAN peer discovery.

    Every node sends a small beacon once per second to a multicast group and to
    the directed broadcast address of every IPv4 interface. Beacons carry an
    8-byte group id derived from the shared group key and are authenticated with
    HMAC-SHA256(key), so only nodes holding the same key see each other.

    All callbacks (Listener) are delivered on the message thread. */
class Discovery : private juce::Thread,
                  private juce::AsyncUpdater
{
public:
    static constexpr int kPort = 47700;
    static constexpr const char* kMulticastGroup = "239.255.77.77";
    static constexpr int kBeaconIntervalMs = 1000;
    static constexpr int kPeerTimeoutMs = 5000;

    struct Peer
    {
        juce::String id, name, ip;
        int audioPort = 0;
        ServerClaim claim;
        juce::StringArray muted;      // only meaningful when this peer is the server
        juce::String version;
        juce::uint32 lastSeenMs = 0;
    };

    struct LocalInfo
    {
        juce::String nodeId, name, groupKey;
        int audioPort = 0;
        ServerClaim claim;
        juce::StringArray muted;
    };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void discoveryChanged() = 0;
    };

    Discovery();
    ~Discovery() override;

    void setListener (Listener* l) { listener = l; }

    bool start();                       // false if the discovery port could not be opened
    void stop();
    bool isRunning() const              { return isThreadRunning(); }

    void setLocalInfo (const LocalInfo&);
    void sendBeaconSoon();              // e.g. right after claiming the server role

    std::vector<Peer> getPeers() const; // live peers, excluding ourselves

    struct Stats { juce::uint64 rawRx = 0, accepted = 0, rejected = 0, sent = 0, sendErrors = 0; int interfaces = 0; };
    Stats getStats() const;

    // exposed for tests
    static juce::MemoryBlock encode (const LocalInfo&, const juce::String& session, juce::uint64 seq);
    struct Decoded { Peer peer; juce::String session; juce::uint64 seq = 0; };
    static bool decode (const void* data, size_t size, const juce::String& groupKey, Decoded& out);

private:
    void run() override;
    void handleAsyncUpdate() override;

    void refreshInterfaces();
    void sendBeacon();
    void receiveOne();
    void prune();

    struct NetInterface
    {
        juce::String address, broadcast;   // IPv4 dotted
        bool operator== (const NetInterface& o) const { return address == o.address && broadcast == o.broadcast; }
    };
    /** Up, non-loopback, non-link-local IPv4 interfaces with their broadcast address. */
    static std::vector<NetInterface> listInterfaces();

    std::unique_ptr<juce::DatagramSocket> socket;
    std::vector<NetInterface> interfaces;
    juce::uint32 lastInterfaceRefresh = 0;
    juce::StringArray loggedSendErrors;   // discovery thread only

    mutable juce::CriticalSection lock;
    LocalInfo local;
    std::map<juce::String, Peer> peers;
    std::map<juce::String, std::pair<juce::String, juce::uint64>> lastSeq; // id -> (session, seq)

    const juce::String session { juce::Uuid().toString() };
    juce::uint64 seq = 0;
    std::atomic<bool> sendNow { true };
    std::atomic<juce::uint64> rawRx { 0 }, accepted { 0 }, rejected { 0 }, sent { 0 }, sendErrors { 0 };
    std::atomic<int> numInterfaces { 0 };

    Listener* listener = nullptr;
};
