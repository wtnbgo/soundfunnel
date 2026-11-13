// SPDX-License-Identifier: GPL-3.0-or-later
#include "DefaultDevices.h"

using namespace juce;

#if JUCE_WINDOWS
#include <windows.h>
#include <mmdeviceapi.h>
#include <propsys.h>

namespace
{
    const PROPERTYKEY kFriendlyName = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

    template <typename T> void release (T*& p) { if (p != nullptr) { p->Release(); p = nullptr; } }
}

bool DefaultDevices::isSupported() { return true; }

String DefaultDevices::getName (bool input)
{
    // may be called on a thread that already initialised COM in another mode: then just use it
    const bool comOk = SUCCEEDED (CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED));

    String name;
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IPropertyStore* store = nullptr;

    if (SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                     __uuidof (IMMDeviceEnumerator), (void**) &en))
        && SUCCEEDED (en->GetDefaultAudioEndpoint (input ? eCapture : eRender, eConsole, &dev))
        && SUCCEEDED (dev->OpenPropertyStore (STGM_READ, &store)))
    {
        PROPVARIANT v;
        PropVariantInit (&v);
        if (SUCCEEDED (store->GetValue (kFriendlyName, &v)) && v.vt == VT_LPWSTR)
            name = String (v.pwszVal);
        PropVariantClear (&v);
    }

    release (store);
    release (dev);
    release (en);
    if (comOk) CoUninitialize();
    return name;
}

#else
bool DefaultDevices::isSupported()        { return false; }
String DefaultDevices::getName (bool)     { return {}; }
#endif
