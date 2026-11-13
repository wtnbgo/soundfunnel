// SPDX-License-Identifier: GPL-3.0-or-later
#if defined (_WIN32)
 #define NOMINMAX
 #include <winsock2.h>
 #include <ws2tcpip.h>
 #include <iphlpapi.h>
 #pragma comment (lib, "iphlpapi.lib")
#else
 #include <sys/socket.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>
 #include <ifaddrs.h>
 #include <net/if.h>
 #include <cerrno>
 #include <cstring>
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
    socket->setMulticastLoopbackEnabled (true);
   #ifdef SO_NOSIGPIPE
    {
        int one = 1;
        setsockopt (socket->getRawSocketHandle(), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof (one));
    }
   #endif // lets several instances on one PC see each other

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

Discovery::Stats Discovery::getStats() const
{
    return { rawRx.load(), accepted.load(), rejected.load(), sent.load(), sendErrors.load(), numInterfaces.load() };
}

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
    o->setProperty ("t", String (Time::currentTimeMillis()));
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

    out.peer.id        = v["id"].toString().substring (0, 64);
    out.peer.name      = v["name"].toString().substring (0, 64);
    out.peer.audioPort = (int) v["port"];
    out.peer.claim     = claimFromVar (v["claim"]);
    out.peer.version   = v["app"].toString().substring (0, 32);
    out.peer.muted.clear();
    if (auto* m = v["muted"].getArray())
        for (auto& s : *m)
            if (out.peer.muted.size() < kMaxPeers)
                out.peer.muted.add (s.toString().substring (0, 64));
    out.session = v["sess"].toString().substring (0, 64);
    out.seq     = (uint64) v["seq"].toString().getLargeIntValue();
    out.sentMs  = v["t"].toString().getLargeIntValue();

    if (out.peer.audioPort < 0 || out.peer.audioPort > 65535) out.peer.audioPort = 0;
    if (! out.peer.claim.isValid()) out.peer.claim = {};
    return out.peer.id.isNotEmpty() && out.session.isNotEmpty();
}

//==============================================================================
// JUCE's IPAddress::findAllAddresses() returns multicast group and link-local
// addresses on Windows, and getInterfaceBroadcastAddress() returns 0.0.0.0 there,
// so enumerate interfaces with the OS API instead.
std::vector<Discovery::NetInterface> Discovery::listInterfaces()
{
    std::vector<NetInterface> result;

    auto add = [&result] (uint32 addrHost, uint32 maskHost)
    {
        if ((addrHost >> 24) == 127) return;                  // loopback
        if ((addrHost >> 16) == 0xa9fe) return;               // 169.254/16 link-local
        if ((addrHost >> 28) == 0xe || addrHost == 0) return; // multicast / any
        in_addr a {}, b {};
        a.s_addr = htonl (addrHost);
        b.s_addr = htonl (maskHost != 0 && maskHost != 0xffffffffu ? (addrHost | ~maskHost) : 0);
        char sa[INET_ADDRSTRLEN] {}, sb[INET_ADDRSTRLEN] {};
        inet_ntop (AF_INET, &a, sa, sizeof (sa));
        inet_ntop (AF_INET, &b, sb, sizeof (sb));
        NetInterface ni { sa, b.s_addr != 0 ? String (sb) : String() };
        if (std::find (result.begin(), result.end(), ni) == result.end())
            result.push_back (ni);
    };

   #if defined (_WIN32)
    ULONG size = 16 * 1024;
    HeapBlock<char> buf;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        buf.calloc (size);
        auto r = GetAdaptersAddresses (AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                       nullptr, (IP_ADAPTER_ADDRESSES*) buf.get(), &size);
        if (r == ERROR_BUFFER_OVERFLOW) continue;
        if (r != NO_ERROR) return result;
        break;
    }
    for (auto* ad = (IP_ADAPTER_ADDRESSES*) buf.get(); ad != nullptr; ad = ad->Next)
    {
        if (ad->OperStatus != IfOperStatusUp || ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = ad->FirstUnicastAddress; u != nullptr; u = u->Next)
        {
            if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
            auto addr = ntohl (((sockaddr_in*) u->Address.lpSockaddr)->sin_addr.s_addr);
            auto prefix = (int) u->OnLinkPrefixLength;
            uint32 mask = prefix <= 0 ? 0 : (prefix >= 32 ? 0xffffffffu : ~((1u << (32 - prefix)) - 1));
            add (addr, mask);
        }
    }
   #else
    ifaddrs* list = nullptr;
    if (getifaddrs (&list) != 0) return result;
    for (auto* i = list; i != nullptr; i = i->ifa_next)
    {
        if (i->ifa_addr == nullptr || i->ifa_addr->sa_family != AF_INET) continue;
        if ((i->ifa_flags & IFF_UP) == 0 || (i->ifa_flags & IFF_LOOPBACK) != 0) continue;
        auto addr = ntohl (((sockaddr_in*) i->ifa_addr)->sin_addr.s_addr);
        auto mask = i->ifa_netmask != nullptr ? ntohl (((sockaddr_in*) i->ifa_netmask)->sin_addr.s_addr) : 0u;
        add (addr, mask);
    }
    freeifaddrs (list);
   #endif

    return result;
}

