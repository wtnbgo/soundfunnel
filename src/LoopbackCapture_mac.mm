// SPDX-License-Identifier: GPL-3.0-or-later
// macOS implementation of LoopbackCapture: a Core Audio process tap (macOS 14.2+)
// on all processes, read through a private aggregate device.
#include "LoopbackCapture.h"
#include <juce_audio_basics/juce_audio_basics.h>

#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#include <AvailabilityMacros.h>

#if defined (MAC_OS_VERSION_14_2)
 #import <CoreAudio/AudioHardwareTapping.h>
 #import <CoreAudio/CATapDescription.h>
 #define SF_HAVE_PROCESS_TAP 1
#else
 #define SF_HAVE_PROCESS_TAP 0
#endif

using namespace juce;

// works with and without ARC
#if __has_feature(objc_arc)
 #define SF_AUTORELEASE(x) (x)
#else
 #define SF_AUTORELEASE(x) [(x) autorelease]
#endif

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

StringArray LoopbackCapture::getDeviceNames()  { return {}; }   // the tap mixes every process

#if SF_HAVE_PROCESS_TAP
namespace
{
    String osStatus (OSStatus s) { return String ((int) s); }

    String defaultOutputUID()
    {
        AudioObjectPropertyAddress a { kAudioHardwarePropertyDefaultSystemOutputDevice,
                                       kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        AudioObjectID dev = kAudioObjectUnknown;
        UInt32 size = sizeof (dev);
        if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &a, 0, nullptr, &size, &dev) != noErr)
            return {};

        a.mSelector = kAudioDevicePropertyDeviceUID;
        CFStringRef uid = nullptr;
        size = sizeof (uid);
        if (AudioObjectGetPropertyData (dev, &a, 0, nullptr, &size, &uid) != noErr || uid == nullptr)
            return {};
        String s = String::fromCFString (uid);
        CFRelease (uid);
        return s;
    }
}

bool LoopbackCapture::isSupported()
{
    if (@available (macOS 14.2, *)) return true;
    return false;
}

