# SDカードへの音声ファイル配置

## ディレクトリ構成

```
/sdcard/
├── sound_preset.txt        (省略可。中身は使用するプリセット名を1行だけ。
│                             無ければ "default" を使う)
└── sounds/
    └── default/             (sound_preset.txt の中身、省略時は "default")
        ├── button.wav
        ├── move.wav
        ├── confirm.wav
        ├── back.wav
        └── done.wav
```

プリセットを切り替えたいときは `/sounds/` の下に別名のフォルダ
(例: `sounds/soft/`)を用意し、`sound_preset.txt` の中身を `soft` に
書き換える。現状は**起動時に1回だけ**読むので、切り替えには再起動が必要
(実行中のホットリロードは未実装、将来のタスク候補)。

## WAVファイルの要件

`sound_hooks.c` の簡易パーサが対応する範囲:

- RIFF/WAVE形式、非圧縮PCM
  (Audacityなら「WAV (Microsoft) signed 16-bit PCM」のような単純な形式で
  エクスポートすること。WAVE_FORMAT_EXTENSIBLEには非対応)
- **モノラル推奨**(CoreS3内蔵アンプAW88298はモノラルのため、ステレオにしても
  容量が倍になるだけで聞こえ方は変わらない)
- 16bit / 44.1kHzまたは48kHz程度を推奨
  (AW88298での動作確認範囲は16bit / 16kHz〜48kHz程度)

## ファイルが無い/壊れている場合

そのIDの音だけが無効になり(起動ログにWARNINGが出る)、他の音や画面遷移
自体は問題なく動作を続ける(EMBED_FILES版のときと同じ挙動)。

## 容量について

ファームウェアへの埋め込みではなくなったため、1MBパーティションのような
制約はない。16GBカードなら、上記の推奨品質のクリップを数百〜数千個入れても
まったく問題にならない。
