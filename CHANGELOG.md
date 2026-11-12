# 変更履歴

## 0.1.0（未リリース）

最初の版。

- LAN 内の自動検出（マルチキャスト + ブロードキャスト、グループキーによる HMAC 認証）
- 「最後に名乗った PC がサーバ」になるサーバの選出と移動
- クライアント → サーバへの音声送信（AOO、Opus / PCM）、サーバでのミックス再生
- サーバからのクライアントごとのミュートと音量
- 送る音の選択: 音声入力、またはシステムの音（Windows: WASAPI ループバック、Linux: PulseAudio / PipeWire のモニタ）
- トレイ常駐（Windows / macOS）、Linux は通常ウィンドウ
- サインイン時の自動起動
- 日本語 / 英語の UI
- macOS: マイクとローカルネットワークの許可に対応（許可待ちでも固まらない）
- インストーラ: Windows（Inno Setup）、macOS（dmg）、Linux（.deb / tar.gz）
- GitHub Actions による 3 OS のビルド・自己テスト・パッケージ作成
