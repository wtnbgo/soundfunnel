// SPDX-License-Identifier: GPL-3.0-or-later
#include "Localisation.h"
#include <juce_gui_basics/juce_gui_basics.h>

using namespace juce;

namespace
{
    // JUCE LocalisedStrings format: "english" = "translated"
    const char* const kJapanese = R"TXT(
language: Japanese
countries: jp

"Settings" = "設定"
"Status" = "状態"
"Audio" = "オーディオ"
"Send format" = "送信形式"
"Sending" = "送信"
"Waiting for audio permission..." = "オーディオの許可を待っています..."
"Send" = "送るもの"
"Audio input (microphone / line in)" = "音声入力 (マイク / ライン入力)"
"System audio (what this PC plays)" = "システムの音 (このPCで再生中の音)"
"Capture from" = "取り込む出力先"
"Default output device" = "既定の出力デバイス"
"Sending system audio is not available here yet. Use a virtual audio device as the input instead." = "この環境ではシステムの音を直接送れません。仮想オーディオデバイスを入力に選んでください。"
"System audio: " = "システムの音: "
"Group" = "グループ"
"Quit" = "終了"
"Open settings..." = "設定を開く..."
"Make this PC the server" = "このPCをサーバにする"
"Pause sending" = "送信を一時停止"
"This PC is the server" = "このPCがサーバです"
"Server: " = "サーバ: "
"No server yet" = "サーバ未設定"
"Server is offline" = "サーバがオフラインです"
"Set a group key to start" = "グループキーを設定してください"
"Discovery port is in use" = "検出用ポートが使用中です"
"Audio port could not be opened" = "音声用ポートを開けませんでした"
"Connected" = "接続中"
"Connecting..." = "接続待ち..."
"Muted by the server" = "サーバでミュートされています"
"Paused" = "一時停止中"
"Members" = "メンバー"
"Name" = "名前"
"Mute" = "ミュート"
"Volume" = "音量"
"server" = "サーバ"
"receiving" = "受信中"
"silent" = "無音"
"online" = "オンライン"
"(this PC)" = "(このPC)"
"PC name" = "PC名"
"Group key" = "グループキー"
"Show" = "表示"
"PCs with the same group key find each other automatically on the LAN." = "同じグループキーを設定したPC同士が、LAN内で自動的にグループになります。"
"Start automatically at login" = "ログイン時に自動起動"
"Codec" = "コーデック"
"Channels" = "チャンネル数"
"Mono" = "モノラル"
"Stereo" = "ステレオ"
"Jitter buffer (ms)" = "ジッタバッファ (ms)"
"When this PC is the server, also play its own input" = "サーバのとき、このPC自身の入力も再生する"
"Input = what this PC sends. Output = where the server plays." = "入力 = このPCが送る音、出力 = サーバのときに再生する先"
"Apply" = "適用"
"Send level" = "送信レベル"
"Audio port" = "音声ポート"
"Client" = "クライアント"
"Role" = "役割"
)TXT";

    String pickCjkFont()
    {
        StringArray candidates;
       #if JUCE_WINDOWS
        candidates = { "Yu Gothic UI", "Meiryo UI", "Meiryo", "MS UI Gothic" };
       #elif JUCE_MAC
        candidates = { "Hiragino Sans", "Hiragino Kaku Gothic ProN" };
       #else
        candidates = { "Noto Sans CJK JP", "Noto Sans JP", "IPAexGothic", "TakaoPGothic", "VL PGothic" };
       #endif
        auto installed = Font::findAllTypefaceNames();
        for (auto& c : candidates)
            if (installed.contains (c, true))
                return c;
        return {};
    }

   #if JUCE_LINUX || JUCE_BSD
    /** JUCE 7 only scans .ttf/.otf/.pfb/.pcf files on Linux, but distributions ship the
        Japanese fonts (Noto Sans CJK) as .ttc collections, so they are never found and
        Japanese text shows as boxes. Find a CJK font file ourselves (fontconfig first,
        then the usual locations) and load it from memory. Face 0 of the Noto CJK
        collections is the Japanese one. */
    Typeface::Ptr loadLinuxCjkTypeface()
    {
        StringArray files;

        ChildProcess fc;
        if (fc.start (StringArray { "fc-match", "-f", "%{file}", "sans-serif:lang=ja" }, ChildProcess::wantStdOut))
        {
            auto f = fc.readAllProcessOutput().trim();
            fc.waitForProcessToFinish (2000);
            if (f.isNotEmpty()) files.add (f);
        }

        for (auto* p : { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",          // Debian / Ubuntu
                         "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",               // Arch / Fedora
                         "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",        // Fedora
                         "/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.otf",
                         "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf",              // Debian alternative
                         "/usr/share/fonts/opentype/ipaexfont-gothic/ipaexg.ttf",
                         "/usr/share/fonts/truetype/takao-gothic/TakaoPGothic.ttf",
                         "/usr/share/fonts/truetype/vlgothic/VL-PGothic-Regular.ttf" })
            files.addIfNotAlreadyThere (p);

        for (auto& path : files)
        {
            File f (path);
            if (! f.existsAsFile() || f.getSize() > 64 * 1024 * 1024) continue;

            MemoryBlock data;
            if (! f.loadFileAsData (data)) continue;

            if (auto tf = Typeface::createSystemTypefaceFor (data.getData(), data.getSize()))
            {
                // must actually have Japanese glyphs (U+3042 HIRAGANA LETTER A)
                Array<int> glyphs;
                Array<float> offsets;
                tf->getGlyphPositions (String::charToString ((juce_wchar) 0x3042), glyphs, offsets);
                if (glyphs.size() == 1 && glyphs[0] > 0)
                {
                    Logger::writeToLog ("font: " + tf->getName() + " (" + path + ")");
                    return tf;
                }
            }
        }
        Logger::writeToLog ("font: no Japanese font found (install fonts-noto-cjk)");
        return nullptr;
    }
   #endif
}

void Localisation::install()
{
    auto lang = SystemStats::getUserLanguage().toLowerCase();   // e.g. "ja"
    if (lang.startsWith ("ja"))
    {
        LocalisedStrings::setCurrentMappings (new LocalisedStrings (String::fromUTF8 (kJapanese), false));
        if (auto f = pickCjkFont(); f.isNotEmpty())
        {
            LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypefaceName (f);
            return;
        }
    }

   #if JUCE_LINUX || JUCE_BSD
    // JUCE draws all text itself on Linux and has no font fallback, so Japanese text
    // (UI translation, or PC names typed in Japanese) needs a CJK font in any language
    if (auto tf = loadLinuxCjkTypeface())
        LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypeface (tf);
   #endif
}
