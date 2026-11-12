// SPDX-License-Identifier: GPL-3.0-or-later
#include "MicPermission.h"

#if ! defined (__APPLE__)
#include <juce_events/juce_events.h>

namespace MicPermission
{
    Status current() { return Status::granted; }

    void request (std::function<void (bool)> done)
    {
        juce::MessageManager::callAsync ([done] { done (true); });
    }
}
#endif
