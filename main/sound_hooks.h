#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 試作0の音トリガー用フック。
 * UI_SOUND_HIT/PROCEED/MOVE/BACK/DONEのいずれも、SDカード上の対応する
 * wavファイルを起動時に読み込み済みであれば実際に再生する(詳細は
 * sound_hooks.cおよびmain/SD_CARD_SOUND_SETUP.md参照)。
 *
 * HIT(押した瞬間)とPROCEED(離した瞬間)は、続けて鳴らすことで
 * 「ヒット→決定」のワンフレーズになるように意図されている。そのため
 * PROCEEDはsound_hooks_play()(即時割り込み版)で鳴らし、再生中のHITが
 * あればそこで打ち切ってPROCEEDに繋げる(sound_hooks_play_chained()は
 * 使わない)。
 */

typedef enum {
    UI_SOUND_HIT,      // ボタンを押した瞬間(押し始め)
    UI_SOUND_PROCEED,  // ボタンを離した瞬間(短押し=決定操作)。
                        // HITと連続して鳴ることで一つのフレーズになる想定
    UI_SOUND_MOVE,      // 選択項目が変わった(ダイヤルを1ステップ回した)
    UI_SOUND_BACK,      // キャンセル/戻る操作
    UI_SOUND_DONE,      // 全パラメータ確定(一連の操作フロー完了)
} ui_sound_id_t;

void sound_hooks_init(void);

/* 再生中の音を即座に打ち切って割り込む(ボタン押下・ダイヤル操作など、
 * ユーザーの新しい入力に対する即時フィードバック用。HITの再生中に
 * PROCEEDを鳴らす場合もこちらを使い、HITを打ち切ってPROCEEDに繋げる)。 */
void sound_hooks_play(ui_sound_id_t id);

/* 再生中の音を打ち切らず、自然に終わるのを待ってから鳴らす。
 * ただし待っている間にsound_hooks_play()(即時割り込み版)や
 * 別のsound_hooks_play_chained()が呼ばれると、そちらに上書きされて
 * このリクエストは鳴らないまま消える。
 * (例: MOVE音がまだ再生中でも、その後のBACK/DONE音がMOVE音のテールを
 * 打ち切らないようにする) */
void sound_hooks_play_chained(ui_sound_id_t id);

#ifdef __cplusplus
}
#endif
