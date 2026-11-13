// SPDX-License-Identifier: GPL-3.0-or-later
#include "LoopbackCapture.h"
#include <juce_audio_basics/juce_audio_basics.h>

using namespace juce;

LoopbackCapture::LoopbackCapture() : Thread ("SoundFunnel loopback") {}
LoopbackCapture::~LoopbackCapture() { stop(); }

void LoopbackCapture::start (const String& deviceName, Callback cb)
{
    stop();
    requestedDevice = deviceName;
    callback = std::move (cb);
    setError ({});
    startThread (Thread::Priority::highest);
}

void LoopbackCapture::stop()
{
    stopThread (3000);
    const ScopedLock sl (infoLock);
    description.clear();
}

String LoopbackCapture::getLastError() const    { const ScopedLock sl (infoLock); return lastError; }

bool LoopbackCapture::isSettled() const
{
    const ScopedLock sl (infoLock);
    return description.isNotEmpty() || lastError.isNotEmpty() || ! isThreadRunning();
}
String LoopbackCapture::getDescription() const  { const ScopedLock sl (infoLock); return description; }

void LoopbackCapture::setError (const String& e)
{
    {
        const ScopedLock sl (infoLock);
        if (lastError == e) return;
        lastError = e;
    }
    if (e.isNotEmpty()) Logger::writeToLog ("loopback: " + e);
}

#if JUCE_WINDOWS
//==============================================================================
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <propsys.h>

namespace
{
    // defined locally so no extra import library (ksuser / propsys GUIDs) is needed
    const GUID kSubtypePcm   = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
    const GUID kSubtypeFloat = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
    const PROPERTYKEY kFriendlyName = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

    template <typename T>
    struct ComPtr
    {
        T* p = nullptr;
        ComPtr() = default;
        ComPtr (const ComPtr&) = delete;
        ComPtr& operator= (const ComPtr&) = delete;
        ~ComPtr() { reset(); }
        void reset() { if (p != nullptr) { p->Release(); p = nullptr; } }
        T** put() { reset(); return &p; }
        T* operator->() const { return p; }
        explicit operator bool() const { return p != nullptr; }
    };

    struct ComScope
    {
        ComScope()  { ok = SUCCEEDED (CoInitializeEx (nullptr, COINIT_MULTITHREADED)); }
        ~ComScope() { if (ok) CoUninitialize(); }
        bool ok = false;
    };

    String hr (HRESULT h) { return "0x" + String::toHexString ((int) h); }

    String friendlyName (IMMDevice* d)
    {
        ComPtr<IPropertyStore> store;
        if (FAILED (d->OpenPropertyStore (STGM_READ, store.put()))) return {};
        PROPVARIANT v;
        PropVariantInit (&v);
        String name;
        if (SUCCEEDED (store->GetValue (kFriendlyName, &v)) && v.vt == VT_LPWSTR)
            name = String (v.pwszVal);
        PropVariantClear (&v);
        return name;
    }

    String deviceId (IMMDevice* d)
    {
        LPWSTR id = nullptr;
        String s;
        if (SUCCEEDED (d->GetId (&id))) { s = String (id); CoTaskMemFree (id); }
        return s;
    }

