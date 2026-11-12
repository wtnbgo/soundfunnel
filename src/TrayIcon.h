// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "AppCore.h"

class TrayIcon : public juce::SystemTrayIconComponent,
                 private juce::ChangeListener
{
public:
    TrayIcon (AppCore&, std::function<void()> openSettings, std::function<void()> quit);
    ~TrayIcon() override;

    static juce::Image makeIcon (AppCore::Role, int size);

private:
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showMenu();
    void refresh();

    AppCore& core;
    std::function<void()> openSettings, quit;
    AppCore::Role shownRole { (AppCore::Role) -1 };
};
