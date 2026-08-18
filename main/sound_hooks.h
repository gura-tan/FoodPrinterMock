#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 試作0の音トリガー用フック。
 * UI_SOUND_BUTTON/MOVE/CONFIRM/BACK/DONEのいずれも、SDカード上の対応する
 * wavファイルを起動時に読み込み済みであれば実際に再生する(詳細は
 * sound_hooks.cおよびmain/SD_CARD_SOUND_SETUP.md参照)。
 */

typedef enum {
    UI_SOUND_BUTTON,   // ボタンを押した瞬間(押し始め)。実際に音を鳴らす最初の対象
    UI_SOUND_MOVE,     // 選択項目が変わった(ダイヤルを1ステップ回した)
    UI_SOUND_CONFIRM,  // 決定操作
    UI_SOUND_BACK,     // キャンセル/戻る操作
    UI_SOUND_DONE,     // 全パラメータ確定(一連の操作フロー完了)
} ui_sound_id_t;

void sound_hooks_init(void);

/* 再生中の音を即座に打ち切って割り込む(ボタン押下・ダイヤル操作など、
 * ユーザーの新しい入力に対する即時フィードバック用)。 */
void sound_hooks_play(ui_sound_id_t id);

/* 再生中の音を打ち切らず、自然に終わるのを待ってから鳴らす。
 * ただし待っている間にsound_hooks_play()(即時割り込み版)や
 * 別のsound_hooks_play_chained()が呼ばれると、そちらに上書きされて
 * このリクエストは鳴らないまま消える。
 * (例: ボタンを押した瞬間のクリック音がまだ再生中でも、離した瞬間の
 * CONFIRM/BACK/DONE音がそのクリック音のテールを打ち切らないようにする) */
void sound_hooks_play_chained(ui_sound_id_t id);

#ifdef __cplusplus
}
#endif
