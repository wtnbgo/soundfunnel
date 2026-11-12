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

    bool isValid() const { return nodeId.isNotEmpty(); }

    bool operator< (const ServerClaim& o) const
    {
        if (epoch != o.epoch) return epoch < o.epoch;
        return nodeId.compare (o.nodeId) < 0;
    }
    bool operator== (const ServerClaim& o) const { return epoch == o.epoch && nodeId == o.nodeId; }
    bool operator!= (const ServerClaim& o) const { return ! operator== (o); }
};
