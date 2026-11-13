// SPDX-License-Identifier: GPL-3.0-or-later
// macOS: give the menu-bar icon a native menu.
//
// JUCE's SystemTrayIconComponent opens its menu with -[NSStatusItem popUpStatusItemMenu:]
// from the click handler; that call is deprecated and showed nothing for this
// menu-bar-only (LSUIElement) app. JUCE's NSMenu conversion also only runs item actions
// when a MenuBarModel is installed. So the menu is built here as a plain NSMenu, attached
// to the status item (AppKit opens it on click), with actions dispatched to the message thread.
#include <juce_gui_extra/juce_gui_extra.h>
#import <AppKit/AppKit.h>

namespace
{
    std::vector<std::function<void()>>& menuActions()
    {
        static std::vector<std::function<void()>> actions;
        return actions;
    }
}

@interface SoundFunnelMenuTarget : NSObject
- (void) invoke: (NSMenuItem*) sender;
@end

@implementation SoundFunnelMenuTarget
- (void) invoke: (NSMenuItem*) sender
{
    const auto index = (size_t) [sender tag];
    auto& actions = menuActions();
    if (index < actions.size() && actions[index])
        juce::MessageManager::callAsync (actions[index]);
}
@end

void setMacStatusItemMenu (void* nativeStatusItem, const juce::PopupMenu& menu)
{
    auto* statusItem = static_cast<NSStatusItem*> (nativeStatusItem);
    if (statusItem == nil) return;

    static SoundFunnelMenuTarget* target = [[SoundFunnelMenuTarget alloc] init];   // lives for the process

    auto& actions = menuActions();
    actions.clear();

    NSMenu* m = [[NSMenu alloc] initWithTitle: @"SoundFunnel"];
    [m setAutoenablesItems: NO];

    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
    {
        const auto& i = it.getItem();
        if (i.isSeparator)
        {
            [m addItem: [NSMenuItem separatorItem]];
            continue;
        }

        NSString* title = [NSString stringWithUTF8String: i.text.toRawUTF8()];
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle: title != nil ? title : @""
                                                      action: nil
                                               keyEquivalent: @""];
        if (i.isSectionHeader)
        {
            [item setEnabled: NO];
        }
        else
        {
            [item setTarget: target];
            [item setAction: @selector (invoke:)];
            [item setTag: (NSInteger) actions.size()];
            [item setEnabled: i.isEnabled ? YES : NO];
            [item setState: i.isTicked ? NSControlStateValueOn : NSControlStateValueOff];
            actions.push_back (i.action);
        }
        [m addItem: item];
       #if ! __has_feature(objc_arc)
        [item release];
       #endif
    }

    [statusItem setMenu: m];
   #if ! __has_feature(objc_arc)
    [m release];   // the status item keeps its own reference
   #endif
}
