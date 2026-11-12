// SPDX-License-Identifier: GPL-3.0-or-later
#include "MicPermission.h"

#import <AVFoundation/AVFoundation.h>
#include <juce_events/juce_events.h>

namespace MicPermission
{
    Status current()
    {
        if (@available (macOS 10.14, *))
        {
            switch ([AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio])
            {
                case AVAuthorizationStatusAuthorized:    return Status::granted;
                case AVAuthorizationStatusNotDetermined: return Status::undetermined;
                default:                                 return Status::denied;
            }
        }
        return Status::granted;
    }

    void request (std::function<void (bool)> done)
    {
        const auto s = current();
        if (s != Status::undetermined)
        {
            juce::MessageManager::callAsync ([done, s] { done (s == Status::granted); });
            return;
        }

        if (@available (macOS 10.14, *))
        {
            [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio
                                     completionHandler: ^(BOOL granted)
            {
                const bool g = granted;
                juce::MessageManager::callAsync ([done, g] { done (g); });
            }];
        }
    }
}
