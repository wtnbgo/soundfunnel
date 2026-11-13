// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppCore.h"

/** Linux tray icon through AppIndicator / StatusNotifierItem.

    JUCE's own Linux tray icon uses XEmbed, which crashed gnome-shell, so this uses
    libayatana-appindicator3 (or libappindicator3) instead, loaded at run time.
    GTK's main loop is pumped from a JUCE timer on the message thread.
    create() returns nullptr when the library is not installed; the app then runs
    as a normal window. */
class LinuxTray : private juce::ChangeListener,
                  private juce::Timer
{
public:
    static std::unique_ptr<LinuxTray> create (AppCore&, std::function<void()> openSettings, std::function<void()> quit);
    ~LinuxTray() override;

private:
    struct Api;
    LinuxTray (AppCore&, Api&, std::function<void()>, std::function<void()>);

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();
    juce::String iconNameFor (AppCore::Role);
    juce::String iconPathFor (AppCore::Role);

    static void onServer (void*, void* self);
    static void onPause (void*, void* self);
    static void onSettings (void*, void* self);
    static void onQuit (void*, void* self);

    AppCore& core;
    Api& api;
    std::function<void()> openSettings, quit;

    void* indicator = nullptr;
    void* menu = nullptr;
    void* statusItem = nullptr;
    void* serverItem = nullptr;
    void* pauseItem = nullptr;
    bool updating = false;
    juce::File iconDir;
    AppCore::Role shownRole { (AppCore::Role) -1 };
    juce::String shownStatus;
};
