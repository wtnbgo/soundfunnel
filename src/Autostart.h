// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/** Start-at-login registration.
    Windows: HKCU\...\Run, macOS: ~/Library/LaunchAgents plist, Linux: ~/.config/autostart. */
namespace Autostart
{
    bool isEnabled();
    bool setEnabled (bool);

    /** Linux: when SoundFunnel is not installed from a package (tar.gz, own build), add
        ~/.local/share/applications/soundfunnel.desktop and an icon so the desktop shows
        the right icon in the dock / app list. No-op elsewhere. */
    void ensureDesktopEntry();
}
