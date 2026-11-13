// SPDX-License-Identifier: GPL-3.0-or-later
#include "Autostart.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

using namespace juce;

namespace
{
    File executable() { return File::getSpecialLocation (File::currentExecutableFile); }

    /** A path as one quoted argument of a desktop entry Exec key: the reserved characters
        " ` $ \ are backslash-escaped inside the quotes, and every backslash is doubled
        again because the value itself is an escaped string (Desktop Entry spec). */
    String desktopExecArg (const String& path)
    {
        String quoted;
        for (auto c : path)
        {
            if (c == '"' || c == '`' || c == '$' || c == '\\') quoted << "\\\\";
            quoted << String::charToString (c);
        }
        return "\"" + quoted + "\"";
    }

    /** A plain desktop entry string value (Icon=). */
    String desktopValue (const String& s) { return s.replace ("\\", "\\\\"); }

    /** Line breaks would start a new key: never write such a path. */
    bool safeForDesktopEntry (const String& path) { return ! path.containsAnyOf ("\r\n"); }

    String xmlEscape (const String& s)
    {
        return s.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;")
                .replace ("\"", "&quot;").replace ("'", "&apos;");
    }

   #if JUCE_WINDOWS
    const String kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\SoundFunnel";
   #elif JUCE_MAC
    File plistFile() { return File ("~/Library/LaunchAgents/io.github.wtnbgo.soundfunnel.plist"); }
    File appBundle()
    {
        auto f = executable();     // .../SoundFunnel.app/Contents/MacOS/SoundFunnel
        return f.getParentDirectory().getParentDirectory().getParentDirectory();
    }
   #else
    File desktopFile() { return File ("~/.config/autostart/soundfunnel.desktop"); }
   #endif
}

bool Autostart::isEnabled()
{
   #if JUCE_WINDOWS
    return WindowsRegistry::valueExists (kRunKey);
   #elif JUCE_MAC
    return plistFile().existsAsFile();
   #else
    return desktopFile().existsAsFile();
   #endif
}

bool Autostart::setEnabled (bool on)
{
   #if JUCE_WINDOWS
    if (on)
        return WindowsRegistry::setValue (kRunKey, executable().getFullPathName().quoted() + " --background");
    WindowsRegistry::deleteValue (kRunKey);
    return true;
   #elif JUCE_MAC
    if (! on) return plistFile().deleteFile();
    String plist;
    plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
          << "<plist version=\"1.0\"><dict>\n"
          << "<key>Label</key><string>io.github.wtnbgo.soundfunnel</string>\n"
          << "<key>ProgramArguments</key><array><string>/usr/bin/open</string><string>-a</string><string>"
          << xmlEscape (appBundle().getFullPathName()) << "</string><string>--args</string><string>--background</string></array>\n"
          << "<key>RunAtLoad</key><true/>\n"
          << "</dict></plist>\n";
    plistFile().getParentDirectory().createDirectory();
    return plistFile().replaceWithText (plist, false, false, "\n");
   #else
    if (! on) return desktopFile().deleteFile();
    if (! safeForDesktopEntry (executable().getFullPathName())) return false;
    String d;
    d << "[Desktop Entry]\nType=Application\nName=SoundFunnel\n"
      << "Exec=" << desktopExecArg (executable().getFullPathName()) << " --background\n"
      << "StartupWMClass=SoundFunnel\n"
      << "X-GNOME-Autostart-enabled=true\n";
    desktopFile().getParentDirectory().createDirectory();
    return desktopFile().replaceWithText (d, false, false, "\n");
   #endif
}

void Autostart::ensureDesktopEntry()
{
   #if JUCE_LINUX || JUCE_BSD
    const auto home = File::getSpecialLocation (File::userHomeDirectory);
    auto icon = home.getChildFile (".local/share/icons/hicolor/256x256/apps/soundfunnel.png");
    auto desktop = home.getChildFile (".local/share/applications/soundfunnel.desktop");

    // Installed from the .deb: the system entry is the one to use. A user entry written by an
    // earlier unpackaged run would shadow it (and launch that old build), so remove ours.
    for (auto* sys : { "/usr/share/applications/soundfunnel.desktop", "/usr/local/share/applications/soundfunnel.desktop" })
    {
        if (File (sys).existsAsFile())
        {
            const auto text = desktop.loadFileAsString();
            if (text.contains ("StartupWMClass=SoundFunnel") && text.contains ("Name=SoundFunnel"))
            {
                desktop.deleteFile();
                icon.deleteFile();
            }
            return;
        }
    }

    if (! safeForDesktopEntry (executable().getFullPathName())) return;

    if (! icon.existsAsFile())
    {
        auto img = ImageCache::getFromMemory (BinaryData::icon_png, BinaryData::icon_pngSize)
                       .rescaled (256, 256, Graphics::highResamplingQuality);
        icon.getParentDirectory().createDirectory();
        FileOutputStream out (icon);
        if (out.openedOk()) PNGImageFormat().writeImageToStream (img, out);
    }

    String d;
    d << "[Desktop Entry]\nType=Application\nName=SoundFunnel\n"
      << "Comment=Gather audio from several PCs on a LAN onto one server PC\n"
      << "Exec=" << desktopExecArg (executable().getFullPathName()) << "\n"
      << "Icon=" << desktopValue (icon.getFullPathName()) << "\n"
      << "StartupWMClass=SoundFunnel\n"
      << "Categories=AudioVideo;Audio;\nTerminal=false\n";

    // rewrite only when something changed (e.g. the app was moved)
    if (desktop.loadFileAsString() != d)
    {
        desktop.getParentDirectory().createDirectory();
        desktop.replaceWithText (d, false, false, "\n");
    }
   #endif
}
