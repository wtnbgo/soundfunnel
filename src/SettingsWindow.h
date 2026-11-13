// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "AppCore.h"

/** Settings window opened from the tray. Closing it only hides it; the app
    keeps running in the tray. */
class SettingsWindow : public juce::DocumentWindow
{
public:
    /** onQuit is shown as a button where there is no tray menu to quit from (Linux). */
    SettingsWindow (AppCore&, std::function<void()> onQuit);
    void closeButtonPressed() override;
    void visibilityChanged() override;
    void showAndFocus();

    /** True where the app lives in a system tray (Windows / macOS). */
    static bool hasTray();
    static void setTrayAvailable (bool);   // call before creating the window

private:
    void applyWindowIcon();
    juce::ComponentPeer* iconPeer = nullptr;
};
