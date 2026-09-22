# SDカードへの音声ファイル配置

## ディレクトリ構成

```
/sdcard/
├── sound_preset.txt        (省略可。中身は使用するプリセット名を1行だけ。
│                             無ければ "default" を使う)
└── sounds/
    └── default/             (sound_preset.txt の中身、省略時は "default")
        ├── hit.wav          (ボタンを押した瞬間の音。Encoder/Scroll共通)
        ├── proceed.wav      (ボタンを離した瞬間=決定操作の音。Encoder/Scroll共通。
        │                     再生中のhit.wavを打ち切って鳴るため、
        │                     hit→proceedで一つのフレーズになるように
        │                     作ること)
        ├── emovecat.wav     (【Encoder用】カテゴリ/メニュー選択画面で
        │                     選択項目が変わった音)
        ├── emoveprm.wav     (【Encoder用】パラメーター調整画面で
        │                     選択項目が変わった音)
        ├── smovecat.wav     (【Scroll用】カテゴリ/メニュー選択画面で
        │                     選択項目が変わった音)
        ├── smoveprm.wav     (【Scroll用】パラメーター調整画面で
        │                     選択項目が変わった音)
        ├── movecat.wav      (【互換用】emovecat/smovecatが無いときの
        │                     フォールバック。旧バージョンとの互換のために残してある)
        ├── moveprm.wav      (【互換用】emoveprm/smoveprmが無いときの
        │                     フォールバック)
        ├── back.wav         (Encoder/Scroll共通)
        ├── deny.wav         (操作しても何も変化が起きなかったことを伝える音。
        │                     画面遷移の演出中の入力や、リストの端で範囲外
        │                     方向へダイヤルを回した場合などに鳴る。
        │                     Encoder/Scroll共通)
        ├── ready.wav        (調理中画面のカウントダウンが0になった瞬間の音。共通)
        └── done.wav         (共通)
```

## 【Unit Scroll追加に伴う変更】入力ごとの音の区別

この試作機はUnit Encoder(Port.A)とUnit Scroll(Port.B)のどちらからでも
同じように操作できる。研究の主題である「入力機構ごとに音が操作感を
どう変えるか」を比較できるよう、**カテゴリ/メニュー移動とパラメータ移動の音
(MoveCat/MovePrm)だけ**、操作した入力ごとに別のwavを鳴らせるようにしてある。
hit/proceed/back/deny/ready/doneはどちらの入力で操作しても共通の音。

### フォールバックの順序

ファイルが見つからない場合、次の順で探し、最初に読み込めたものを使う
(見つからなかったこと自体はエラー扱いではなく、起動ログにINFOで出るだけ):

- **Encoder**: `emovecat.wav` → 従来の `movecat.wav`
  (MovePrmは `emoveprm.wav` → `moveprm.wav`)
- **Scroll**: `smovecat.wav` → `emovecat.wav` → 従来の `movecat.wav`
  (MovePrmは `smoveprm.wav` → `emoveprm.wav` → `moveprm.wav`)

つまり、`movecat.wav`/`moveprm.wav`しか置いていない従来のSDカードでも
そのまま動く(Encoder/Scrollとも同じ音が鳴る)。`emovecat.wav`/`emoveprm.wav`
だけ追加すれば、Scrollもそれを使う(専用のsmovecat/smoveprmが無い間は
Encoder用の音を借りる)。4つ全部揃えて初めて、入力ごとに完全に別の音になる。

従来の`movecat.wav`/`moveprm.wav`は、対応する新ファイル(`emovecat.wav`/
`emoveprm.wav`)が既にあるときは読み込まれない(起動時のヒープ消費を
二重にしないため)。

## WAVファイルの要件

`sound_hooks.c` の簡易パーサが対応する範囲:

- RIFF/WAVE形式、非圧縮PCM
  (Audacityなら「WAV (Microsoft) signed 16-bit PCM」のような単純な形式で
  エクスポートすること。WAVE_FORMAT_EXTENSIBLEには非対応)
- **サンプリング周波数は16kHz/32kHz/48kHzのいずれかを使うこと。44.1kHzは
  使わない**(下記の実機確認参照。半端な値でクロック生成が噛み合わないと
  見られる)
