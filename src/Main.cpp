// SoundFunnel - aggregate audio from several PCs on a LAN to one server PC.
// Based on SonoBus (https://www.sonobus.net, Copyright (C) Jesse Chappell), GPLv3.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_extra/juce_gui_extra.h>

#include "AppCore.h"
#include "Localisation.h"
#include "SettingsWindow.h"
#include "TrayIcon.h"

using namespace juce;

int runSelfTest();   // SelfTest.cpp

namespace
{
    String argValue (const StringArray& args, const String& name)
    {
        auto i = args.indexOf (name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : String();
    }
}

class SoundFunnelApplication : public JUCEApplication
{
public:
    const String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }

    bool moreThanOneInstanceAllowed() override
    {
        // test profiles may run side by side; the normal app is single-instance
        return getCommandLineParameterArray().contains ("--profile")
            || getCommandLineParameterArray().contains ("--selftest");
    }

    void initialise (const String&) override
    {
        auto args = getCommandLineParameterArray();

        if (args.contains ("--selftest"))
        {
            setApplicationReturnValue (runSelfTest());
            quit();
            return;
        }

        Localisation::install();

        core = std::make_unique<AppCore> (argValue (args, "--profile"));
        core->getEngine().setTestTone (args.contains ("--test-tone"));
        if (args.contains ("--silent"))
        {
            // testing on one PC: never open an output device (avoids mic -> speaker feedback)
            auto& dm = core->getDeviceManager();
            auto setup = dm.getAudioDeviceSetup();
            setup.outputDeviceName = {};
            dm.setAudioDeviceSetup (setup, true);
        }
        settings = std::make_unique<SettingsWindow> (*core);

        tray = std::make_unique<TrayIcon> (*core,
                                           [this] { settings->showAndFocus(); },
                                           [this] { systemRequestedQuit(); });

        bool showWindow = ! args.contains ("--background") || core->getConfig().getGroupKey().isEmpty();
       #if JUCE_LINUX || JUCE_BSD
        showWindow = showWindow || ! args.contains ("--background"); // tray support varies on Linux desktops
       #endif
        if (showWindow) settings->showAndFocus();

        // diagnostics / automated testing:
        //   --status-file F   : write the current state as JSON every second
        //   --control-file F  : poll F for one command ("server", "pause", "resume", "mute <name>", "unmute <name>")
        statusFile = argValue (args, "--status-file");
        controlFile = argValue (args, "--control-file");
        if (statusFile.isNotEmpty() || controlFile.isNotEmpty())
            diag = std::make_unique<DiagTimer> (*this);
    }

    struct DiagTimer : public Timer
    {
        explicit DiagTimer (SoundFunnelApplication& a) : app (a) { startTimer (1000); }
        void timerCallback() override { app.diagTick(); }
        SoundFunnelApplication& app;
    };

    void diagTick()
    {
        if (controlFile.isNotEmpty())
        {
            File f (controlFile);
            if (f.existsAsFile())
            {
                auto cmd = f.loadFileAsString().trim();
                f.deleteFile();
                auto verb = cmd.upToFirstOccurrenceOf (" ", false, false);
                auto arg  = cmd.fromFirstOccurrenceOf (" ", false, false);
                if (verb == "server") core->becomeServer();
                else if (verb == "pause")  core->setPaused (true);
                else if (verb == "resume") core->setPaused (false);
                else if (verb == "mute" || verb == "unmute")
                    for (auto& m : core->getMembers())
                        if (m.name == arg) core->setClientMuted (m.id, verb == "mute");
            }
        }

        if (statusFile.isNotEmpty())
        {
            auto* o = new DynamicObject();
            o->setProperty ("status", core->getStatusText());
            o->setProperty ("role", (int) core->getRole());
            o->setProperty ("server", core->getServerName());
            o->setProperty ("audioPort", core->getEngine().getPort());
            auto k = core->getEngine().getCounters();
            o->setProperty ("net", String::formatted ("rx=%llu routed=%llu dropped=%llu tx=%llu cb=%llu",
                                                      k.rx, k.rxRouted, k.rxDropped, k.tx, k.callbacks));
            if (auto* dev = core->getDeviceManager().getCurrentAudioDevice())
                o->setProperty ("device", dev->getName() + " / " + String (dev->getCurrentSampleRate()) + " Hz / "
                                + String (dev->getCurrentBufferSizeSamples()) + " / out " + String (dev->getActiveOutputChannels().countNumberOfSetBits()));
            Array<var> members;
            for (auto& m : core->getMembers())
            {
                auto* mo = new DynamicObject();
                mo->setProperty ("name", m.name);
                mo->setProperty ("ip", m.ip);
                mo->setProperty ("self", m.isSelf);
                mo->setProperty ("server", m.isServer);
                mo->setProperty ("muted", m.muted);
                mo->setProperty ("receiving", m.receiving);
                mo->setProperty ("level", m.level);
                members.add (var (mo));
            }
            o->setProperty ("members", members);
            File (statusFile).replaceWithText (JSON::toString (var (o)));
        }
    }

    void shutdown() override
    {
        diag.reset();
        tray.reset();
        settings.reset();
        core.reset();
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const String&) override
    {
        if (settings) settings->showAndFocus();
    }

private:
    std::unique_ptr<AppCore> core;
    std::unique_ptr<SettingsWindow> settings;
    std::unique_ptr<TrayIcon> tray;
    std::unique_ptr<DiagTimer> diag;
    String statusFile, controlFile;
};

START_JUCE_APPLICATION (SoundFunnelApplication)
