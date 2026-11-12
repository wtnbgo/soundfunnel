// SPDX-License-Identifier: GPL-3.0-or-later
#include "LinuxTray.h"
#include "TrayIcon.h"

#if JUCE_LINUX || JUCE_BSD
#include <dlfcn.h>
#include <unistd.h>

using namespace juce;

/** The few GTK / GLib / AppIndicator functions used, resolved with dlsym. */
struct LinuxTray::Api
{
    using gboolean = int;
    using GCallback = void (*)();

    gboolean (*gtk_init_check) (int*, char***) = nullptr;
    void* (*gtk_menu_new)() = nullptr;
    void* (*gtk_menu_item_new_with_label) (const char*) = nullptr;
    void* (*gtk_check_menu_item_new_with_label) (const char*) = nullptr;
    void  (*gtk_check_menu_item_set_active) (void*, gboolean) = nullptr;
    void* (*gtk_separator_menu_item_new)() = nullptr;
    void  (*gtk_menu_shell_append) (void*, void*) = nullptr;
    void  (*gtk_menu_item_set_label) (void*, const char*) = nullptr;
    void  (*gtk_widget_set_sensitive) (void*, gboolean) = nullptr;
    void  (*gtk_widget_show_all) (void*) = nullptr;
    unsigned long (*g_signal_connect_data) (void*, const char*, GCallback, void*, void*, int) = nullptr;
    gboolean (*g_main_context_iteration) (void*, gboolean) = nullptr;
    void  (*g_object_unref) (void*) = nullptr;

    void* (*app_indicator_new_with_path) (const char*, const char*, int, const char*) = nullptr;
    void  (*app_indicator_set_status) (void*, int) = nullptr;
    void  (*app_indicator_set_menu) (void*, void*) = nullptr;
    void  (*app_indicator_set_icon_full) (void*, const char*, const char*) = nullptr;
    void  (*app_indicator_set_title) (void*, const char*) = nullptr;

    // GIO / GLib, to ask D-Bus whether a tray host is running
    void* (*g_bus_get_sync) (int, void*, void**) = nullptr;
    void* (*g_variant_new) (const char*, ...) = nullptr;
    void* (*g_dbus_connection_call_sync) (void*, const char*, const char*, const char*, const char*,
                                          void*, const void*, int, int, void*, void**) = nullptr;
    void  (*g_variant_get) (void*, const char*, ...) = nullptr;
    void  (*g_variant_unref) (void*) = nullptr;
    void  (*g_error_free) (void*) = nullptr;

    bool ok = false;

    Api()
    {
        void* lib = nullptr;
        for (auto* name : { "libayatana-appindicator3.so.1", "libappindicator3.so.1" })
            if ((lib = dlopen (name, RTLD_NOW | RTLD_GLOBAL)) != nullptr)
                break;
        if (lib == nullptr) return;

        // GTK and GLib come in as dependencies of the indicator library
        auto get = [] (auto& fn, const char* sym) { fn = reinterpret_cast<std::remove_reference_t<decltype (fn)>> (dlsym (RTLD_DEFAULT, sym)); return fn != nullptr; };
        ok = get (gtk_init_check, "gtk_init_check")
          && get (gtk_menu_new, "gtk_menu_new")
          && get (gtk_menu_item_new_with_label, "gtk_menu_item_new_with_label")
          && get (gtk_check_menu_item_new_with_label, "gtk_check_menu_item_new_with_label")
          && get (gtk_check_menu_item_set_active, "gtk_check_menu_item_set_active")
          && get (gtk_separator_menu_item_new, "gtk_separator_menu_item_new")
          && get (gtk_menu_shell_append, "gtk_menu_shell_append")
          && get (gtk_menu_item_set_label, "gtk_menu_item_set_label")
          && get (gtk_widget_set_sensitive, "gtk_widget_set_sensitive")
          && get (gtk_widget_show_all, "gtk_widget_show_all")
          && get (g_signal_connect_data, "g_signal_connect_data")
          && get (g_main_context_iteration, "g_main_context_iteration")
          && get (g_object_unref, "g_object_unref")
          && get (app_indicator_new_with_path, "app_indicator_new_with_path")
          && get (app_indicator_set_status, "app_indicator_set_status")
          && get (app_indicator_set_menu, "app_indicator_set_menu")
          && get (app_indicator_set_icon_full, "app_indicator_set_icon_full")
          && get (app_indicator_set_title, "app_indicator_set_title")
          && get (g_bus_get_sync, "g_bus_get_sync")
          && get (g_variant_new, "g_variant_new")
          && get (g_dbus_connection_call_sync, "g_dbus_connection_call_sync")
          && get (g_variant_get, "g_variant_get")
          && get (g_variant_unref, "g_variant_unref")
          && get (g_error_free, "g_error_free");

        // gtk_init_check is done in create(), only when a tray host exists
    }

