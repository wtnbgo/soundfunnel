// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_core/juce_core.h>

/** "Who is the server" claim. The node that pressed "become server" most
    recently wins: it claims epoch = (largest epoch it knows) + 1.
    Ties (simultaneous presses) are broken by node id so every node converges
    on the same answer. */
struct ServerClaim
{
    juce::int64  epoch = 0;
    juce::String nodeId;       // empty = nobody has claimed yet

    // Epochs grow by one per "become server" press. Anything outside this range comes from
    // a broken or hostile peer (and epoch + 1 must never overflow).
    static constexpr juce::int64 kMaxEpoch = 1000000000;
    // A peer may be ahead of us by a few presses we missed, not by thousands.
    static constexpr juce::int64 kMaxEpochJump = 1000;

    bool isValid() const { return nodeId.isNotEmpty() && epoch > 0 && epoch <= kMaxEpoch; }

    bool operator< (const ServerClaim& o) const
    {
        if (epoch != o.epoch) return epoch < o.epoch;
        return nodeId.compare (o.nodeId) < 0;
    }
    bool operator== (const ServerClaim& o) const { return epoch == o.epoch && nodeId == o.nodeId; }
    bool operator!= (const ServerClaim& o) const { return ! operator== (o); }
};
