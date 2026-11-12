# ビルドと配布

## 必要なもの（共通）

- CMake 3.22 以降、git
- C++17 コンパイラ
- サブモジュール（JUCE 7.0.12）: `git clone --recursive`、または取得後に
  `git submodule update --init --recursive`
- Opus は configure 時に自動で取得します（FetchContent、v1.5.2）。ネットワーク接続が必要です。

JUCE 8 は AGPLv3 のため使っていません。JUCE のサブモジュールは 7.0.12 に固定しています。

## Windows

- Visual Studio 2022 以降（「C++ によるデスクトップ開発」）。CMake は VS 同梱のものでも構いません。

```bash
cmake -S . -B build
```

```bash
cmake --build build --config Release --target SoundFunnel
```

出力: `build/SoundFunnel_artefacts/Release/SoundFunnel.exe`（静的 CRT。VC++ 再頒布パッケージは不要）

インストーラ: [Inno Setup 6](https://jrsoftware.org/isinfo.php) を入れて、

```bash
iscc /DAppVersion=0.1.0 packaging/windows/SoundFunnel.iss
```

`build/installer/SoundFunnel-0.1.0-windows-x64-setup.exe` ができます。

## macOS

- Xcode（コマンドラインツール）。macOS 11 以降を対象にビルドします（`CMAKE_OSX_DEPLOYMENT_TARGET`）。

```bash
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
```

```bash
cmake --build build --config Release --target SoundFunnel
```

出力: `build/SoundFunnel_artefacts/Release/SoundFunnel.app`。ビルド後に ad-hoc 署名します。
ユニバーサルビルドでは、Opus の SIMD 最適化を切っています（アーキテクチャごとに判定できないため）。

dmg:

```bash
sh packaging/macos/make_dmg.sh build/SoundFunnel_artefacts/Release/SoundFunnel.app SoundFunnel.dmg
```

注意:

- ad-hoc 署名はビルドのたびに変わるため、macOS はマイクとローカルネットワークの許可を毎回求めます。
- 正式配布には Developer ID での署名と公証が必要です（未対応。[PLAN.md](PLAN.md)）。

## Linux

Debian / Ubuntu の場合:

```bash
sudo apt install build-essential cmake ninja-build libasound2-dev libjack-jackd2-dev libfreetype-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libglu1-mesa-dev mesa-common-dev
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build --target SoundFunnel
```

出力: `build/SoundFunnel_artefacts/Release/SoundFunnel`

パッケージ（.deb と tar.gz）:

```bash
cd build && cpack -G "DEB;TGZ"
```

## 自己テスト

```bash
build/SoundFunnel_artefacts/Release/SoundFunnel --selftest
```

検出パケットのエンコード / デコード、HMAC 認証、改ざん検出、サーバの選出順序などを確かめます。
画面のない Linux では `xvfb-run -a` を付けて実行します。実機での動作確認の方法は [TESTING.md](TESTING.md)。

## CI

[.github/workflows/build.yml](../.github/workflows/build.yml) が、`main` への push と pull request のたびに
Windows / macOS（ユニバーサル）/ Linux でビルドし、自己テストとパッケージ作成まで行います。
できたパッケージは、実行結果の Artifacts から取得できます。

## リリース手順

1. `CMakeLists.txt` の `project(SoundFunnel VERSION x.y.z)` を上げる。
2. [CHANGELOG.md](../CHANGELOG.md) に変更点を書く。
3. コミットして、タグ `vx.y.z` を付けて push する（タグとプロジェクトのバージョンが違うと CI が失敗します）。

   ```bash
   git tag vx.y.z
   ```

   ```bash
   git push origin main vx.y.z
   ```

4. CI が 3 OS のパッケージを添付した **下書き** のリリースを作るので、内容を確認して公開する。
