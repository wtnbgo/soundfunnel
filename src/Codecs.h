// SPDX-License-Identifier: GPL-3.0-or-later
// Codec choices follow SonoBus (Copyright (C) Jesse Chappell, GPLv3).
#pragma once

#include <juce_core/juce_core.h>
#include "aoo/aoo.h"

namespace Codecs
{
    struct Info
    {
        enum Kind { Opus, PCM } kind;
        int bitrate = 0;              // opus, per channel (bit/s)
        int bitdepth = 2;             // pcm, bytes
        int minBlocksize = 120;
        juce::String name;
    };

    const juce::Array<Info>& all();
    int defaultIndex();

    /** Fill an AOO format for a source running at samplerate/blocksize. */
    bool toAooFormat (int index, int channels, double samplerate, int blocksize, aoo_format_storage& out);
}
