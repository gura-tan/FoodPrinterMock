#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 操作入力の種類。実機(main/)とシミュレータ(sim/)の両方から参照する
 * 共有ヘッダなので、ESP-IDFやLVGLのヘッダには依存させないこと
 * (simは main/ 全体をビルドせず、必要なファイルだけを取り込んでいる)。
 *
 * 音の区別(MoveCat/MovePrm)と、app_main.cの入力ごとの状態(per-source)の
 * 添字の両方に使う。値を変えると配列の添字がずれるので、並びを変えないこと。
 * 新しい入力を追加するときはINPUT_SRC_COUNTの直前に足す。
 */
typedef enum {
    INPUT_SRC_ENCODER = 0,  // Unit Encoder (U135) / Port.A
    INPUT_SRC_SCROLL,       // Unit Scroll (U186)  / Port.B
    INPUT_SRC_COUNT,
} input_source_t;

#ifdef __cplusplus
}
#endif