void LoopbackCapture::run()
{
    if (@available (macOS 14.2, *))
    {
        while (! threadShouldExit())
        {
            AudioObjectID tapID = kAudioObjectUnknown, aggID = kAudioObjectUnknown;
            AudioDeviceIOProcID procID = nullptr;
            const String outUID = defaultOutputUID();

            auto cleanup = [&]
            {
                if (procID != nullptr)          { AudioDeviceStop (aggID, procID); AudioDeviceDestroyIOProcID (aggID, procID); }
                if (aggID != kAudioObjectUnknown) AudioHardwareDestroyAggregateDevice (aggID);
                if (tapID != kAudioObjectUnknown) AudioHardwareDestroyProcessTap (tapID);
                procID = nullptr; aggID = tapID = kAudioObjectUnknown;
            };

            @autoreleasepool
            {
                // a stereo mix of every process, left audible, visible only to us
                CATapDescription* desc = SF_AUTORELEASE ([[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses: @[]]);
                desc.name = @"SoundFunnel";
                desc.privateTap = YES;
                desc.muteBehavior = CATapUnmuted;

                OSStatus st = AudioHardwareCreateProcessTap (desc, &tapID);
                if (st != noErr)
                {
                    setError ("cannot create the system audio tap (" + osStatus (st) + "). "
                              "Allow \"System Audio Recording\" for SoundFunnel in Privacy & Security.");
                    cleanup();
                    wait (3000);
                    continue;
                }

                // tap UID and format
                AudioObjectPropertyAddress a { kAudioTapPropertyUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
                CFStringRef tapUID = nullptr;
                UInt32 size = sizeof (tapUID);
                AudioObjectGetPropertyData (tapID, &a, 0, nullptr, &size, &tapUID);

                AudioStreamBasicDescription fmt {};
                a.mSelector = kAudioTapPropertyFormat;
                size = sizeof (fmt);
                AudioObjectGetPropertyData (tapID, &a, 0, nullptr, &size, &fmt);

                if (tapUID == nullptr || fmt.mSampleRate <= 0 || fmt.mChannelsPerFrame == 0
                    || (fmt.mFormatFlags & kAudioFormatFlagIsFloat) == 0 || fmt.mBitsPerChannel != 32)
                {
                    if (tapUID) CFRelease (tapUID);
                    setError ("unexpected system audio tap format");
                    cleanup();
                    wait (3000);
                    continue;
                }

                // private aggregate device that reads the tap, clocked by the default output
                NSMutableDictionary* agg = [NSMutableDictionary dictionaryWithDictionary: @{
                    @kAudioAggregateDeviceNameKey:       @"SoundFunnel system audio",
                    @kAudioAggregateDeviceUIDKey:        [[NSUUID UUID] UUIDString],
                    @kAudioAggregateDeviceIsPrivateKey:  @YES,
                    @kAudioAggregateDeviceIsStackedKey:  @NO,
                    @kAudioAggregateDeviceTapAutoStartKey: @YES,
                    @kAudioAggregateDeviceTapListKey:    @[ @{ @kAudioSubTapUIDKey: (__bridge NSString*) tapUID,
                                                               @kAudioSubTapDriftCompensationKey: @YES } ],
                }];
                if (outUID.isNotEmpty())
                {
                    NSString* o = [NSString stringWithUTF8String: outUID.toRawUTF8()];
                    agg[@kAudioAggregateDeviceMainSubDeviceKey] = o;
                    agg[@kAudioAggregateDeviceSubDeviceListKey] = @[ @{ @kAudioSubDeviceUIDKey: o } ];
                }
                CFRelease (tapUID);

                st = AudioHardwareCreateAggregateDevice ((__bridge CFDictionaryRef) agg, &aggID);
                if (st != noErr)
                {
                    setError ("cannot create the aggregate device (" + osStatus (st) + ")");
                    cleanup();
                    wait (3000);
                    continue;
                }

                const double sr = fmt.mSampleRate;
                const int chans = (int) fmt.mChannelsPerFrame;
                const bool interleaved = (fmt.mFormatFlags & kAudioFormatFlagIsNonInterleaved) == 0;
                auto buf = std::make_shared<juce::AudioBuffer<float>> (jmax (2, chans), 8192);
                auto cb = callback;

                st = AudioDeviceCreateIOProcIDWithBlock (&procID, aggID, nullptr,
                    ^(const AudioTimeStamp*, const AudioBufferList* in, const AudioTimeStamp*, AudioBufferList*, const AudioTimeStamp*)
                    {
                        if (in == nullptr || in->mNumberBuffers == 0 || ! cb) return;
                        if (interleaved)
                        {
                            const auto& b = in->mBuffers[0];
                            const int nch = (int) jmax ((UInt32) 1, b.mNumberChannels);
                            const int frames = jmin ((int) (b.mDataByteSize / (sizeof (float) * (UInt32) nch)), buf->getNumSamples());
                            auto* src = static_cast<const float*> (b.mData);
                            for (int c = 0; c < jmin (nch, buf->getNumChannels()); ++c)
                            {
                                auto* d = buf->getWritePointer (c);
                                for (int i = 0; i < frames; ++i) d[i] = src[i * nch + c];
                            }
                            cb (buf->getArrayOfReadPointers(), jmin (nch, buf->getNumChannels()), frames, sr);
                        }
                        else
                        {
                            const int nch = jmin ((int) in->mNumberBuffers, buf->getNumChannels());
                            const int frames = jmin ((int) (in->mBuffers[0].mDataByteSize / sizeof (float)), buf->getNumSamples());
                            for (int c = 0; c < nch; ++c)
                                buf->copyFrom (c, 0, static_cast<const float*> (in->mBuffers[c].mData), frames);
                            cb (buf->getArrayOfReadPointers(), nch, frames, sr);
                        }
                    });
                if (st == noErr) st = AudioDeviceStart (aggID, procID);
                if (st != noErr)
                {
                    setError ("cannot start capturing system audio (" + osStatus (st) + ")");
                    cleanup();
                    wait (3000);
                    continue;
                }

                setError ({});
                {
                    const ScopedLock sl (infoLock);
                    description = "System audio (process tap) " + String ((int) sr) + " Hz " + String (chans) + " ch";
                }
                Logger::writeToLog ("loopback: capturing system audio via process tap " + String ((int) sr) + " Hz " + String (chans) + " ch");
            }

            // follow the default output: rebuild the aggregate when it changes
            while (! threadShouldExit())
            {
                wait (1000);
                if (defaultOutputUID() != outUID)
                {
                    Logger::writeToLog ("loopback: default output changed");
                    break;
                }
            }
            cleanup();
        }
    }
    else
    {
        setError ("capturing system audio needs macOS 14.2 or later");
    }
}

#else
bool LoopbackCapture::isSupported() { return false; }
void LoopbackCapture::run()         { setError ("this build has no system audio capture (macOS SDK older than 14.2)"); }
#endif
