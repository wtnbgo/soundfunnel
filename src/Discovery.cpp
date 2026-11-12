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

#include "Discovery.h"
#include <juce_cryptography/juce_cryptography.h>

using namespace juce;

namespace
{
    constexpr char kMagic[4] = { 'S', 'F', 'N', 'L' };
    constexpr uint8 kProtoVersion = 1;
    constexpr size_t kHeaderSize = 4 + 1 + 8 + 2;
    constexpr size_t kMacSize = 32;

    MemoryBlock sha256 (const void* d, size_t n)
    {
        return SHA256 (d, n).getRawData();
    }

    /** RFC 2104 HMAC with SHA-256. */
    MemoryBlock hmacSha256 (const String& key, const void* data, size_t size)
    {
        constexpr size_t B = 64;
        MemoryBlock k (key.toRawUTF8(), key.getNumBytesAsUTF8());
        if (k.getSize() > B) k = sha256 (k.getData(), k.getSize());
        k.ensureSize (B, true);

        MemoryBlock ipad (B), opad (B);
        for (size_t i = 0; i < B; ++i)
        {
            auto c = (uint8) k[i];
            ipad[i] = (char) (c ^ 0x36);
            opad[i] = (char) (c ^ 0x5c);
        }

        ipad.append (data, size);
        auto inner = sha256 (ipad.getData(), ipad.getSize());
        opad.append (inner.getData(), inner.getSize());
        return sha256 (opad.getData(), opad.getSize());
    }

    MemoryBlock groupId (const String& key)
    {
        auto s = ("soundfunnel-group:" + key);
        auto h = sha256 (s.toRawUTF8(), s.getNumBytesAsUTF8());
        h.setSize (8);
        return h;
    }

    bool constantTimeEqual (const void* a, const void* b, size_t n)
    {
        auto* x = static_cast<const uint8*> (a);
        auto* y = static_cast<const uint8*> (b);
        uint8 r = 0;
        for (size_t i = 0; i < n; ++i) r |= (uint8) (x[i] ^ y[i]);
        return r == 0;
    }

    var claimToVar (const ServerClaim& c)
    {
        auto* o = new DynamicObject();
        o->setProperty ("e", String (c.epoch));
        o->setProperty ("id", c.nodeId);
        return var (o);
    }

    ServerClaim claimFromVar (const var& v)
    {
        ServerClaim c;
        c.epoch  = v["e"].toString().getLargeIntValue();
        c.nodeId = v["id"].toString();
        return c;
    }
}

//==============================================================================
Discovery::Discovery() : Thread ("SoundFunnel discovery") {}

Discovery::~Discovery()
{
    stop();
}

bool Discovery::start()
{
    if (isThreadRunning()) return true;

    socket = std::make_unique<DatagramSocket> (true);
    socket->setEnablePortReuse (true);
    if (! socket->bindToPort (kPort))
    {
        socket.reset();
        return false;
    }
    socket->setMulticastLoopbackEnabled (true); // lets several instances on one PC see each other

    refreshInterfaces();
    startThread();
    return true;
}

void Discovery::stop()
{
    signalThreadShouldExit();
    if (socket) socket->shutdown();
    stopThread (2000);
    socket.reset();
    cancelPendingUpdate();
}

void Discovery::setLocalInfo (const LocalInfo& info)
{
    bool changed;
    {
        const ScopedLock sl (lock);
        changed = info.groupKey != local.groupKey;
        local = info;
        if (changed)
        {
            peers.clear();
            lastSeq.clear();
        }
    }
    sendNow = true;
    if (changed) triggerAsyncUpdate();
}

void Discovery::sendBeaconSoon() { sendNow = true; }

std::vector<Discovery::Peer> Discovery::getPeers() const
{
    const ScopedLock sl (lock);
    std::vector<Peer> r;
    for (auto& [id, p] : peers) r.push_back (p);
    std::sort (r.begin(), r.end(), [] (auto& a, auto& b) { return a.name.compareNatural (b.name) < 0; });
    return r;
}

//==============================================================================
MemoryBlock Discovery::encode (const LocalInfo& info, const String& sess, uint64 sequence)
{
    auto* o = new DynamicObject();
    o->setProperty ("v", 1);
    o->setProperty ("app", JUCE_APPLICATION_VERSION_STRING);
    o->setProperty ("id", info.nodeId);
    o->setProperty ("sess", sess);
    o->setProperty ("seq", String ((int64) sequence));
    o->setProperty ("name", info.name);
    o->setProperty ("port", info.audioPort);
    o->setProperty ("claim", claimToVar (info.claim));
    if (! info.muted.isEmpty())
    {
        Array<var> m;
        for (auto& s : info.muted) m.add (s);
        o->setProperty ("muted", m);
    }

    auto json = JSON::toString (var (o), true);
    MemoryBlock payload (json.toRawUTF8(), json.getNumBytesAsUTF8());
    if (payload.getSize() > 1200) return {};   // keep well under a typical MTU

    MemoryBlock pkt;
    pkt.append (kMagic, 4);
    pkt.append (&kProtoVersion, 1);
    auto gid = groupId (info.groupKey);
    pkt.append (gid.getData(), 8);
    uint8 len[2] = { (uint8) (payload.getSize() >> 8), (uint8) (payload.getSize() & 0xff) };
    pkt.append (len, 2);
    pkt.append (payload.getData(), payload.getSize());
    auto mac = hmacSha256 (info.groupKey, pkt.getData(), pkt.getSize());
    pkt.append (mac.getData(), mac.getSize());
    return pkt;
}

