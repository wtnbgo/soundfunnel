// SoundFunnel - aggregate audio from several PCs on a LAN to one server PC.
// Based on SonoBus (https://www.sonobus.net, Copyright (C) Jesse Chappell), GPLv3.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_extra/juce_gui_extra.h>
#include <csignal>

#include "AppCore.h"
#include "Localisation.h"
#include "SettingsWindow.h"
#include "TrayIcon.h"
#if JUCE_LINUX || JUCE_BSD
 #include "LinuxTray.h"
#endif

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
            logger.reset (new TimestampedLogger (File (lf), "SoundFunnel " + getApplicationVersion() + " " + args.joinIntoString (" ")));
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
        // --loopback: send what this PC plays instead of the input (this run only;
        // --loopback-device NAME picks an output device, default = the default output)
        const bool loopbackArg = args.contains ("--loopback");
        AppCore::Options opts;
        opts.profile = argValue (args, "--profile");
        opts.openOutput = ! silent;
        opts.openInput = ! loopbackArg && ! (testTone && ! silent);

        Logger::writeToLog (String ("starting core (input ") + (opts.openInput ? "on" : "off") + ", output " + (opts.openOutput ? "on" : "off") + ")");
        core = std::make_unique<AppCore> (opts);
        core->getEngine().setTestTone (testTone);
        if (loopbackArg)
            core->applySendSource (AppConfig::Source::loopback, argValue (args, "--loopback-device"), false);
        Logger::writeToLog ("core started: " + core->getStatusText() + ", audio port " + String (core->getEngine().getPort()));

       #if JUCE_LINUX || JUCE_BSD
        // AppIndicator tray when available; created first so the window knows whether it can hide
        linuxTray = LinuxTray::create (*core,
                                       [this] { if (settings) settings->showAndFocus(); },
                                       [this] { systemRequestedQuit(); });
        SettingsWindow::setTrayAvailable (linuxTray != nullptr);
       #endif

        settings = std::make_unique<SettingsWindow> (*core, [this] { systemRequestedQuit(); });
        Logger::writeToLog ("settings window created");

        const bool background = args.contains ("--background") && core->getConfig().getGroupKey().isNotEmpty();

       #if JUCE_LINUX || JUCE_BSD
        // JUCE's Linux tray icon (XEmbed) crashed gnome-shell, so it is never used.
        // With the AppIndicator tray the window behaves as on the other OSes; without it
        // the app runs as a normal window and "background" starts it minimised.
        if (linuxTray != nullptr)
        {
            if (! background) settings->showAndFocus();
        }
        else
        {
            settings->showAndFocus();
            if (background) settings->setMinimised (true);
        }
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
            if (core->getEngine().isLoopbackEnabled())
                o->setProperty ("loopback", core->getEngine().getLoopbackStatus());
            o->setProperty ("sendLevel", core->getEngine().getSendLevel());
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
       #if JUCE_LINUX || JUCE_BSD
        linuxTray.reset();
       #endif
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
   #if JUCE_LINUX || JUCE_BSD
    std::unique_ptr<LinuxTray> linuxTray;
   #endif
    std::unique_ptr<DiagTimer> diag;
    /** FileLogger with a time stamp on every line (helps to see where startup waits). */
    struct TimestampedLogger : public FileLogger
    {
        using FileLogger::FileLogger;
        void logMessage (const String& m) override
        {
            FileLogger::logMessage (Time::getCurrentTime().formatted ("%H:%M:%S.") + String (Time::getCurrentTime().getMilliseconds()).paddedLeft ('0', 3) + " " + m);
        }
    };
    std::unique_ptr<FileLogger> logger;
    std::unique_ptr<SignalWatcher> signalWatcher;
    String statusFile, controlFile;
};

START_JUCE_APPLICATION (SoundFunnelApplication)
