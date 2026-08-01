ここに button.wav を置いてください。

要件(sound_hooks.c の簡易WAVパーサが対応する範囲):
- RIFF/WAVE形式、非圧縮PCM (Audacity等で「WAV (Microsoft) signed 16-bit PCM」
  のような単純な形式でエクスポートしてください。WAVE_FORMAT_EXTENSIBLEには
  対応していません)
- モノラル/ステレオどちらも可、サンプルレート・ビット深度も特に制限は
  ありませんが、AW88298(CoreS3内蔵アンプ)で確認が取れているのは
  16bit / 16kHz〜48kHz程度です

ファイルサイズについて:
このWAVはSDカードからではなく、ファームウェアに直接埋め込まれます
(main/CMakeLists.txtのEMBED_FILES)。ビルドログで見えていたfactoryパー
ティションは1MBしかないため、button.wavは短く(1〜2秒程度、数十〜
100KB程度まで)しておくことを推奨します。長い/高音質な音を使いたく
なったら、その時点でSDカード読み込みに切り替えるのがよいと思います。

このファイル(README_PLACE_WAV_HERE.txt)はbutton.wavを置いたら削除して
問題ありません。
