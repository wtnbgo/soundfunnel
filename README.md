# SoundFunnel

LAN 内の複数 PC の音を、1 台の「サーバ」PC に集めて再生する常駐アプリです（Windows / macOS / Linux）。

- 同じ **グループキー** を設定した PC 同士が、LAN 内で自動的に見つかります。
- どの PC でも「このPCをサーバにする」を押せます。**最後に押した PC** がサーバになり、
  ほかの PC は自分の入力デバイスの音をそのサーバへ送ります。
- サーバでは、クライアントごとにミュートと音量を設定できます。
- サーバ自身の入力は、既定では再生しません（ハウリング防止）。

[SonoBus](https://www.sonobus.net) をもとに、送信方向を「クライアント → サーバ」に固定し、
セッション・録音・エフェクトなどの機能を外した派生物です。

## ビルド

必要なもの: CMake 3.22 以降、C++17 コンパイラ（MSVC 2022 以降 / Xcode / GCC・Clang）、git。
Linux では ALSA・JACK・X11・freetype などの開発パッケージも必要です（JUCE の Linux 依存パッケージ）。

```bash
git clone --recursive https://github.com/wtnbgo/soundfunnel.git
```

```bash
cmake -S . -B build
```

```bash
cmake --build build --config Release --target SoundFunnel
```

Opus はビルド時に自動で取得します（FetchContent）。

## 使い方

1. 起動するとトレイ（メニューバー）に常駐します。最初はグループキーの入力画面が開きます。
2. 集めたい PC すべてに、同じグループキーを設定します。
3. 音を鳴らしたい PC で「このPCをサーバにする」を押します。
4. 各クライアントの「オーディオ」タブで、送りたい入力デバイスを選びます。

PC で再生中の音そのものを送る場合は、当面は仮想オーディオデバイス
（Windows: VB-CABLE など、macOS: BlackHole など、Linux: PulseAudio / PipeWire の monitor ソース）を
入力に選んでください。Windows の WASAPI ループバックへの直接対応は予定しています（[docs/PLAN.md](docs/PLAN.md)）。

### ネットワーク

| 用途 | ポート |
|---|---|
| 仲間の検出（マルチキャスト 239.255.77.77 とブロードキャスト） | UDP 47700 |
| 音声 | UDP 47701（使用中なら空きポート） |

ファイアウォールでこれらの UDP 通信を許可してください。
macOS では、アプリケーションファイアウォールが有効だと、SoundFunnel への受信が止められます
（「システム設定 > ネットワーク > ファイアウォール > オプション」で SoundFunnel.app の受信を許可してください）。
Linux では、いまはトレイアイコンを使わず、通常のウィンドウとして動きます（閉じると最小化）。

### コマンドライン

| 引数 | 意味 |
|---|---|
| `--background` | 設定画面を開かずにトレイで起動する（自動起動で使用） |
| `--profile NAME` | 別の設定で起動する（1 台で複数起動して試すとき） |
| `--silent` | 出力デバイスを開かない（テスト用） |
| `--test-tone` | 入力の代わりに 440 Hz のテスト音を送る（テスト用） |
| `--status-file F` / `--control-file F` | 状態の JSON 出力 / コマンドファイルでの操作（テスト用） |
| `--selftest` | プロトコルの自己テストを実行して終了する |

## ライセンス

GPLv3（[LICENSE](LICENSE)）。同梱物:

- SonoBus 由来のコード: GPLv3、Copyright (C) Jesse Chappell
- AOO (Audio over OSC): BSD（[third_party/aoo/LICENSE](third_party/aoo/LICENSE)）
- JUCE 7: GPLv3 を選択
- Opus: BSD