    /** Finds the render endpoint by friendly name; empty name = default output. */
    bool findDevice (IMMDeviceEnumerator* en, const String& name, ComPtr<IMMDevice>& out)
    {
        if (name.isEmpty())
            return SUCCEEDED (en->GetDefaultAudioEndpoint (eRender, eConsole, out.put()));

        ComPtr<IMMDeviceCollection> list;
        if (FAILED (en->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, list.put()))) return false;
        UINT n = 0;
        list->GetCount (&n);
        for (UINT i = 0; i < n; ++i)
        {
            ComPtr<IMMDevice> d;
            if (SUCCEEDED (list->Item (i, d.put())) && friendlyName (d.p) == name)
            {
                out.p = d.p;
                d.p = nullptr;
                return true;
            }
        }
        return false;
    }

    enum class SampleType { unsupported, f32, i16, i24, i32 };

    SampleType sampleTypeOf (const WAVEFORMATEX* wf)
    {
        WORD tag = wf->wFormatTag;
        if (tag == WAVE_FORMAT_EXTENSIBLE && wf->cbSize >= 22)
        {
            auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*> (wf);
            if (IsEqualGUID (ext->SubFormat, kSubtypeFloat)) tag = WAVE_FORMAT_IEEE_FLOAT;
            else if (IsEqualGUID (ext->SubFormat, kSubtypePcm))   tag = WAVE_FORMAT_PCM;
        }
        if (tag == WAVE_FORMAT_IEEE_FLOAT && wf->wBitsPerSample == 32) return SampleType::f32;
        if (tag == WAVE_FORMAT_PCM)
        {
            if (wf->wBitsPerSample == 16) return SampleType::i16;
            if (wf->wBitsPerSample == 24) return SampleType::i24;
            if (wf->wBitsPerSample == 32) return SampleType::i32;   // 24-in-32 is left-aligned, same scaling
        }
        return SampleType::unsupported;
    }

    void deinterleave (SampleType t, const BYTE* src, int frames, int chans, AudioBuffer<float>& dst)
    {
        for (int c = 0; c < chans; ++c)
        {
            auto* d = dst.getWritePointer (c);
            for (int i = 0; i < frames; ++i)
            {
                const auto idx = (size_t) i * (size_t) chans + (size_t) c;
                switch (t)
                {
                    case SampleType::f32: d[i] = reinterpret_cast<const float*> (src)[idx]; break;
                    case SampleType::i16: d[i] = reinterpret_cast<const int16*> (src)[idx] * (1.0f / 32768.0f); break;
                    case SampleType::i24:
                    {
                        auto* b = src + idx * 3;
                        auto v = (int32) (((uint32) b[0] << 8) | ((uint32) b[1] << 16) | ((uint32) b[2] << 24));
                        d[i] = (float) v * (1.0f / 2147483648.0f);
                        break;
                    }
                    case SampleType::i32: d[i] = (float) reinterpret_cast<const int32*> (src)[idx] * (1.0f / 2147483648.0f); break;
                    case SampleType::unsupported: d[i] = 0.0f; break;
                }
            }
        }
    }
}

bool LoopbackCapture::isSupported() { return true; }

StringArray LoopbackCapture::getDeviceNames()
{
    StringArray names;
    ComScope com;
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof (IMMDeviceEnumerator), (void**) en.put())))
        return names;

    ComPtr<IMMDeviceCollection> list;
    if (FAILED (en->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, list.put()))) return names;
    UINT n = 0;
    list->GetCount (&n);
    for (UINT i = 0; i < n; ++i)
    {
        ComPtr<IMMDevice> d;
        if (SUCCEEDED (list->Item (i, d.put())))
            names.addIfNotAlreadyThere (friendlyName (d.p));
    }
    names.removeEmptyStrings();
    return names;
}

