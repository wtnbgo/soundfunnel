// SPDX-License-Identifier: GPL-3.0-or-later
#include "SettingsWindow.h"
#include "Autostart.h"
#include "Codecs.h"
#include "BinaryData.h"
#include "LoopbackCapture.h"
#include "DefaultDevices.h"

#include <juce_audio_utils/juce_audio_utils.h>

using namespace juce;

namespace
{
    //==========================================================================
    /** Simple horizontal peak meter. */
    struct Meter : public Component
    {
        float level = 0.0f;
        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (r, 2.0f);
            auto db = Decibels::gainToDecibels (level, -60.0f);
            auto w = jmap (db, -60.0f, 0.0f, 0.0f, r.getWidth());
            g.setColour (db > -3.0f ? Colours::orangered : Colour (0xff33c26b));
            g.fillRoundedRectangle (r.withWidth (w), 2.0f);
        }
    };

    //==========================================================================
    /** One row in the member list. */
    struct MemberRow : public Component
    {
        explicit MemberRow (AppCore& c) : core (c)
        {
            addAndMakeVisible (name);
            addAndMakeVisible (state);
            addAndMakeVisible (mute);
            addAndMakeVisible (gain);
            addAndMakeVisible (meter);
            state.setColour (Label::textColourId, Colours::grey);
            mute.setButtonText (TRANS ("Mute"));
            mute.onClick = [this] { core.setClientMuted (id, mute.getToggleState()); };
            gain.setSliderStyle (Slider::LinearHorizontal);
            gain.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            gain.setRange (0.0, 2.0);
            gain.setSkewFactorFromMidPoint (1.0);
            gain.setTooltip (TRANS ("Volume"));
            gain.onValueChange = [this] { core.setClientGain (id, (float) gain.getValue()); };
        }

        void update (const AppCore::Member& m, bool iAmServer)
        {
            id = m.id;
            String n = m.name;
            if (m.isSelf)   n << " " << TRANS ("(this PC)");
            name.setText (n, dontSendNotification);

            String s;
            if (m.isServer) s << "[" << TRANS ("server") << "] ";
            if (! m.isSelf) s << m.ip << "  ";
            if (iAmServer && ! m.isSelf) s << (m.receiving ? TRANS ("receiving") : TRANS ("silent"));
            else if (! m.isSelf)         s << TRANS ("online");
            state.setText (s, dontSendNotification);

            const bool controls = iAmServer && ! m.isSelf;
            mute.setVisible (controls);
            gain.setVisible (controls);
            if (controls)
            {
                mute.setToggleState (m.muted, dontSendNotification);
                if (! gain.isMouseButtonDown()) gain.setValue (m.gain, dontSendNotification);
            }
            meter.setVisible (controls || (m.isSelf && ! iAmServer));
            meter.level = m.level;
            meter.repaint();
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (4, 2);
            auto right = r.removeFromRight (260);
            auto top = r.removeFromTop (r.getHeight() / 2);
            name.setBounds (top);
            state.setBounds (r);
            mute.setBounds (right.removeFromLeft (80));
            gain.setBounds (right.removeFromLeft (100));
            meter.setBounds (right.reduced (4, getHeight() / 2 - 4));
        }

        void paint (Graphics& g) override
        {
            g.setColour (findColour (ResizableWindow::backgroundColourId).brighter (0.06f));
            g.fillRoundedRectangle (getLocalBounds().reduced (1).toFloat(), 4.0f);
        }

        AppCore& core;
        String id;
        Label name, state;
        ToggleButton mute;
        Slider gain;
        Meter meter;
    };

    //==========================================================================
    class StatusPage : public Component, private ChangeListener, private Timer
    {
    public:
        StatusPage (AppCore& c, std::function<void()> onQuit) : core (c)
        {
            if (onQuit != nullptr && ! SettingsWindow::hasTray())
            {
                quit.setButtonText (TRANS ("Quit"));
                quit.onClick = std::move (onQuit);
                addAndMakeVisible (quit);
            }
            addAndMakeVisible (status);
            status.setFont (Font (17.0f, Font::bold));
            addAndMakeVisible (becomeServer);
            becomeServer.setButtonText (TRANS ("Make this PC the server"));
            becomeServer.onClick = [this] { core.becomeServer(); };
            addAndMakeVisible (pause);
            pause.setButtonText (TRANS ("Pause sending"));
            pause.onClick = [this] { core.setPaused (pause.getToggleState()); };
            addAndMakeVisible (membersLabel);
            membersLabel.setText (TRANS ("Members"), dontSendNotification);
            addAndMakeVisible (viewport);
            viewport.setViewedComponent (&list, false);
            viewport.setScrollBarsShown (true, false);

            core.addChangeListener (this);
            refresh();
            startTimerHz (15);
        }
        ~StatusPage() override { core.removeChangeListener (this); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (12);
            status.setBounds (r.removeFromTop (28));
            r.removeFromTop (6);
            auto row = r.removeFromTop (30);
            becomeServer.setBounds (row.removeFromLeft (220));
            row.removeFromLeft (12);
            pause.setBounds (row.removeFromLeft (200));
            if (quit.isVisible()) quit.setBounds (row.removeFromRight (90));
            r.removeFromTop (10);
            membersLabel.setBounds (r.removeFromTop (22));
            viewport.setBounds (r);
            layoutRows();
        }

    private:
        void layoutRows()
        {
            const int h = 48;
            list.setSize (viewport.getMaximumVisibleWidth(), h * rows.size());
            for (int i = 0; i < rows.size(); ++i)
                rows[i]->setBounds (0, i * h, list.getWidth(), h);
        }

        void refresh()
        {
            status.setText (core.getStatusText(), dontSendNotification);
            const bool iAmServer = core.getRole() == AppCore::Role::Server;
            becomeServer.setEnabled (! iAmServer && core.getRole() != AppCore::Role::NoGroup);
            pause.setToggleState (core.isPaused(), dontSendNotification);
            pause.setEnabled (! iAmServer);

            auto members = core.getMembers();
            bool relayout = members.size() != (size_t) rows.size();
            while (rows.size() < (int) members.size()) list.addAndMakeVisible (rows.add (new MemberRow (core)));
            while (rows.size() > (int) members.size()) rows.removeLast();
            for (size_t i = 0; i < members.size(); ++i) rows[(int) i]->update (members[i], iAmServer);
            if (relayout) layoutRows();
        }

        void changeListenerCallback (ChangeBroadcaster*) override { refresh(); }
        void timerCallback() override                             { if (isShowing()) refresh(); }

        AppCore& core;
        Label status, membersLabel;
        TextButton becomeServer, quit;
        ToggleButton pause;
        Viewport viewport;
        Component list;
        OwnedArray<MemberRow> rows;
    };

    //==========================================================================
    class FormatPage : public Component
    {
    public:
        explicit FormatPage (AppCore& c) : core (c)
        {
            auto& cfg = core.getConfig();

            // what to send: the input device, or what this PC plays
            addLabel (sourceLabel, TRANS ("Send"));
            source.addItem (TRANS ("Audio input (microphone / line in)"), 1);
            source.addItem (TRANS ("System audio (what this PC plays)"), 2);
            source.setItemEnabled (2, LoopbackCapture::isSupported());
            source.setSelectedId (cfg.getSendSource() == AppConfig::Source::loopback && LoopbackCapture::isSupported() ? 2 : 1,
                                  dontSendNotification);
            addAndMakeVisible (source);

            addLabel (lbDeviceLabel, TRANS ("Capture from"));
            lbDevice.addItem (TRANS ("Default output device"), 1);
            int lid = 2, sel = 1;
            for (auto& n : LoopbackCapture::getDeviceNames())
            {
                if (n == cfg.getLoopbackDevice()) sel = lid;
                lbDevice.addItem (n, lid++);
            }
            if (cfg.getLoopbackDevice().isNotEmpty() && sel == 1)
            {
                // remembered device is not connected right now: keep it selectable
                lbDevice.addItem (cfg.getLoopbackDevice(), lid);
                sel = lid;
            }
            lbDevice.setSelectedId (sel, dontSendNotification);
            addAndMakeVisible (lbDevice);

            if (! LoopbackCapture::isSupported())
            {
                lbNote.setText (TRANS ("Sending system audio is not available here yet. Use a virtual audio device as the input instead."), dontSendNotification);
                lbNote.setColour (Label::textColourId, Colours::grey);
                addAndMakeVisible (lbNote);
            }

            auto applySource = [this] {
                const bool lb = source.getSelectedId() == 2;
                const auto dev = lbDevice.getSelectedId() <= 1 ? String() : lbDevice.getText();
                core.applySendSource (lb ? AppConfig::Source::loopback : AppConfig::Source::input, dev);
                lbDevice.setEnabled (lb);
            };
            source.onChange = applySource;
            lbDevice.onChange = applySource;
            lbDevice.setEnabled (source.getSelectedId() == 2);

            addLabel (codecLabel, TRANS ("Codec"));
            int id = 1;
            for (auto& f : Codecs::all()) codec.addItem (f.name, id++);
            codec.setSelectedItemIndex (cfg.getFormatIndex(), dontSendNotification);
            addAndMakeVisible (codec);

            addLabel (channelsLabel, TRANS ("Channels"));
            channels.addItem (TRANS ("Mono"), 1);
            channels.addItem (TRANS ("Stereo"), 2);
            channels.setSelectedId (cfg.getSendChannels(), dontSendNotification);
            addAndMakeVisible (channels);

            addLabel (jitterLabel, TRANS ("Jitter buffer (ms)"));
            jitter.setRange (5, 500, 1);
            jitter.setSliderStyle (Slider::LinearHorizontal);
            jitter.setTextBoxStyle (Slider::TextBoxRight, false, 60, 22);
            jitter.setValue (cfg.getJitterBufferMs(), dontSendNotification);
            addAndMakeVisible (jitter);

            monitor.setButtonText (TRANS ("When this PC is the server, also play its own input"));
            monitor.setToggleState (cfg.getMonitorLocalWhenServer(), dontSendNotification);
            addAndMakeVisible (monitor);

            auto apply = [this] {
                core.applyFormat (codec.getSelectedItemIndex(), channels.getSelectedId(),
                                  (int) jitter.getValue(), monitor.getToggleState());
            };
            codec.onChange = apply;
            channels.onChange = apply;
            jitter.onDragEnd = apply;
            monitor.onClick = apply;
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (12);
            auto row = [&r] { auto x = r.removeFromTop (28); r.removeFromTop (8); return x; };
            auto put = [] (Rectangle<int> x, Label& l, Component& c) { l.setBounds (x.removeFromLeft (170)); c.setBounds (x); };
            put (row(), sourceLabel, source);
            put (row(), lbDeviceLabel, lbDevice);
            if (lbNote.isVisible()) lbNote.setBounds (row());
            put (row(), codecLabel, codec);
            put (row(), channelsLabel, channels);
            put (row(), jitterLabel, jitter);
            monitor.setBounds (row());
        }

    private:
        void addLabel (Label& l, const String& t) { l.setText (t, dontSendNotification); addAndMakeVisible (l); }

        AppCore& core;
        Label sourceLabel, lbDeviceLabel, lbNote, codecLabel, channelsLabel, jitterLabel;
        ComboBox source, lbDevice, codec, channels;
        Slider jitter;
        ToggleButton monitor;
    };

    //==========================================================================
    class AudioPage : public Component, private ChangeListener
    {
    public:
        explicit AudioPage (AppCore& c) : core (c)
        {
            hint.setText (TRANS ("Input = what this PC sends. Output = where the server plays."), dontSendNotification);
            hint.setColour (Label::textColourId, Colours::grey);
            addAndMakeVisible (hint);

            // The selector creates the audio device types (and scans devices), so it is only
            // made once AppCore has opened the device: before that a permission prompt may be
            // pending, and a CoreAudio scan would block until it is answered.
            if (DefaultDevices::isSupported())
            {
                followIn.setButtonText (TRANS ("Input: follow the Windows default device"));
                followOut.setButtonText (TRANS ("Output: follow the Windows default device"));
                followIn.onClick  = [this] { core.setFollowDefaultDevice (true,  followIn.getToggleState()); };
                followOut.onClick = [this] { core.setFollowDefaultDevice (false, followOut.getToggleState()); };
                addAndMakeVisible (followIn);
                addAndMakeVisible (followOut);
                refreshFollow();
            }

            waiting.setText (TRANS ("Waiting for audio permission..."), dontSendNotification);
            waiting.setJustificationType (Justification::centred);
            addAndMakeVisible (waiting);

            core.addChangeListener (this);
            createSelectorIfReady();
        }

        ~AudioPage() override
        {
            core.removeChangeListener (this);
            if (selector != nullptr) core.getDeviceManager().removeChangeListener (this);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            hint.setBounds (r.removeFromTop (24));
            if (followIn.isVisible())
            {
                followIn.setBounds (r.removeFromTop (24));
                followOut.setBounds (r.removeFromTop (24));
                r.removeFromTop (4);
            }
            waiting.setBounds (r);
            if (selector != nullptr) selector->setBounds (r);
        }

    private:
        void createSelectorIfReady()
        {
            if (selector != nullptr || ! core.isAudioDeviceOpened()) return;
            selector = std::make_unique<AudioDeviceSelectorComponent> (core.getDeviceManager(), 0, 2, 0, 2, false, false, true, false);
            addAndMakeVisible (*selector);
            waiting.setVisible (false);
            core.getDeviceManager().addChangeListener (this);
            resized();
        }

        void changeListenerCallback (ChangeBroadcaster* src) override
        {
            if (src == &core.getDeviceManager()) core.saveAudioDeviceState();
            else                                 createSelectorIfReady();
            refreshFollow();
        }

        void refreshFollow()
        {
            followIn.setToggleState (core.isFollowingDefaultDevice (true), dontSendNotification);
            followOut.setToggleState (core.isFollowingDefaultDevice (false), dontSendNotification);
        }

        AppCore& core;
        Label hint, waiting;
        ToggleButton followIn, followOut;
        std::unique_ptr<AudioDeviceSelectorComponent> selector;
    };

    //==========================================================================
    class GroupPage : public Component
    {
    public:
        explicit GroupPage (AppCore& c) : core (c)
        {
            auto& cfg = core.getConfig();
            nameLabel.setText (TRANS ("PC name"), dontSendNotification);
            keyLabel.setText (TRANS ("Group key"), dontSendNotification);
            name.setText (cfg.getNodeName(), dontSendNotification);
            key.setText (cfg.getGroupKey(), dontSendNotification);
            key.setPasswordCharacter ((juce_wchar) 0x2022);
            show.setButtonText (TRANS ("Show"));
            show.onClick = [this] { key.setPasswordCharacter (show.getToggleState() ? 0 : (juce_wchar) 0x2022); };
            help.setText (TRANS ("PCs with the same group key find each other automatically on the LAN."), dontSendNotification);
            help.setColour (Label::textColourId, Colours::grey);
            apply.setButtonText (TRANS ("Apply"));
            apply.onClick = [this] { core.applyIdentity (name.getText(), key.getText()); };

            autostart.setButtonText (TRANS ("Start automatically at login"));
            autostart.setToggleState (Autostart::isEnabled(), dontSendNotification);
            autostart.onClick = [this] { Autostart::setEnabled (autostart.getToggleState()); };

            portLabel.setText (TRANS ("Audio port") + ": " + String (core.getEngine().getPort()), dontSendNotification);
            portLabel.setColour (Label::textColourId, Colours::grey);

            for (Component* comp : { (Component*) &nameLabel, (Component*) &keyLabel, (Component*) &name, (Component*) &key,
                                     (Component*) &show, (Component*) &help, (Component*) &apply, (Component*) &autostart,
                                     (Component*) &portLabel })
                addAndMakeVisible (comp);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (12);
            auto row = [&r] { auto x = r.removeFromTop (28); r.removeFromTop (8); return x; };
            auto a = row(); nameLabel.setBounds (a.removeFromLeft (120)); name.setBounds (a);
            auto b = row(); keyLabel.setBounds (b.removeFromLeft (120)); show.setBounds (b.removeFromRight (80)); key.setBounds (b);
            help.setBounds (row());
            apply.setBounds (row().removeFromLeft (120));
            r.removeFromTop (12);
            autostart.setBounds (row());
            portLabel.setBounds (row());
        }

    private:
        AppCore& core;
        Label nameLabel, keyLabel, help, portLabel;
        TextEditor name, key;
        ToggleButton show, autostart;
        TextButton apply;
    };

    //==========================================================================
    struct Content : public TabbedComponent
    {
        Content (AppCore& core, std::function<void()> onQuit) : TabbedComponent (TabbedButtonBar::TabsAtTop)
        {
            auto bg = findColour (ResizableWindow::backgroundColourId);
            addTab (TRANS ("Status"), bg, new StatusPage (core, std::move (onQuit)), true);
            addTab (TRANS ("Audio"), bg, new AudioPage (core), true);
            addTab (TRANS ("Sending"), bg, new FormatPage (core), true);
            addTab (TRANS ("Group"), bg, new GroupPage (core), true);
            if (core.getConfig().getGroupKey().isEmpty())
                setCurrentTabIndex (3);
            setSize (640, 480);
        }
    };
}

