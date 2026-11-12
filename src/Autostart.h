// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/** Start-at-login registration.
    Windows: HKCU\...\Run, macOS: ~/Library/LaunchAgents plist, Linux: ~/.config/autostart. */
namespace Autostart
{
    bool isEnabled();
    bool setEnabled (bool);
}
