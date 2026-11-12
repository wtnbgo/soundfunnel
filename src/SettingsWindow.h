// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "AppCore.h"

/** Settings window opened from the tray. Closing it only hides it; the app
    keeps running in the tray. */
class SettingsWindow : public juce::DocumentWindow
{
public:
    explicit SettingsWindow (AppCore&);
    void closeButtonPressed() override;
    void showAndFocus();
};
