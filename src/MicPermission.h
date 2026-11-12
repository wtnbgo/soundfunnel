// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>

/** Microphone (audio capture) permission.

    On macOS, opening a CoreAudio device that has inputs runs a TCC check even when
    no input channel is used, and CoreAudio blocks inside CreateIOProcID until it is
    answered. When the prompt cannot be shown the whole app hangs, so ask through
    AVFoundation first and only open the device once there is an answer.
    On other platforms access is always reported as granted. */
namespace MicPermission
{
    enum class Status { granted, denied, undetermined };

    Status current();

    /** Asks the user if needed. The callback runs on the message thread. */
    void request (std::function<void (bool granted)> done);
}
