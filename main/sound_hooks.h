#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 試作0の音トリガー用フック(仮実装)。
 * ハードウェア検討メモの低遅延アーキテクチャ(ISR検出→専用FreeRTOSタスク→
 * I2S+DMA, PSRAM常駐PCM, esp_codec_dev 1.1.0固定)は、このファイルの中身を
 * 差し替える形で実装する想定。現時点ではログ出力のみのスタブとし、
 * まず操作フロー側(このセッションで書いたnav/ui/encoder)の動作確認を優先する。
 */

typedef enum {
    UI_SOUND_MOVE,    // 選択項目が変わった(ダイヤルを1ステップ回した)
    UI_SOUND_CONFIRM, // 決定操作
    UI_SOUND_BACK,    // キャンセル/戻る操作
    UI_SOUND_DONE,    // 全パラメータ確定(一連の操作フロー完了)
} ui_sound_id_t;

void sound_hooks_init(void);
void sound_hooks_play(ui_sound_id_t id);

#ifdef __cplusplus
}
#endif