void LoopbackCapture::run()
{
    ComScope com;

    while (! threadShouldExit())
    {
        // one capture session; on any failure (device unplugged, default device
        // changed, format change...) tear down and start over after a short pause
        ComPtr<IMMDeviceEnumerator> en;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioCaptureClient> capture;
        WAVEFORMATEX* wf = nullptr;

        auto fail = [&] (const String& e) { setError (e); if (wf) CoTaskMemFree (wf); wf = nullptr; };

        HRESULT h = CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      __uuidof (IMMDeviceEnumerator), (void**) en.put());
        if (FAILED (h)) { fail ("device enumerator " + hr (h)); wait (1000); continue; }

        if (! findDevice (en.p, requestedDevice, device))
        {
            fail (requestedDevice.isEmpty() ? String ("no default output device")
                                            : "output device not found: " + requestedDevice);
            wait (2000);
            continue;
        }
        const String devId = deviceId (device.p);
        const String devName = friendlyName (device.p);

        h = device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) client.put());
        if (FAILED (h)) { fail ("activate " + hr (h)); wait (1000); continue; }

        h = client->GetMixFormat (&wf);
        if (FAILED (h) || wf == nullptr) { fail ("mix format " + hr (h)); wait (1000); continue; }

        const auto type = sampleTypeOf (wf);
        const int chans = (int) wf->nChannels;
        const double sr = (double) wf->nSamplesPerSec;
        if (type == SampleType::unsupported || chans <= 0)
        {
            fail ("unsupported mix format (" + String (wf->wBitsPerSample) + " bit)");
            wait (2000);
            continue;
        }

        // 100 ms buffer: we poll every few ms, so this only matters if the thread stalls
        h = client->Initialize (AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 1000000, 0, wf, nullptr);
        if (FAILED (h)) { fail ("initialize " + hr (h)); wait (1000); continue; }

        h = client->GetService (__uuidof (IAudioCaptureClient), (void**) capture.put());
        if (FAILED (h)) { fail ("capture service " + hr (h)); wait (1000); continue; }

        h = client->Start();
        if (FAILED (h)) { fail ("start " + hr (h)); wait (1000); continue; }

        setError ({});
        {
            const ScopedLock sl (infoLock);
            description = devName + " " + String ((int) sr) + " Hz " + String (chans) + " ch";
        }
        Logger::writeToLog ("loopback: capturing " + devName + " " + String ((int) sr) + " Hz " + String (chans) + " ch");

        AudioBuffer<float> buf (chans, (int) sr);   // up to 1 s per packet
        const int64 ticksPerSec = Time::getHighResolutionTicksPerSecond();
        int64 lastPacketTicks = Time::getHighResolutionTicks();
        int64 baseTicks = lastPacketTicks;
        int64 baseFrames = 0, delivered = 0;
        int64 lastDefaultCheck = lastPacketTicks;
        bool broken = false;

        auto deliver = [&] (int frames)
        {
            if (callback) callback (buf.getArrayOfReadPointers(), chans, frames, sr);
            delivered += frames;
        };

        while (! threadShouldExit() && ! broken)
        {
            wait (5);

            UINT32 packet = 0;
            bool gotData = false;
            while (! broken)
            {
                h = capture->GetNextPacketSize (&packet);
                if (FAILED (h)) { broken = true; break; }
                if (packet == 0) break;

                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                h = capture->GetBuffer (&data, &frames, &flags, nullptr, nullptr);
                if (FAILED (h)) { broken = true; break; }

                const int n = jmin ((int) frames, buf.getNumSamples());
                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr)
                    buf.clear (0, n);
                else
                    deinterleave (type, data, n, chans, buf);
                capture->ReleaseBuffer (frames);

                deliver (n);
                gotData = true;
            }
            if (broken) { setError ("capture stopped (" + hr (h) + "), restarting"); break; }

            const auto now = Time::getHighResolutionTicks();
            if (gotData)
            {
                // real packets define the timeline; re-anchor so clock drift never accumulates
                lastPacketTicks = baseTicks = now;
                baseFrames = delivered;
            }
            else if ((now - lastPacketTicks) * 1000 / ticksPerSec > 30)
            {
                // nothing is playing: WASAPI sends nothing, so supply silence in real time,
                // staying 10 ms behind the wall clock
                const auto target = baseFrames + (int64) ((double) (now - baseTicks) * sr / (double) ticksPerSec) - (int64) (sr * 0.01);
                auto missing = target - delivered;
                while (missing > 0)
                {
                    const int n = (int) jmin<int64> (missing, buf.getNumSamples());
                    buf.clear (0, n);
                    deliver (n);
                    missing -= n;
                }
            }

            // following the default output: restart when the user switches it
            if (requestedDevice.isEmpty() && (now - lastDefaultCheck) > ticksPerSec * 2)
            {
                lastDefaultCheck = now;
                ComPtr<IMMDevice> def;
                if (SUCCEEDED (en->GetDefaultAudioEndpoint (eRender, eConsole, def.put())) && deviceId (def.p) != devId)
                {
                    Logger::writeToLog ("loopback: default output changed");
                    break;
                }
            }
        }

        client->Stop();
        CoTaskMemFree (wf);
        wf = nullptr;
        if (broken) wait (500);
    }
}

#elif JUCE_LINUX || JUCE_BSD
//==============================================================================
// PulseAudio / PipeWire (pipewire-pulse): read the monitor source of an output.
// libpulse-simple is loaded at run time so the build does not depend on it.
#include <dlfcn.h>

namespace
{
    struct pa_sample_spec_ { int format; uint32_t rate; uint8_t channels; };
    struct pa_buffer_attr_ { uint32_t maxlength, tlength, prebuf, minreq, fragsize; };
    constexpr int PA_STREAM_RECORD_ = 2;
    constexpr int PA_SAMPLE_FLOAT32LE_ = 5;

    using pa_simple_new_t   = void* (*) (const char*, const char*, int, const char*, const char*,
                                         const pa_sample_spec_*, const void*, const pa_buffer_attr_*, int*);
    using pa_simple_read_t  = int (*) (void*, void*, size_t, int*);
    using pa_simple_free_t  = void (*) (void*);

