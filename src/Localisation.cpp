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
}

void Localisation::install()
{
    auto lang = SystemStats::getUserLanguage().toLowerCase();   // e.g. "ja"
    if (lang.startsWith ("ja"))
    {
        LocalisedStrings::setCurrentMappings (new LocalisedStrings (String::fromUTF8 (kJapanese), false));
        if (auto f = pickCjkFont(); f.isNotEmpty())
            LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypefaceName (f);
    }
}
