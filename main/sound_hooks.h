#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 試作0の音トリガー用フック。
 * 最初のステップとして UI_SOUND_BUTTON (ボタンを押した瞬間) のみ実際に
 * button.wav を再生する。他のイベント(MOVE/CONFIRM/BACK/DONE)は
 * これまで通りログ出力のみのスタブ。
 *
 * 音声データはmicroSDからではなく、main/sounds/button.wavをEMBED_FILESで
 * ファームウェアに直接埋め込んだものを再生する(main/CMakeLists.txt参照)。
 * SDカード読み込みに切り替える際は、sound_hooks.c内部の「PCMデータへの
 * ポインタを渡して鳴らす」という構造はそのまま使い、データの取得元だけを
 * 差し替えればよい。
 *
 * ハードウェア検討メモの低遅延アーキテクチャ(GPIO割り込み→専用FreeRTOS
 * タスク→I2S+DMA, PSRAM常駐PCM)のうち、専用タスクでの再生は実装済みだが、
 * 「都度open/closeする」実装になっているため、まだ最終形の低遅延構成
 * ではない(今後の改善点)。
 */

typedef enum {
    UI_SOUND_BUTTON,   // ボタンを押した瞬間(押し始め)。実際に音を鳴らす最初の対象
    UI_SOUND_MOVE,     // 選択項目が変わった(ダイヤルを1ステップ回した)
    UI_SOUND_CONFIRM,  // 決定操作
    UI_SOUND_BACK,     // キャンセル/戻る操作
    UI_SOUND_DONE,     // 全パラメータ確定(一連の操作フロー完了)
} ui_sound_id_t;

void sound_hooks_init(void);
void sound_hooks_play(ui_sound_id_t id);

#ifdef __cplusplus
}
#endif
