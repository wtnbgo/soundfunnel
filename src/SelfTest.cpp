// SPDX-License-Identifier: GPL-3.0-or-later
// `SoundFunnel --selftest` : protocol checks that need no network or audio device.
#include <juce_core/juce_core.h>
#include "Discovery.h"
#include "AudioEngine.h"

using namespace juce;

namespace
{
    int failures = 0;
    void check (bool ok, const char* what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what);
        if (! ok) ++failures;
    }
}

int runSelfTest()
{
    Discovery::LocalInfo li;
    li.nodeId = "node-a";
    li.name = u8"テストPC";
    li.groupKey = "secret";
    li.audioPort = 47701;
    li.claim = { 7, "node-a" };
    li.muted = { "node-b" };

    auto pkt = Discovery::encode (li, "sess1", 42);
    check (! pkt.isEmpty(), "encode produces a packet");

    Discovery::Decoded d;
    check (Discovery::decode (pkt.getData(), pkt.getSize(), "secret", d), "decode with the right key");
    check (d.peer.id == "node-a" && d.peer.name == li.name && d.peer.audioPort == 47701, "fields round-trip");
    check (d.peer.claim == li.claim, "claim round-trips");
    check (d.peer.muted.contains ("node-b"), "mute list round-trips");
    check (d.seq == 42 && d.session == "sess1", "sequence round-trips");

    check (! Discovery::decode (pkt.getData(), pkt.getSize(), "other", d), "wrong key is rejected");

    auto tampered = pkt;
    tampered[20] = (char) (tampered[20] ^ 1);
    check (! Discovery::decode (tampered.getData(), tampered.getSize(), "secret", d), "tampered packet is rejected");

    check (! Discovery::decode (pkt.getData(), pkt.getSize() - 1, "secret", d), "truncated packet is rejected");

    // election ordering
    ServerClaim a { 3, "aaa" }, b { 4, "aaa" }, c { 4, "bbb" };
    check (a < b && b < c && ! (c < b), "claim ordering: epoch, then node id");

    // node -> AOO id
    auto id1 = AudioEngine::idForNode ("node-a"), id2 = AudioEngine::idForNode ("node-a");
    check (id1 == id2 && id1 > 0, "AOO id is stable and positive");

    std::printf ("%s (%d failure%s)\n", failures == 0 ? "PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
