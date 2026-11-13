// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_core/juce_core.h>

/** The operating system's current default audio devices.

    Windows: the default console endpoints (MMDevice API). The names match the
    device names of JUCE's "Windows Audio" (WASAPI shared) device type.
    Other platforms: not supported (empty names). */
namespace DefaultDevices
{
    bool isSupported();

    /** Friendly name of the default input (capture) or output (render) device, or empty. */
    juce::String getName (bool input);

    /** JUCE device type whose device names getName() matches. */
    inline const char* deviceTypeName() { return "Windows Audio"; }
}