    struct PulseSimple
    {
        PulseSimple()
        {
            lib = dlopen ("libpulse-simple.so.0", RTLD_NOW | RTLD_LOCAL);
            if (lib == nullptr) return;
            create = (pa_simple_new_t)  dlsym (lib, "pa_simple_new");
            read   = (pa_simple_read_t) dlsym (lib, "pa_simple_read");
            free   = (pa_simple_free_t) dlsym (lib, "pa_simple_free");
        }
        bool ok() const { return create != nullptr && read != nullptr && free != nullptr; }

        void* lib = nullptr;
        pa_simple_new_t create = nullptr;
        pa_simple_read_t read = nullptr;
        pa_simple_free_t free = nullptr;
    };

    PulseSimple& pulse()
    {
        static PulseSimple p;
        return p;
    }

    //--------------------------------------------------------------------------
    // Listing the outputs (sinks) needs the asynchronous libpulse API; it is driven
    // synchronously here with a private main loop. Struct layout from <pulse/introspect.h>
    // (stable ABI; only the leading fields are used).
    struct pa_channel_map_ { uint8_t channels; int map[32]; };
    struct pa_cvolume_     { uint8_t channels; uint32_t values[32]; };
    struct pa_sink_info_
    {
        const char* name;
        uint32_t index;
        const char* description;
        pa_sample_spec_ sample_spec;
        pa_channel_map_ channel_map;
        uint32_t owner_module;
        pa_cvolume_ volume;
        int mute;
        uint32_t monitor_source;
        const char* monitor_source_name;
    };

    struct PulseApi
    {
        void* (*mainloop_new)() = nullptr;
        void* (*mainloop_get_api) (void*) = nullptr;
        int   (*mainloop_iterate) (void*, int, int*) = nullptr;
        void  (*mainloop_free) (void*) = nullptr;
        void* (*context_new) (void*, const char*) = nullptr;
        int   (*context_connect) (void*, const char*, int, const void*) = nullptr;
        int   (*context_get_state) (void*) = nullptr;
        void  (*context_disconnect) (void*) = nullptr;
        void  (*context_unref) (void*) = nullptr;
        void* (*context_get_sink_info_list) (void*, void (*) (void*, const pa_sink_info_*, int, void*), void*) = nullptr;
        int   (*operation_get_state) (void*) = nullptr;
        void  (*operation_unref) (void*) = nullptr;
        bool ok = false;

        PulseApi()
        {
            void* lib = dlopen ("libpulse.so.0", RTLD_NOW | RTLD_LOCAL);
            if (lib == nullptr) return;
            auto get = [lib] (auto& fn, const char* sym) { fn = reinterpret_cast<std::remove_reference_t<decltype (fn)>> (dlsym (lib, sym)); return fn != nullptr; };
            ok = get (mainloop_new, "pa_mainloop_new") && get (mainloop_get_api, "pa_mainloop_get_api")
              && get (mainloop_iterate, "pa_mainloop_iterate") && get (mainloop_free, "pa_mainloop_free")
              && get (context_new, "pa_context_new") && get (context_connect, "pa_context_connect")
              && get (context_get_state, "pa_context_get_state") && get (context_disconnect, "pa_context_disconnect")
              && get (context_unref, "pa_context_unref") && get (context_get_sink_info_list, "pa_context_get_sink_info_list")
              && get (operation_get_state, "pa_operation_get_state") && get (operation_unref, "pa_operation_unref");
        }

        static PulseApi& get() { static PulseApi a; return a; }
    };

    struct Sink { String description, monitor; };

