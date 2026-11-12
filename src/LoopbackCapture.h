// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_core/juce_core.h>
#include <functional>

/** Captures what this PC plays ("system audio") from an output device.

    Windows: WASAPI loopback on a render endpoint (shared mode).
    Linux:   the monitor source of the default output via PulseAudio / PipeWire
             (libpulse-simple, loaded at run time).
    macOS:   not supported yet (see docs/PLAN.md); use a virtual device such as BlackHole.

    Audio is delivered on an internal thread in the device's mix format,
    converted to float, non-interleaved. WASAPI delivers no packets while nothing
    is playing, so the gaps are filled with silence to keep a steady stream. */
class LoopbackCapture : private juce::Thread
{
public:
    /** data[ch][i], numChannels channels, numSamples frames at sampleRate. */
    using Callback = std::function<void (const float* const* data, int numChannels, int numSamples, double sampleRate)>;

    LoopbackCapture();
    ~LoopbackCapture() override;

    static bool isSupported();

    /** Names of the output devices that can be captured (empty name = the default output). */
    static juce::StringArray getDeviceNames();

    /** Starts capturing. deviceName empty = follow the default output device.
        Errors (device missing, format unsupported...) are retried in the background
        and reported through getLastError(). */
    void start (const juce::String& deviceName, Callback);
    void stop();
    bool isActive() const                   { return isThreadRunning(); }

    juce::String getLastError() const;
    juce::String getDescription() const;    // e.g. "Speakers (Realtek) 48000 Hz 2 ch"

private:
    void run() override;
    void setError (const juce::String&);

    juce::String requestedDevice;
    Callback callback;

    mutable juce::CriticalSection infoLock;
    juce::String lastError, description;
};
