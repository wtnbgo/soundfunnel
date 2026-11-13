# Linux: 手元で鳴らさずに送る（PipeWire の仮想出力デバイス）

SoundFunnel の「システムの音」は、選んだ出力デバイスに流れている音（その出力の「モニタ」）を取り込んで送ります。
実際のスピーカーを取り込み元にすると、送るのと同時に手元のスピーカーからも音が鳴ります。

**どこにも鳴らない仮想の出力デバイス**を作り、システムの既定の出力にしておくと、
アプリの音は手元では鳴らず、サーバの PC だけで鳴ります。

```
アプリの音 ─→ 仮想出力「SoundFunnel 送信用」（システム既定の出力。どこにも鳴らさない）
                    │ モニタ
                    ↓
              SoundFunnel「システムの音」─→ サーバ PC で再生
```

PipeWire（Ubuntu 22.10 以降の標準）を前提に説明します。root 権限は要りません。

## 1. 仮想出力デバイスを作る

`~/.config/pipewire/pipewire.conf.d/30-soundfunnel-send.conf` を作ります。

```bash
mkdir -p ~/.config/pipewire/pipewire.conf.d
cat > ~/.config/pipewire/pipewire.conf.d/30-soundfunnel-send.conf <<'EOF'
# どこにも鳴らない仮想出力。モニタを SoundFunnel の「システムの音」で取り込む
context.objects = [
    { factory = adapter
        args = {
            factory.name     = support.null-audio-sink
            node.name        = "soundfunnel-send"
            node.description = "SoundFunnel 送信用"
            media.class      = "Audio/Sink"
            audio.position   = [ FL FR ]
        }
    }
]
EOF
```

- `node.name`: 内部名です。英数字とハイフンで付けます。
- `node.description`: サウンド設定や SoundFunnel の「取り込む出力先」に出る表示名です。好きな名前にできます。

PipeWire を再起動して読み込ませます（鳴っている音は一瞬途切れます）。

```bash
systemctl --user restart pipewire pipewire-pulse wireplumber
```

確認:

```bash
wpctl status          # Sinks に「SoundFunnel 送信用」が出ていれば OK
```

## 2. システムの既定の出力にする

GNOME の「設定」→「サウンド」→「出力」で「SoundFunnel 送信用」を選びます。
コマンドなら、`wpctl status` で表示される番号（ID）を指定します。

```bash
wpctl set-default <ID>
```

WirePlumber は選んだ既定の出力を覚えているので、再起動後もこの設定のままになります。

## 3. SoundFunnel で取り込む

「送信」タブで「送るもの」を **システムの音** にし、「取り込む出力先」で **SoundFunnel 送信用** を選びます。
コマンドラインなら次のとおりです。

```bash
SoundFunnel --loopback --loopback-device "SoundFunnel 送信用"
```

JACK や `pw-jack`、配線（`pw-link`）の設定は不要です。

## 注意

- **手元では何も聞こえなくなります。** 手元で聞きたいときは、既定の出力を実際のスピーカーに戻してください。
  特定のアプリだけを送りたいときは、そのアプリの出力だけを `pavucontrol` などで「SoundFunnel 送信用」に向けます。
- **音量キー**で変わるのは仮想出力の音量で、送る音の大きさがそのまま変わります。
- **この PC をサーバにするとき**は、SoundFunnel の「オーディオ」タブの出力を、実際のスピーカーにしてください。
  既定の出力（＝仮想出力）のままだと、受け取った音が仮想出力に流れ込みます。
  それを「システムの音」で取り込んでまた送るので、音が回ります。
- 遅延をさらに詰めたい場合は、PipeWire の処理単位を小さく固定できます（CPU 負荷は上がります）。

  ```bash
  cat > ~/.config/pipewire/pipewire.conf.d/10-clock.conf <<'EOF'
  context.properties = {
      default.clock.force-quantum = 256
  }
  EOF
  ```

## 名前を変える / 削除する

設定ファイルの `node.description`（と `node.name`）を書き換えるか、ファイルを削除してから、
PipeWire を再起動します。

```bash
systemctl --user restart pipewire pipewire-pulse wireplumber
```

名前を変えた場合は、SoundFunnel の「取り込む出力先」も選び直してください。
削除した場合は、サウンド設定で既定の出力を実際のスピーカーに戻してください。