    /** The outputs with their monitor source names; empty if the server can't be reached. */
    std::vector<Sink> listSinks()
    {
        std::vector<Sink> sinks;
        auto& pa = PulseApi::get();
        if (! pa.ok) return sinks;

        constexpr int kReady = 4, kFailed = 5, kTerminated = 6, kOpRunning = 0;
        void* ml = pa.mainloop_new();
        if (ml == nullptr) return sinks;
        void* ctx = pa.context_new (pa.mainloop_get_api (ml), "SoundFunnel");

        // poll without blocking so a stuck sound server can't hang the caller (2 s budget)
        const auto deadline = Time::getMillisecondCounter() + 2000;
        auto pump = [&] { pa.mainloop_iterate (ml, 0, nullptr); Thread::sleep (2); return Time::getMillisecondCounter() < deadline; };

        if (ctx != nullptr && pa.context_connect (ctx, nullptr, 0, nullptr) >= 0)
        {
            int st = 0;
            while ((st = pa.context_get_state (ctx)) != kReady && st != kFailed && st != kTerminated && pump()) {}

            if (st == kReady)
            {
                auto cb = [] (void*, const pa_sink_info_* i, int eol, void* user)
                {
                    if (eol != 0 || i == nullptr) return;
                    auto* out = static_cast<std::vector<Sink>*> (user);
                    String desc = String::fromUTF8 (i->description != nullptr ? i->description : i->name);
                    String mon  = String::fromUTF8 (i->monitor_source_name != nullptr ? i->monitor_source_name : "");
                    if (mon.isEmpty() && i->name != nullptr) mon = String::fromUTF8 (i->name) + ".monitor";
                    out->push_back ({ desc, mon });
                };
                if (void* op = pa.context_get_sink_info_list (ctx, cb, &sinks))
                {
                    while (pa.operation_get_state (op) == kOpRunning && pump()) {}
                    pa.operation_unref (op);
                }
            }
            pa.context_disconnect (ctx);
        }
        if (ctx != nullptr) pa.context_unref (ctx);
        pa.mainloop_free (ml);
        return sinks;
    }
}

bool LoopbackCapture::isSupported()            { return pulse().ok(); }

StringArray LoopbackCapture::getDeviceNames()
{
    StringArray names;
    for (auto& s : listSinks())
        names.addIfNotAlreadyThere (s.description);
    return names;
}

void LoopbackCapture::run()
{
    auto& pa = pulse();
    if (! pa.ok())
    {
        setError ("PulseAudio / PipeWire (libpulse-simple) is not available");
        return;
    }

    constexpr double sr = 48000.0;
    constexpr int chans = 2;
    constexpr int frames = 480;                     // 10 ms per read
    while (! threadShouldExit())
    {
        // the chosen output (by its description, as shown in the UI) -> its monitor source;
        // nothing chosen = the default output's monitor
        String source ("@DEFAULT_MONITOR@");
        if (requestedDevice.isNotEmpty())
        {
            source.clear();
            for (auto& s : listSinks())
                if (s.description == requestedDevice) { source = s.monitor; break; }
            if (source.isEmpty())
            {
                setError ("output device not found: " + requestedDevice);
                wait (2000);
                continue;
            }
        }

        pa_sample_spec_ spec { PA_SAMPLE_FLOAT32LE_, (uint32_t) sr, (uint8_t) chans };
        pa_buffer_attr_ attr { (uint32_t) -1, (uint32_t) -1, (uint32_t) -1, (uint32_t) -1,
                               (uint32_t) (frames * chans * sizeof (float)) };
        int err = 0;
        void* s = pa.create (nullptr, "SoundFunnel", PA_STREAM_RECORD_, source.toRawUTF8(),
                             "system audio", &spec, nullptr, &attr, &err);
        if (s == nullptr)
        {
            setError ("cannot open " + source + " (error " + String (err) + ")");
            wait (2000);
            continue;
        }

        setError ({});
        {
            const ScopedLock sl (infoLock);
            description = (requestedDevice.isNotEmpty() ? requestedDevice : String ("Default output")) + " (" + source + ") 48000 Hz 2 ch";
        }
        Logger::writeToLog ("loopback: capturing " + source);

        std::vector<float> interleaved ((size_t) (frames * chans));
        AudioBuffer<float> buf (chans, frames);

        while (! threadShouldExit())
        {
            // monitor sources stream continuously (silence included), so no gap filling here
            if (pa.read (s, interleaved.data(), interleaved.size() * sizeof (float), &err) < 0)
            {
                setError ("capture stopped (error " + String (err) + "), restarting");
                break;
            }
            for (int c = 0; c < chans; ++c)
            {
                auto* d = buf.getWritePointer (c);
                for (int i = 0; i < frames; ++i) d[i] = interleaved[(size_t) (i * chans + c)];
            }
            if (callback) callback (buf.getArrayOfReadPointers(), chans, frames, sr);
        }

        pa.free (s);
        wait (500);
    }
}

#else
//==============================================================================
bool LoopbackCapture::isSupported()            { return false; }
StringArray LoopbackCapture::getDeviceNames()  { return {}; }

void LoopbackCapture::run()
{
    setError ("capturing system audio is not supported on this OS yet");
}
#endif
