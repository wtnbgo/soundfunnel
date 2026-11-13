# SoundFunnel

A tray app that gathers the audio of several PCs on a LAN and plays it on one "server" PC
(Windows / macOS / Linux).

[日本語](README.md)

- PCs that share the same **group key** find each other on the LAN automatically.
- Any PC can press "Make this PC the server". The **last PC that pressed it** becomes the server,
  and every other PC streams its audio there.
- Each client sends either its **audio input** (microphone / line in) or **what it is playing**
  (system audio; macOS 14.2 or later).
- The server can mute each client and set its volume.
- The server does not play its own input by default (avoids feedback).

Derived from [SonoBus](https://www.sonobus.net): the stream direction is fixed to
client → server, and sessions, recording and effects are removed.

## Install

Download the file for your OS from [Releases](https://github.com/wtnbgo/soundfunnel/releases).

| OS | File | Notes |
|---|---|---|
| Windows 10 / 11 (x64) | `SoundFunnel-<ver>-windows-x64-setup.exe` | Installer; can enable start at sign-in and a firewall rule |
| | `SoundFunnel-<ver>-windows-x64-portable.zip` | Unzip and run `SoundFunnel.exe` |
| macOS 11+ (Apple Silicon / Intel) | `SoundFunnel-<ver>-macos-universal.dmg` | Drag `SoundFunnel.app` to Applications |
| Linux (x86_64) | `soundfunnel_<ver>_amd64.deb` | Debian / Ubuntu: `sudo apt install ./soundfunnel_*.deb` |
| | `SoundFunnel-<ver>-linux-x86_64.tar.gz` | Extract and run `bin/SoundFunnel` |

### First launch on macOS

Release builds are not signed with an Apple Developer ID yet. The first time, right-click
(Control-click) `SoundFunnel.app` in Finder and choose **Open**.

Allow the permission prompts:

- **Microphone**: needed to send audio input. It is also asked when a device that has inputs
  (BlackHole etc.) is opened.
- **Local Network**: needed to find the other PCs. Without it no other PC is visible.
  (System Settings > Privacy & Security > Local Network)
- **System Audio Recording**: needed to send System audio (macOS 14.2+).

## Usage

1. SoundFunnel lives in the tray (menu bar on macOS). On first start the group key page opens.
   On Linux it uses the tray when an AppIndicator / StatusNotifierItem host is running
   (on GNOME: the AppIndicator extension); otherwise it runs as a normal window
   (closing it minimises it).
2. Set the same **group key** on every PC (Group tab). Only PCs with the same key see each other.
3. On the PC that should play the audio, press **Make this PC the server** (Status tab).
   Pressing it on another PC later moves the server there.
4. On each client choose what to send (Sending tab): **Audio input**, or **System audio**
   (Windows: pick the output device to capture, default follows the system default;
   macOS 14.2+: every app's output mixed together;
   Linux: pick an output; its PulseAudio / PipeWire monitor is captured).
   For audio input, choose the input device on the Audio tab.
5. On the server's Status tab, each client shows whether audio arrives and its level, with mute
   and volume controls. A muted client stops sending.

Before macOS 14.2, system audio can be sent through a virtual device
([BlackHole](https://github.com/ExistentialAudio/BlackHole)) selected as the input.

### Network

| Purpose | Port |
|---|---|
| Discovery (multicast 239.255.77.77 and broadcast) | UDP 47700 |
| Audio | UDP 47701 (or any free port) |

Discovery packets are authenticated with the group key (HMAC-SHA256); the audio itself is not
encrypted. Use it on a trusted LAN.

### Command line

| Option | Meaning |
|---|---|
| `--background` | Start in the tray without opening the window (used for autostart) |
| `--loopback` | Send system audio for this run; `--loopback-device NAME` picks the output |
| `--profile NAME` | Use separate settings (several instances on one PC for testing) |
| `--silent` | Do not open an output device (testing) |
| `--test-tone` | Send a 440 Hz tone instead of the input (testing) |
| `--status-file F` / `--control-file F` | JSON status output / command file control ([docs/TESTING.md](docs/TESTING.md)) |
| `--log-file F` | Write a log file |
| `--list-loopback-devices` | Print the output devices that system audio can be captured from, then exit |
| `--selftest` | Run the protocol self test and exit |

## Building from source

```bash
git clone --recursive https://github.com/wtnbgo/soundfunnel.git
```

```bash
cmake -S soundfunnel -B soundfunnel/build
```

```bash
cmake --build soundfunnel/build --config Release --target SoundFunnel
```

See [docs/BUILDING.md](docs/BUILDING.md) for per-OS prerequisites, installers, CI and releases.

## License

GPLv3 ([LICENSE](LICENSE)). Includes:

- Code derived from SonoBus: GPLv3, Copyright (C) Jesse Chappell
- AOO (Audio over OSC): BSD ([third_party/aoo/LICENSE](third_party/aoo/LICENSE))
- JUCE 7: used under GPLv3
- Opus: BSD
