// SoundFunnel - aggregate audio from several PCs on a LAN to one server PC.
// Based on SonoBus (https://www.sonobus.net, Copyright (C) Jesse Chappell), GPLv3.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_extra/juce_gui_extra.h>
#include <csignal>

#include "AppCore.h"
#include "Localisation.h"
#include "SettingsWindow.h"
#include "TrayIcon.h"

using namespace juce;

int runSelfTest();   // SelfTest.cpp

namespace
{
    std::atomic<bool> terminateRequested { false };
    void onTerminateSignal (int) { terminateRequested = true; }

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

       #if JUCE_MAC || JUCE_LINUX || JUCE_BSD
        // A write to a closed pipe/socket must report EPIPE, not kill a resident app.
        std::signal (SIGPIPE, SIG_IGN);
        // SIGTERM / SIGINT (kill, logout, Ctrl+C): leave through the normal shutdown path
        std::signal (SIGTERM, onTerminateSignal);
        std::signal (SIGINT, onTerminateSignal);
        signalWatcher = std::make_unique<SignalWatcher> (*this);
       #endif

        // --log-file F : write startup steps and problems to F (diagnostics)
        if (auto lf = argValue (args, "--log-file"); lf.isNotEmpty())
        {
            logger.reset (new FileLogger (File (lf), "SoundFunnel " + getApplicationVersion() + " " + args.joinIntoString (" ")));
            Logger::setCurrentLogger (logger.get());
        }

        if (args.contains ("--selftest"))
        {
            setApplicationReturnValue (runSelfTest());
            quit();
            return;
        }

        Localisation::install();

        // --silent: testing on one PC, never open an output device (avoids mic -> speaker feedback).
        // --test-tone: the input's audio is not used, so don't open it (no microphone permission
        // prompt), unless it is the only device left: something has to drive the audio callback.
        // Decided before the core starts, so the device is never opened even briefly.
        const bool testTone = args.contains ("--test-tone");
        const bool silent = args.contains ("--silent");
        AppCore::Options opts;
        opts.profile = argValue (args, "--profile");
        opts.openOutput = ! silent;
        opts.openInput = ! (testTone && ! silent);

        Logger::writeToLog (String ("starting core (input ") + (opts.openInput ? "on" : "off") + ", output " + (opts.openOutput ? "on" : "off") + ")");
        core = std::make_unique<AppCore> (opts);
        core->getEngine().setTestTone (testTone);
        Logger::writeToLog ("core started: " + core->getStatusText() + ", audio port " + String (core->getEngine().getPort()));

        settings = std::make_unique<SettingsWindow> (*core, [this] { systemRequestedQuit(); });

        const bool background = args.contains ("--background") && core->getConfig().getGroupKey().isNotEmpty();

       #if JUCE_LINUX || JUCE_BSD
        // No tray on Linux: JUCE's Linux tray icon uses XEmbed, and on GNOME (Wayland/Xwayland)
        // embedding it crashed gnome-shell with BadWindow. Run as a normal window instead;
        // "background" starts it minimised.
        settings->showAndFocus();
        if (background) settings->setMinimised (true);
       #else
        tray = std::make_unique<TrayIcon> (*core,
                                           [this] { settings->showAndFocus(); },
                                           [this] { systemRequestedQuit(); });
        if (! background) settings->showAndFocus();
       #endif

        // diagnostics / automated testing:
        //   --status-file F   : write the current state as JSON every second
        //   --control-file F  : poll F for one command ("server", "pause", "resume", "mute <name>", "unmute <name>")
        statusFile = argValue (args, "--status-file");
        controlFile = argValue (args, "--control-file");
        if (statusFile.isNotEmpty() || controlFile.isNotEmpty())
            diag = std::make_unique<DiagTimer> (*this);

        Logger::writeToLog ("initialised");
    }

    struct SignalWatcher : public Timer
    {
        explicit SignalWatcher (SoundFunnelApplication& a) : app (a) { startTimer (200); }
        void timerCallback() override
        {
            if (terminateRequested.exchange (false))
            {
                Logger::writeToLog ("termination signal received");
                app.systemRequestedQuit();
            }
        }
        SoundFunnelApplication& app;
    };

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
            auto ds = core->getDiscovery().getStats();
            o->setProperty ("discovery", String::formatted ("raw=%llu ok=%llu bad=%llu sent=%llu senderr=%llu if=%d",
                                                            ds.rawRx, ds.accepted, ds.rejected, ds.sent, ds.sendErrors, ds.interfaces));
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
        Logger::writeToLog ("shutdown");
        signalWatcher.reset();
        diag.reset();
        tray.reset();
        settings.reset();
        core.reset();
        Logger::setCurrentLogger (nullptr);
        logger.reset();
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
    std::unique_ptr<FileLogger> logger;
    std::unique_ptr<SignalWatcher> signalWatcher;
    String statusFile, controlFile;
};

START_JUCE_APPLICATION (SoundFunnelApplication)