- 16bit
- モノラル/ステレオはどちらでも動作を確認済み(16kHz・32kHz・48kHzの
  ステレオで確認。容量を気にするならモノラルでもよい)

### 【2026/08/08〜09 実機確認】44.1kHzのファイルだけ無音になる

button.wav/move.wavの組み合わせを変えながら実機で確認したところ、
サンプリング周波数だけが結果を分けることが分かった(全て16bit):

| サンプリング周波数 | チャンネル | 結果 |
| --- | --- | --- |
| 16kHz | ステレオ | 鳴る |
| 32kHz | ステレオ | 鳴る |
| 44.1kHz | モノラル | **鳴らない** |
| 44.1kHz | ステレオ | **鳴らない** |
| 48kHz | ステレオ | 鳴る |

ログ上は「マウント成功→WAVパース成功→コーデックopen成功→PCM書き込み完了
(エラーなし・所要時間も正確)」と何もかも正常に見え、読み込んだPCMの振幅も
無音ではない(peak値が最大値の約半分程度)のに、44.1kHzのときだけ実機の
スピーカーから音が一切出ない。モノラル/ステレオは結果に影響しなかった。

原因はI2S/コーデック側のクロック生成が44.1kHz(48kHzや32kHzのようなキリの
良い値ではない)とうまく噛み合っていないためと推測されるが、根本原因の
特定はしていない。**新しい音源は16kHz/32kHz/48kHzのいずれかでエクスポート
すること。現状は48kHz/16bit/ステレオを標準として採用している。**

### 検証用: SDカードを抜き差しせずにプリセットを切り替える

`main/sound_hooks.c` 冒頭の `SOUND_PRESET_OVERRIDE` に値を入れると、
SDカードの `preset.txt` を書き換えなくても、`idf.py build && idf.py flash`
するだけで `/sdcard/sounds/<その名前>/` を優先的に読みに行く
(空文字列 `""` なら従来通りSDカードの`preset.txt`を見る)。
複数のフォーマットパターンをSDカードに用意しておき、これで切り替えながら
実機で聞き比える際に使う。

### 検証用: 実機上でボタン長押しからプリセットを選ぶ(デバッグ選択画面)

再フラッシュせずに実機だけでプリセットを選び直したい場合は、**Encoderまたは
Scroll、どちらか一方のボタンを押したまま電源を入れる(またはリセットする)**と、
通常のメニューの代わりに `/sdcard/sounds/` 直下のフォルダ名一覧が選択画面に
表示される。選択画面自体もEncoder/Scrollどちらの回転・ボタンでも操作でき、
ダイヤルで選び、ボタンを短押しして離すと確定し、自動的に再起動する。
次回起動(ボタンを押さない通常起動)からは選んだプリセットが使われる。

選択結果はSDカードではなくNVS(ESP32内蔵フラッシュ)に保存されるため、
`preset.txt` は書き換わらない。一覧の先頭にある「(SDのpreset.txtに戻す)」を
選ぶと、この上書き設定を消去して通常通り `preset.txt` / `default` の挙動に
戻せる。

優先順位: `SOUND_PRESET_OVERRIDE`(コンパイル時、空でなければ常に最優先)
＞ このデバッグ選択画面で選んだプリセット(NVS) ＞ SDカードの `preset.txt`
＞ `default`。

## ファイルが無い/壊れている場合

- hit/proceed/back/deny/ready/done: そのIDの音だけが無効になり
  (起動ログにWARNINGが出る)、他の音や画面遷移自体は問題なく動作を続ける。
- emovecat/emoveprm/smovecat/smoveprm: 見つからなくてもWARNINGではなく
  INFOログで済む(フォールバックがあるため異常ではない)。
- movecat/moveprm(従来ファイル): 対応するemove系がある場合は最初から
  読み込まないので、無くてもINFOログのみ。emove系も無く、movecat/moveprm
  も無い場合は、そのMoveCat/MovePrmはどの入力からでも無音になる
  (起動ログにWARNINGが出る)。

## 容量について

ファームウェアへの埋め込みではなくなったため、1MBパーティションのような
制約はない。16GBカードなら、上記の推奨品質のクリップを数百〜数千個入れても
まったく問題にならない。
