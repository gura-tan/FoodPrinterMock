#pragma once
#include "input_source.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 試作0の音トリガー用フック。
 * SDカード上の対応するwavファイルを起動時に読み込み済みであれば実際に再生する
 * (詳細はsound_hooks.cおよびmain/SD_CARD_SOUND_SETUP.md参照)。
 *
 * HIT(押した瞬間)とPROCEED(離した瞬間)は、続けて鳴らすことで
 * 「ヒット→決定」のワンフレーズになるように意図されている。そのため
 * PROCEEDはsound_hooks_play()(即時割り込み版)で鳴らし、再生中のHITが
 * あればそこで打ち切ってPROCEEDに繋げる(sound_hooks_play_chained()は
 * 使わない)。
 *
 * 【入力ごとの音の区別】
 * UI_SOUND_MOVE_CATEGORY / UI_SOUND_MOVE_PARAMだけは、操作した入力
 * (input_source_t)によって別のwavを鳴らせる(sound_hooks_play_from())。
 * 他のIDは入力によらず共通の音。フォールバックの順序は
 * sound_hooks_play_from()のコメント参照。
 */

/* 【重要】この並びと値を変えないこと(sim/sim_sound.cのJS側idNamesと連動している)。
 * 新しい音を追加するときはUI_SOUND_COUNTの直前に足し、sim_sound.cのidNames
 * (と、実機側のk_slot_filenames)も同じ順に更新する。 */
typedef enum {
    UI_SOUND_HIT,      // ボタンを押した瞬間(押し始め)
    UI_SOUND_PROCEED,  // ボタンを離した瞬間(短押し=決定操作)。
                        // HITと連続して鳴ることで一つのフレーズになる想定
    UI_SOUND_MOVE_CATEGORY, // カテゴリ/メニュー選択画面で選択項目が変わった
                             // (大カテゴリ/小カテゴリ/メニュー選択、ダイヤルを1ステップ回した)
                             // 入力ごとにemovecat/smovecatを使い分ける(フォールバックあり)
    UI_SOUND_MOVE_PARAM,    // パラメーター調整画面で選択項目が変わった
                             // (ダイヤルを1ステップ回した)
                             // 入力ごとにemoveprm/smoveprmを使い分ける(フォールバックあり)
    UI_SOUND_BACK,      // キャンセル/戻る操作
    UI_SOUND_DENY,      // 操作してもなにも変化が起きなかった(画面遷移演出中の
                        // 入力・リストの端で範囲外方向へ回した場合など)
    UI_SOUND_READY,     // 調理中画面のカウントダウンが0になった瞬間
    UI_SOUND_DONE,      // 全パラメータ確定(一連の操作フロー完了)
    UI_SOUND_COUNT,     // 音IDの総数(番兵。音として鳴らすIDではない)
} ui_sound_id_t;

void sound_hooks_init(void);

/* 再生中の音を即座に打ち切って割り込む(ボタン押下・ダイヤル操作など、
 * ユーザーの新しい入力に対する即時フィードバック用。HITの再生中に
 * PROCEEDを鳴らす場合もこちらを使い、HITを打ち切ってPROCEEDに繋げる)。
 * MOVE_CATEGORY/MOVE_PARAMはINPUT_SRC_ENCODER扱いになる
 * (入力ごとに区別したいときはsound_hooks_play_from()を使う)。 */
void sound_hooks_play(ui_sound_id_t id);

/* sound_hooks_play()と同じ即時割り込み版で、どの入力の操作かを指定する。
 * MOVE_CATEGORY/MOVE_PARAM以外のIDではsrcは無視される(共通の音)。
 *
 * MOVE_CATEGORY/MOVE_PARAMのフォールバック順(先頭から探し、最初に読み込めて
 * いたものを鳴らす。MovePrmも同様にemoveprm/smoveprm/moveprmで読み替える):
 *   INPUT_SRC_ENCODER: emovecat → 従来のmovecat
 *   INPUT_SRC_SCROLL : smovecat → emovecat → 従来のmovecat
 * 従来のmovecat.wav/moveprm.wavは、emovecat.wav/emoveprm.wavが無いときだけ
 * 起動時に読み込まれる(ヒープの重複を避けるため)。
 * どれも無い場合は音は鳴らないが、再生中の音は打ち切られる(従来と同じ挙動)。 */
void sound_hooks_play_from(ui_sound_id_t id, input_source_t src);

/* 再生中の音を打ち切らず、自然に終わるのを待ってから鳴らす。
 * ただし待っている間にsound_hooks_play()(即時割り込み版)や
 * 別のsound_hooks_play_chained()が呼ばれると、そちらに上書きされて
 * このリクエストは鳴らないまま消える。
 * (例: MOVE音がまだ再生中でも、その後のBACK/DONE音がMOVE音のテールを
 * 打ち切らないようにする)
 * MOVE_CATEGORY/MOVE_PARAMはINPUT_SRC_ENCODER扱い。 */
void sound_hooks_play_chained(ui_sound_id_t id);

#ifdef __cplusplus
}
#endif