bool Discovery::decode (const void* data, size_t size, const String& key, Decoded& out)
{
    auto* p = static_cast<const uint8*> (data);
    if (size < kHeaderSize + kMacSize) return false;
    if (memcmp (p, kMagic, 4) != 0 || p[4] != kProtoVersion) return false;

    auto gid = groupId (key);
    if (memcmp (p + 5, gid.getData(), 8) != 0) return false;     // another group: ignore quietly

    size_t len = ((size_t) p[13] << 8) | p[14];
    if (kHeaderSize + len + kMacSize != size) return false;

    auto mac = hmacSha256 (key, p, kHeaderSize + len);
    if (! constantTimeEqual (mac.getData(), p + kHeaderSize + len, kMacSize)) return false;

    auto v = JSON::parse (String::fromUTF8 ((const char*) p + kHeaderSize, (int) len));
    if (! v.isObject()) return false;

    out.peer.id        = v["id"].toString();
    out.peer.name      = v["name"].toString();
    out.peer.audioPort = (int) v["port"];
    out.peer.claim     = claimFromVar (v["claim"]);
    out.peer.version   = v["app"].toString();
    out.peer.muted.clear();
    if (auto* m = v["muted"].getArray())
        for (auto& s : *m) out.peer.muted.add (s.toString());
    out.session = v["sess"].toString();
    out.seq     = (uint64) v["seq"].toString().getLargeIntValue();
    return out.peer.id.isNotEmpty();
}

//==============================================================================
void Discovery::refreshInterfaces()
{
    lastInterfaceRefresh = Time::getMillisecondCounter();

    Array<IPAddress> addrs;
    IPAddress::findAllAddresses (addrs, false);
    addrs.removeIf ([] (const IPAddress& a) { return a.isNull(); });

    if (addrs == interfaces) return;
    interfaces = addrs;

    // join the multicast group on every interface (JUCE's joinMulticast only uses the default one)
    auto fd = socket->getRawSocketHandle();
    for (auto& a : interfaces)
    {
        ip_mreq mreq {};
        inet_pton (AF_INET, kMulticastGroup, &mreq.imr_multiaddr);
        inet_pton (AF_INET, a.toString().toRawUTF8(), &mreq.imr_interface);
        setsockopt ((decltype (socket->getRawSocketHandle())) fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                    (const char*) &mreq, sizeof (mreq)); // fails harmlessly if already joined
    }
}

void Discovery::sendBeacon()
{
    MemoryBlock pkt;
    {
        const ScopedLock sl (lock);
        if (local.nodeId.isEmpty() || local.groupKey.isEmpty()) return;
        pkt = encode (local, session, ++seq);
    }
    if (pkt.isEmpty()) return;

    auto fd = socket->getRawSocketHandle();

    for (auto& a : interfaces)
    {
        // multicast out of this interface
        in_addr ifaddr {};
        inet_pton (AF_INET, a.toString().toRawUTF8(), &ifaddr);
        setsockopt ((decltype (fd)) fd, IPPROTO_IP, IP_MULTICAST_IF, (const char*) &ifaddr, sizeof (ifaddr));
        socket->write (kMulticastGroup, kPort, pkt.getData(), (int) pkt.getSize());

        // directed broadcast as a fallback for networks that drop multicast
        auto bc = IPAddress::getInterfaceBroadcastAddress (a);
        if (! bc.isNull() && a != IPAddress::local())
            socket->write (bc.toString(), kPort, pkt.getData(), (int) pkt.getSize());
    }
}

void Discovery::receiveOne()
{
    char buf[2048];
    String senderIP;
    int senderPort = 0;
    auto n = socket->read (buf, sizeof (buf), false, senderIP, senderPort);
    if (n <= 0) return;

    String key, self;
    {
        const ScopedLock sl (lock);
        key = local.groupKey;
        self = local.nodeId;
    }
    if (key.isEmpty()) return;

    Decoded d;
    if (! decode (buf, (size_t) n, key, d) || d.peer.id == self) return;

    bool changed = false;
    {
        const ScopedLock sl (lock);

        // drop duplicates (we receive both multicast and broadcast copies) and replays
        auto& last = lastSeq[d.peer.id];
        if (last.first == d.session && d.seq <= last.second) return;
        last = { d.session, d.seq };

        d.peer.ip = senderIP;
        d.peer.lastSeenMs = Time::getMillisecondCounter();

        auto it = peers.find (d.peer.id);
        if (it == peers.end())
            changed = true;
        else
        {
            auto& o = it->second;
            changed = o.name != d.peer.name || o.ip != d.peer.ip || o.audioPort != d.peer.audioPort
                   || o.claim != d.peer.claim || o.muted != d.peer.muted;
        }
        peers[d.peer.id] = d.peer;
    }

    if (changed) triggerAsyncUpdate();
}

void Discovery::prune()
{
    bool changed = false;
    {
        const ScopedLock sl (lock);
        auto now = Time::getMillisecondCounter();
        for (auto it = peers.begin(); it != peers.end();)
        {
            if (now - it->second.lastSeenMs > (uint32) kPeerTimeoutMs)
            {
                it = peers.erase (it);
                changed = true;
            }
            else ++it;
        }
    }
    if (changed) triggerAsyncUpdate();
}

void Discovery::run()
{
    uint32 lastSend = 0;

    while (! threadShouldExit())
    {
        auto now = Time::getMillisecondCounter();

        if (sendNow.exchange (false) || now - lastSend >= (uint32) kBeaconIntervalMs)
        {
            sendBeacon();
            lastSend = now;
            prune();
        }

        if (now - lastInterfaceRefresh > 10000)
            refreshInterfaces();

        if (socket->waitUntilReady (true, 100) == 1)
            receiveOne();
    }
}

void Discovery::handleAsyncUpdate()
{
    if (listener != nullptr) listener->discoveryChanged();
}
