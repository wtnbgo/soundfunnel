// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrayIcon.h"

using namespace juce;

TrayIcon::TrayIcon (AppCore& c, std::function<void()> o, std::function<void()> q)
    : core (c), openSettings (std::move (o)), quit (std::move (q))
{
    core.addChangeListener (this);
    refresh();
}

TrayIcon::~TrayIcon()
{
    core.removeChangeListener (this);
}

Image TrayIcon::makeIcon (AppCore::Role role, int size)
{
    Image img (Image::ARGB, size, size, true);
    Graphics g (img);
    auto r = Rectangle<float> (0, 0, (float) size, (float) size).reduced (size * 0.06f);

    Colour c = role == AppCore::Role::Server ? Colour (0xff2e9bff)    // blue: playing everything
             : role == AppCore::Role::Client ? Colour (0xff33c26b)    // green: sending
                                             : Colour (0xff9a9a9a);   // grey: idle
    g.setColour (c);
    g.fillEllipse (r);

    // three "sound" arcs converging into a dot
    g.setColour (Colours::white);
    auto centre = r.getCentre();
    const float s = r.getWidth();
    g.fillEllipse (Rectangle<float> (s * 0.18f, s * 0.18f).withCentre (centre));
    for (int i = 1; i <= 2; ++i)
    {
        Path p;
        auto rad = s * (0.16f + 0.13f * (float) i);
        p.addCentredArc (centre.x, centre.y, rad, rad, 0.0f, -0.9f, 0.9f, true);
        g.strokePath (p, PathStrokeType (s * 0.07f, PathStrokeType::curved, PathStrokeType::rounded));
        Path q;
        q.addCentredArc (centre.x, centre.y, rad, rad, 0.0f, MathConstants<float>::pi - 0.9f, MathConstants<float>::pi + 0.9f, true);
        g.strokePath (q, PathStrokeType (s * 0.07f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    return img;
}

void TrayIcon::refresh()
{
    if (core.getRole() != shownRole)
    {
        shownRole = core.getRole();
        auto img = makeIcon (shownRole, 64);
        setIconImage (img, img);
    }
    setIconTooltip ("spsync - " + core.getStatusText());
}

void TrayIcon::changeListenerCallback (ChangeBroadcaster*) { refresh(); }

void TrayIcon::mouseDown (const MouseEvent& e)
{
   #if JUCE_WINDOWS
    // left click opens settings, right click shows the menu (Windows convention)
    if (! e.mods.isPopupMenu())
    {
        if (openSettings) openSettings();
        return;
    }
   #endif
    ignoreUnused (e);
    showMenu();
}

void TrayIcon::mouseDoubleClick (const MouseEvent&)
{
    if (openSettings) openSettings();
}

void TrayIcon::showMenu()
{
    PopupMenu m;
    m.addSectionHeader (core.getStatusText());
    m.addSeparator();
    m.addItem (TRANS ("Make this PC the server"), core.getRole() != AppCore::Role::Server && core.getRole() != AppCore::Role::NoGroup,
               false, [this] { core.becomeServer(); });
    m.addItem (TRANS ("Pause sending"), true, core.isPaused(), [this] { core.setPaused (! core.isPaused()); });
    m.addSeparator();
    m.addItem (TRANS ("Open settings..."), [this] { if (openSettings) openSettings(); });
    m.addItem (TRANS ("Quit"), [this] { if (quit) quit(); });

   #if JUCE_MAC
    showDropdownMenu (m);
   #else
    // a hidden window must be in front for the menu to dismiss correctly on Windows
    Process::makeForegroundProcess();
    m.showMenuAsync (PopupMenu::Options().withMousePosition());
   #endif
}