    /** True when a StatusNotifierItem host is on the session bus. Without one the indicator
        is invisible (or may fall back to an XEmbed icon), so the tray must not be used. */
    bool trayHostRunning()
    {
        constexpr int kBusSession = 2;
        void* err = nullptr;
        void* bus = g_bus_get_sync (kBusSession, nullptr, &err);
        if (bus == nullptr) { if (err) g_error_free (err); return false; }

        int has = 0;
        void* reply = g_dbus_connection_call_sync (bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                   "org.freedesktop.DBus", "NameHasOwner",
                                                   g_variant_new ("(s)", "org.kde.StatusNotifierWatcher"),
                                                   nullptr, 0, 1000, nullptr, &err);
        if (reply != nullptr)
        {
            g_variant_get (reply, "(b)", &has);
            g_variant_unref (reply);
        }
        if (err) g_error_free (err);
        g_object_unref (bus);
        return has != 0;
    }

    static Api& get()
    {
        static Api a;
        return a;
    }
};

namespace
{
    constexpr int kCategoryApplicationStatus = 0;
    constexpr int kStatusActive = 1;
}

std::unique_ptr<LinuxTray> LinuxTray::create (AppCore& c, std::function<void()> o, std::function<void()> q)
{
    if (SystemStats::getEnvironmentVariable ("SOUNDFUNNEL_NO_TRAY", {}).isNotEmpty())
        return {};
    auto& api = Api::get();
    if (! api.ok)
    {
        Logger::writeToLog ("tray: AppIndicator library not available, running as a window");
        return {};
    }
    if (! api.trayHostRunning())
    {
        // e.g. GNOME without the AppIndicator extension
        Logger::writeToLog ("tray: no StatusNotifierWatcher on the session bus, running as a window");
        return {};
    }
    static const bool gtkReady = api.gtk_init_check (nullptr, nullptr) != 0;
    if (! gtkReady)
    {
        Logger::writeToLog ("tray: GTK could not be initialised, running as a window");
        return {};
    }
    return std::unique_ptr<LinuxTray> (new LinuxTray (c, api, std::move (o), std::move (q)));
}