void Discovery::refreshInterfaces()
{
    lastInterfaceRefresh = Time::getMillisecondCounter();

    auto list = listInterfaces();
    if (list == interfaces) return;
    interfaces = list;
    numInterfaces = (int) interfaces.size();
    for (auto& a : interfaces)
        Logger::writeToLog ("discovery interface " + a.address + " broadcast " + (a.broadcast.isEmpty() ? String ("-") : a.broadcast));

    // join the multicast group on every interface (JUCE's joinMulticast only uses the default one)
    auto fd = socket->getRawSocketHandle();
    for (auto& a : interfaces)
    {
        ip_mreq mreq {};
        inet_pton (AF_INET, kMulticastGroup, &mreq.imr_multiaddr);
        inet_pton (AF_INET, a.address.toRawUTF8(), &mreq.imr_interface);
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

    auto sendTo = [&] (const String& dest, const String& via)
    {
        if (socket->write (dest, kPort, pkt.getData(), (int) pkt.getSize()) > 0)
        {
            ++sent;
            return;
        }
       #if defined (_WIN32)
        const int err = WSAGetLastError();
        const String reason (err);
       #else
        const int err = errno;
        const String reason = String (err) + " " + String (std::strerror (err));
       #endif
        // log each distinct failure once so a broken route shows up without flooding the log
        const auto key = dest + "|" + via + "|" + String (err);
        if (++sendErrors, ! loggedSendErrors.contains (key) && loggedSendErrors.size() < 32)
        {
            loggedSendErrors.add (key);
            Logger::writeToLog ("discovery send to " + dest + " via " + via + " failed: " + reason);
        }
    };

    for (auto& a : interfaces)
    {
        // multicast out of this interface
        in_addr ifaddr {};
        inet_pton (AF_INET, a.address.toRawUTF8(), &ifaddr);
        setsockopt ((decltype (fd)) fd, IPPROTO_IP, IP_MULTICAST_IF, (const char*) &ifaddr, sizeof (ifaddr));
        sendTo (kMulticastGroup, a.address);

        // directed broadcast as a fallback for networks that drop multicast
        if (a.broadcast.isNotEmpty())
            sendTo (a.broadcast, a.address);
    }
}

void Discovery::receiveOne()
{
    char buf[2048];
    String senderIP;
    int senderPort = 0;
    auto n = socket->read (buf, sizeof (buf), false, senderIP, senderPort);
    if (n <= 0) return;
    ++rawRx;

    String key, self;
    {
        const ScopedLock sl (lock);
        key = local.groupKey;
        self = local.nodeId;
    }
    if (key.isEmpty()) return;

    Decoded d;
    if (! decode (buf, (size_t) n, key, d)) { ++rejected; return; }
    if (d.peer.id == self) return;
    ++accepted;

    bool changed = false;
    {
        const ScopedLock sl (lock);

        // verified with a key that has been replaced meanwhile: belongs to the old group
        if (local.groupKey != key) return;

        const auto now = Time::getMillisecondCounter();
        auto st = lastSeq.find (d.peer.id);

        if (st == lastSeq.end())
        {
            if ((int) peers.size() >= kMaxPeers) return;
            lastSeq[d.peer.id] = { d.session, d.seq, d.sentMs, senderIP, now };
        }
        else if (st->second.session == d.session)
        {
            auto& s = st->second;
            const bool fromPinned = senderIP == s.pinnedIp;
            if (fromPinned) s.pinnedSeenMs = now;        // copies (multicast + broadcast) keep it alive
            if (d.seq <= s.seq) return;                  // duplicate or replay
            if (! fromPinned && now - s.pinnedSeenMs < (uint32) kPeerTimeoutMs)
                return;                                  // another address while the pinned one is alive
            s.seq = d.seq;
            s.pinnedIp = senderIP;                       // (re-pin only once the old address went silent)
            s.pinnedSeenMs = now;
        }
        else
        {
            // a new run of that node: only if it is newer than what we accepted before
            if (d.sentMs <= st->second.sentMs) return;
            st->second = { d.session, d.seq, d.sentMs, senderIP, now };
        }

        d.peer.ip = lastSeq[d.peer.id].pinnedIp;
        d.peer.lastSeenMs = now;

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

        // replay state outlives the peer (so an old run of a node that went offline can't be
        // replayed), but not forever: an hour of silence, and a hard cap against floods
        for (auto it = lastSeq.begin(); it != lastSeq.end();)
        {
            if (now - it->second.pinnedSeenMs > 3600u * 1000u && peers.count (it->first) == 0)
                it = lastSeq.erase (it);
            else ++it;
        }
        while ((int) lastSeq.size() > kMaxPeers * 4)
        {
            auto oldest = lastSeq.end();
            for (auto it = lastSeq.begin(); it != lastSeq.end(); ++it)
                if (peers.count (it->first) == 0
                     && (oldest == lastSeq.end() || it->second.pinnedSeenMs < oldest->second.pinnedSeenMs))
                    oldest = it;
            if (oldest == lastSeq.end()) break;
            lastSeq.erase (oldest);
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