//==============================================================================
namespace
{
   #if JUCE_LINUX || JUCE_BSD
    bool trayAvailable = false;   // only when the AppIndicator tray could be created
   #else
    bool trayAvailable = true;
   #endif
}

bool SettingsWindow::hasTray()               { return trayAvailable; }
void SettingsWindow::setTrayAvailable (bool b) { trayAvailable = b; }

SettingsWindow::SettingsWindow (AppCore& core, std::function<void()> onQuit)
    : DocumentWindow ("SoundFunnel - " + TRANS ("Settings"),
                      LookAndFeel::getDefaultLookAndFeel().findColour (ResizableWindow::backgroundColourId),
                      DocumentWindow::closeButton | DocumentWindow::minimiseButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (core, std::move (onQuit)), true);
    setResizable (true, false);
    setResizeLimits (520, 380, 1600, 1200);
    centreWithSize (getWidth(), getHeight());
}

void SettingsWindow::closeButtonPressed()
{
    if (hasTray())
        setVisible (false);   // keep running in the tray
    else
        setMinimised (true);  // no tray (Linux): stay reachable from the taskbar
}

void SettingsWindow::showAndFocus()
{
    setVisible (true);
    setMinimised (false);
    toFront (true);
    applyWindowIcon();
}

void SettingsWindow::visibilityChanged()
{
    DocumentWindow::visibilityChanged();
    applyWindowIcon();
}

void SettingsWindow::applyWindowIcon()
{
   #if JUCE_LINUX || JUCE_BSD
    // _NET_WM_ICON, which the dock falls back to when no .desktop file matches. It has to go
    // to the native peer (DocumentWindow::setIcon only draws it in a JUCE title bar), and the
    // peer only exists once the window is on the desktop (it is recreated when the title bar
    // style changes), so apply it whenever the peer is new.
    if (auto* peer = getPeer(); peer != nullptr && peer != iconPeer)
    {
        iconPeer = peer;
        peer->setIcon (ImageCache::getFromMemory (BinaryData::icon_png, BinaryData::icon_pngSize)
                           .rescaled (128, 128, Graphics::highResamplingQuality));
    }
   #endif
}
