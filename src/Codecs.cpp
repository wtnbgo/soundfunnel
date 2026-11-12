// SPDX-License-Identifier: GPL-3.0-or-later
#include "Codecs.h"
#include "aoo/aoo_opus.h"
#include "aoo/aoo_pcm.h"

using namespace juce;

namespace Codecs
{

static Info opus (int bitrate, int minBlock)
{
    Info i { Info::Opus };
    i.bitrate = bitrate;
    i.minBlocksize = minBlock;
    i.name = "Opus " + String (bitrate / 1000) + " kbps/ch";
    return i;
}

static Info pcm (int bytes)
{
    Info i { Info::PCM };
    i.bitdepth = bytes;
    i.minBlocksize = 16;
    i.name = bytes == 4 ? String ("PCM 32 bit float") : "PCM " + String (bytes * 8) + " bit";
    return i;
}

const Array<Info>& all()
{
    static const Array<Info> list {
        opus (16000, 960),
        opus (24000, 480),
        opus (48000, 240),
        opus (64000, 240),
        opus (96000, 120),
        opus (128000, 120),
        opus (160000, 120),
        opus (256000, 120),
        pcm (2), pcm (3), pcm (4)
    };
    return list;
}

int defaultIndex() { return 5; } // Opus 128 kbps/ch: transparent enough for music on a LAN

bool toAooFormat (int index, int channels, double samplerate, int blocksize, aoo_format_storage& out)
{
    if (! isPositiveAndBelow (index, all().size()))
        index = defaultIndex();

    const auto& info = all().getReference (index);
    zerostruct (out);

    if (info.kind == Info::PCM)
    {
        auto* f = reinterpret_cast<aoo_format_pcm*> (&out);
        f->header.codec      = AOO_CODEC_PCM;
        f->header.blocksize  = jmax (blocksize, info.minBlocksize);
        f->header.samplerate = (int32_t) samplerate;
        f->header.nchannels  = channels;
        f->bitdepth = info.bitdepth == 3 ? AOO_PCM_INT24
                    : info.bitdepth == 4 ? AOO_PCM_FLOAT32
                                         : AOO_PCM_INT16;
        return true;
    }

    auto* f = reinterpret_cast<aoo_format_opus*> (&out);
    f->header.codec      = AOO_CODEC_OPUS;
    f->header.blocksize  = jmax (blocksize, info.minBlocksize);
    f->header.samplerate = (int32_t) samplerate;
    f->header.nchannels  = channels;
    f->bitrate           = info.bitrate * channels;
    f->complexity        = 10;
    f->signal_type       = OPUS_SIGNAL_MUSIC;
    f->application_type  = OPUS_APPLICATION_RESTRICTED_LOWDELAY;
    return true;
}

} // namespace Codecs
