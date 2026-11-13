# SoundFunnel

LAN 内の複数 PC の音を、1 台の「サーバ」PC に集めて再生する常駐アプリです（Windows / macOS / Linux）。

[English](README.en.md)

- 同じ **グループキー** を設定した PC 同士が、LAN 内で自動的に見つかります。
- どの PC でも「このPCをサーバにする」を押せます。**最後に押した PC** がサーバになり、
  ほかの PC は自分の音をそのサーバへ送ります。
- 送る音は、**音声入力**（マイク・ライン入力）か、**その PC で再生中の音** から選べます（macOS は 14.2 以降）。
- サーバでは、クライアントごとにミュートと音量を設定できます。
- サーバ自身の入力は、既定では再生しません（ハウリング防止）。

[SonoBus](https://www.sonobus.net) をもとに、送信方向を「クライアント → サーバ」に固定し、
セッション・録音・エフェクトなどの機能を外した派生物です。

## インストール

[Releases](https://github.com/wtnbgo/soundfunnel/releases) から、OS に合ったファイルを取得してください。

| OS | ファイル | 補足 |
|---|---|---|
| Windows 10 / 11 (x64) | `SoundFunnel-<版>-windows-x64-setup.exe` | インストーラ。サインイン時の自動起動と、ファイアウォールの許可を選べます |
| | `SoundFunnel-<版>-windows-x64-portable.zip` | 展開して `SoundFunnel.exe` を起動するだけの版 |
| macOS 11 以降 (Apple Silicon / Intel) | `SoundFunnel-<版>-macos-universal.dmg` | `SoundFunnel.app` を「アプリケーション」へドラッグ |
| Linux (x86_64) | `soundfunnel_<版>_amd64.deb` | Debian / Ubuntu 系: `sudo apt install ./soundfunnel_*.deb` |
| | `SoundFunnel-<版>-linux-x86_64.tar.gz` | 展開して `bin/SoundFunnel` を起動 |

### macOS で初めて開くとき

いまの配布版は Apple の開発者署名をしていないため、初回はダブルクリックで開けません。
Finder で `SoundFunnel.app` を右クリック（Control + クリック）して「開く」を選んでください。
（それでも開けない場合は「システム設定 > プライバシーとセキュリティ」の下にある「このまま開く」）

起動後に次の許可を求められるので、**許可** してください。

- **マイク**: 音声入力を送るため。入力デバイスを使わない場合でも、入力を持つデバイス
  （BlackHole など）を開くときに求められます。
- **ローカルネットワーク**: ほかの PC を見つけて音を送るため。許可しないと、ほかの PC が見えません。
  あとから変えるときは「システム設定 > プライバシーとセキュリティ > ローカルネットワーク」。
- **システムオーディオ録音**: 「システムの音」を送るとき（macOS 14.2 以降）。

## 使い方

1. 起動するとトレイ（macOS はメニューバー）に常駐します。最初はグループキーの入力画面が開きます。
   Linux では、トレイ（AppIndicator / StatusNotifierItem）が使える環境ならトレイに常駐します。
   GNOME では「AppIndicator」拡張（Ubuntu では `ubuntu-appindicators`）が有効なときに使えます。
   使えない環境では通常のウィンドウとして動きます（閉じると最小化）。
2. 集めたい PC すべてに、同じ **グループキー** を設定します（「グループ」タブ）。
   キーは合言葉です。同じキーの PC だけが互いに見え、キーを知らない PC は参加できません。
3. 音を鳴らしたい PC で「**このPCをサーバにする**」を押します（「状態」タブ）。
   あとから別の PC で押せば、サーバはその PC に移ります。
4. 各クライアントで、送る音を選びます。
   - 「送信」タブの「送るもの」: **音声入力** か **システムの音**（このPCで再生中の音）。
   - 音声入力の場合は、「オーディオ」タブで入力デバイスを選びます。
   - システムの音の場合、Windows では「取り込む出力先」で出力デバイスを選べます（既定の出力デバイスに追従）。
     macOS（14.2 以降）では、すべてのアプリの再生音をまとめて取り込みます。
     Linux でも出力デバイスを選べます（PulseAudio / PipeWire の出力の「モニタ」を取り込みます）。
5. サーバの「状態」タブで、クライアントごとに受信状態・レベルを確認し、ミュートや音量を調整できます。
   ミュートすると、そのクライアントは送信そのものを止めます。

サーバの出力先は「オーディオ」タブの出力デバイスです。

### Linux での使い方

Linux 版の画面と操作は Windows / macOS と同じです。違うのは、常駐のしかたと起動のしかたです。

- **起動**: .deb を入れた場合はアプリ一覧の「SoundFunnel」から、または端末で `SoundFunnel`。
  tar.gz の場合は展開したフォルダの `bin/SoundFunnel` を実行します。
- **常駐のしかた**:
  - トレイ（AppIndicator / StatusNotifierItem）が使えるデスクトップ（KDE Plasma、Xfce、
    AppIndicator 拡張を有効にした GNOME など）では、トップバー / パネルにアイコンが出ます。
    アイコンのメニューから「このPCをサーバにする」「送信を一時停止」「設定を開く」「終了」を選べます。
    設定画面の閉じるボタンでは終了せず、トレイに残ります。
  - トレイが使えない環境（AppIndicator 拡張が無効な GNOME など）では、通常のウィンドウとして動きます。
    閉じるボタンを押すと最小化され（タスクバー / ドックから戻せます）、終了は「状態」タブの「終了」ボタンです。
  - Ubuntu の GNOME でトレイに出したいときは、「拡張機能」アプリで「Ubuntu AppIndicators」を有効にしてから
    SoundFunnel を起動し直してください。
- **自動起動**: 「グループ」タブの「ログイン時に自動起動」で、`~/.config/autostart/soundfunnel.desktop` を作ります。
  自動起動では `--background` 付きで起動します（トレイがない環境では最小化した状態）。
- **送る音**:
  - 音声入力: 「オーディオ」タブで ALSA / JACK のデバイスを選びます。PipeWire / PulseAudio の環境では
    `pulse` や `default` を選ぶと、デスクトップの音声設定で選んだマイクが使われます。
  - システムの音: 「送信」タブで「システムの音」を選び、「取り込む出力先」で出力デバイスを選びます
    （既定の出力デバイスのままなら、その時点の既定の出力を取り込みます）。
    PulseAudio か PipeWire（pipewire-pulse）が動いている必要があります。
- **ファイアウォール**: ufw などを使っている場合は、UDP 47700 と 47701 を LAN から許可してください。
  例: `sudo ufw allow from 192.168.0.0/16 to any port 47700:47701 proto udp`
- **ログ**: 端末から `--log-file ~/soundfunnel.log` を付けて起動すると、検出やデバイスの状況を記録できます。

### システムの音について

macOS 14.2 より前では「システムの音」を選べません。[BlackHole](https://github.com/ExistentialAudio/BlackHole)
などの仮想オーディオデバイスを出力にして、SoundFunnel の音声入力にも同じものを選んでください。

Linux の「システムの音」は PulseAudio か PipeWire（pipewire-pulse）が動いている環境で使えます
（`libpulse-simple` を使います。ほとんどのデスクトップ環境に入っています）。
手元のスピーカーでは鳴らさずに送りたいときは、どこにも鳴らない仮想の出力デバイスを作って既定の出力にします。
手順は [docs/LINUX_VIRTUAL_SINK.md](docs/LINUX_VIRTUAL_SINK.md) を見てください。

### 送信形式

「送信」タブで、コーデック（Opus 16〜256 kbps / PCM 16・24・32 bit）、モノラル / ステレオ、
サーバ側のジッタバッファ（ms）を選べます。LAN なら既定の Opus 128 kbps で十分です。
遅延を詰めたいときはジッタバッファを小さく、音が途切れるときは大きくします。

### ネットワーク

| 用途 | ポート |
|---|---|
| 仲間の検出（マルチキャスト 239.255.77.77 とブロードキャスト） | UDP 47700 |
| 音声 | UDP 47701（使用中なら空きポート） |

- ファイアウォールでこれらの UDP 通信を許可してください。Windows のインストーラは許可ルールを追加できます
  （プライベート / ドメイン ネットワークのみ）。
- macOS のアプリケーションファイアウォールが有効な場合は、
  「システム設定 > ネットワーク > ファイアウォール > オプション」で SoundFunnel の受信を許可してください。
- 検出パケットはグループキーで認証（HMAC-SHA256）していますが、音声そのものは暗号化していません。
  信頼できる LAN で使ってください。

### コマンドライン

| 引数 | 意味 |
|---|---|
| `--background` | 設定画面を開かずにトレイで起動する（自動起動で使用） |
| `--loopback` | この起動に限り、システムの音を送る。`--loopback-device NAME` で出力デバイスを指定 |
| `--profile NAME` | 別の設定で起動する（1 台で複数起動して試すとき） |
| `--silent` | 出力デバイスを開かない（テスト用） |
| `--test-tone` | 入力の代わりに 440 Hz のテスト音を送る（テスト用） |
| `--status-file F` / `--control-file F` | 状態の JSON 出力 / コマンドファイルでの操作（テスト用、[docs/TESTING.md](docs/TESTING.md)） |
| `--log-file F` | ログをファイルに書く |
| `--list-loopback-devices` | 「システムの音」で取り込める出力デバイスの名前を表示して終了する |
| `--selftest` | プロトコルの自己テストを実行して終了する |

### 設定ファイルの場所

| OS | 場所 |
|---|---|
| Windows | `%APPDATA%\SoundFunnel\SoundFunnel.settings` |
| macOS | `~/Library/Application Support/SoundFunnel/SoundFunnel.settings` |
| Linux | `~/.config/SoundFunnel/SoundFunnel.settings` |

`--profile NAME` を付けると `SoundFunnel-NAME.settings` になります。

## ソースからのビルド

```bash
git clone --recursive https://github.com/wtnbgo/soundfunnel.git
```

```bash
cmake -S soundfunnel -B soundfunnel/build
```

```bash
cmake --build soundfunnel/build --config Release --target SoundFunnel
```

OS ごとの準備、インストーラの作り方、CI とリリースの手順は [docs/BUILDING.md](docs/BUILDING.md) を見てください。
設計と今後の予定は [docs/PLAN.md](docs/PLAN.md)、変更履歴は [CHANGELOG.md](CHANGELOG.md) にあります。

## ライセンス

GPLv3（[LICENSE](LICENSE)）。同梱物:

- SonoBus 由来のコード: GPLv3、Copyright (C) Jesse Chappell
- AOO (Audio over OSC): BSD（[third_party/aoo/LICENSE](third_party/aoo/LICENSE)）
- JUCE 7: GPLv3 を選択
- Opus: BSD