LinuxTray::LinuxTray (AppCore& c, Api& a, std::function<void()> o, std::function<void()> q)
    : core (c), api (a), openSettings (std::move (o)), quit (std::move (q))
{
    // the indicator host loads icons by name from this folder
    iconDir = File::getSpecialLocation (File::tempDirectory).getChildFile ("soundfunnel-tray-" + String (getpid()));
    iconDir.createDirectory();
    for (auto role : { AppCore::Role::NoGroup, AppCore::Role::Client, AppCore::Role::Server })
    {
        auto img = TrayIcon::makeIcon (role, 64);
        FileOutputStream out (iconDir.getChildFile (iconNameFor (role) + ".png"));
        if (out.openedOk())
        {
            out.setPosition (0);
            out.truncate();
            PNGImageFormat().writeImageToStream (img, out);
        }
    }

    indicator = api.app_indicator_new_with_path ("soundfunnel", iconNameFor (AppCore::Role::NoGroup).toRawUTF8(),
                                                 kCategoryApplicationStatus, iconDir.getFullPathName().toRawUTF8());
    api.app_indicator_set_title (indicator, "SoundFunnel");

    menu = api.gtk_menu_new();
    auto add = [this] (void* item, void (*fn) (void*, void*))
    {
        if (fn != nullptr)
            api.g_signal_connect_data (item, "activate", reinterpret_cast<Api::GCallback> (fn), this, nullptr, 0);
        api.gtk_menu_shell_append (menu, item);
        return item;
    };

    statusItem = add (api.gtk_menu_item_new_with_label ("SoundFunnel"), nullptr);
    api.gtk_widget_set_sensitive (statusItem, 0);
    add (api.gtk_separator_menu_item_new(), nullptr);
    serverItem = add (api.gtk_menu_item_new_with_label (TRANS ("Make this PC the server").toRawUTF8()), &LinuxTray::onServer);
    pauseItem  = add (api.gtk_check_menu_item_new_with_label (TRANS ("Pause sending").toRawUTF8()), &LinuxTray::onPause);
    add (api.gtk_separator_menu_item_new(), nullptr);
    add (api.gtk_menu_item_new_with_label (TRANS ("Open settings...").toRawUTF8()), &LinuxTray::onSettings);
    add (api.gtk_menu_item_new_with_label (TRANS ("Quit").toRawUTF8()), &LinuxTray::onQuit);
    api.gtk_widget_show_all (menu);

    api.app_indicator_set_menu (indicator, menu);
    api.app_indicator_set_status (indicator, kStatusActive);

    core.addChangeListener (this);
    refresh();
    startTimer (30);
    Logger::writeToLog ("tray: AppIndicator icon created");
}

LinuxTray::~LinuxTray()
{
    stopTimer();
    core.removeChangeListener (this);
    if (indicator != nullptr) api.g_object_unref (indicator);   // removes the icon
    while (api.g_main_context_iteration (nullptr, 0)) {}
    iconDir.deleteRecursively();
}

String LinuxTray::iconNameFor (AppCore::Role r)
{
    return r == AppCore::Role::Server ? "soundfunnel-server"
         : r == AppCore::Role::Client ? "soundfunnel-client"
                                      : "soundfunnel-idle";
}

void LinuxTray::timerCallback()
{
    // let GTK handle D-Bus traffic and menu clicks; bounded so a busy loop can't stall JUCE
    for (int i = 0; i < 50 && api.g_main_context_iteration (nullptr, 0); ++i) {}
}

void LinuxTray::changeListenerCallback (ChangeBroadcaster*) { refresh(); }

void LinuxTray::refresh()
{
    const ScopedValueSetter<bool> svs (updating, true);   // set_active below must not toggle the pause state

    const auto role = core.getRole();
    if (role != shownRole)
    {
        shownRole = role;
        api.app_indicator_set_icon_full (indicator, iconNameFor (role).toRawUTF8(), "SoundFunnel");
    }

    const auto status = core.getStatusText();
    if (status != shownStatus)
    {
        shownStatus = status;
        api.gtk_menu_item_set_label (statusItem, status.toRawUTF8());
    }

    api.gtk_widget_set_sensitive (serverItem, role != AppCore::Role::Server && role != AppCore::Role::NoGroup);
    api.gtk_check_menu_item_set_active (pauseItem, core.isPaused());
}

void LinuxTray::onServer (void*, void* self)
{
    static_cast<LinuxTray*> (self)->core.becomeServer();
}

void LinuxTray::onPause (void*, void* self)
{
    auto* t = static_cast<LinuxTray*> (self);
    if (! t->updating)
        t->core.setPaused (! t->core.isPaused());
}

void LinuxTray::onSettings (void*, void* self)
{
    auto* t = static_cast<LinuxTray*> (self);
    if (t->openSettings) MessageManager::callAsync ([t] { t->openSettings(); });
}

void LinuxTray::onQuit (void*, void* self)
{
    auto* t = static_cast<LinuxTray*> (self);
    if (t->quit) MessageManager::callAsync ([q = t->quit] { q(); });
}

#endif
