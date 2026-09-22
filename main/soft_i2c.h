#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ソフトウェア(ビットバング)I2Cマスタ。Unit Scroll(Port.B)専用。
 *
 * 【なぜソフトウェア実装か】
 * ESP32-S3のハードI2Cコントローラは2つだけ(sdkconfigのCONFIG_SOC_I2C_NUM=2)。
 * 1つはBSPの内部バス(GPIO11/12, I2C_NUM_1)、もう1つはPort.AのEncoder
 * (encoder_unit.c, I2C_NUM_0固定)が使うため、Scroll用の3本目のハードI2Cは
 * 作れない。そのためGPIOを直接叩いてI2Cを実装している。
 * (I2Cハブを買ってScrollもPort.Aに集約する場合、このモジュールは不要になる。
 *  差し替えはscroll_unit.cのscroll_bus_*()の中身だけで済むようにしてある)
 *
 * 【配線】Port.B: G9(黄)=SDA, G8(白)=SCL。Grove規格の慣例に基づく想定で、
 * 実機で未確認(scroll_unit.hの「未検証事項」参照)。
 *
 * 【スレッド】排他制御は持たない。app_mainのタスクからのみ呼ぶこと。
 * 他タスクから呼ぶ設計にするなら、呼び出し側でミューテックスを取ること。
 * 転送中に割り込み禁止にはしない(数msの間、他タスク/割り込みに横取りされて
 * クロックが延びても、I2Cのスレーブはマスタの遅いクロックを許容する)。
 */

#define SOFT_I2C_SCL_GPIO            8
#define SOFT_I2C_SDA_GPIO            9

/* 半周期。10usで1周期20us=約50kHz。Port.Bは内部プルアップ(数十kΩ)頼みで
 * 立ち上がりが遅いので、Encoder側の100kHzより遅くしてある。 */
#define SOFT_I2C_HALF_PERIOD_US      10

/* クロックストレッチ(スレーブがSCLをLowのまま離さない)を待つ上限。
 * 1us刻みで数えるので、実際の待ち時間はこの値以上(ループのオーバーヘッド分長い)。 */
#define SOFT_I2C_STRETCH_TIMEOUT_US  2000

/* GPIO8/9をオープンドレイン+内部プルアップに設定する。既に初期化済みなら
 * ESP_OKを即返す。SDAがLowに張り付いていた場合はバスクリアを試みる。 */
esp_err_t soft_i2c_init(void);

/* レジスタ読み出し: START → addr(W) → reg → リピートスタート → addr(R) →
 * len バイト読み出し → STOP。最後の1バイトだけNACKを返す。
 * 戻り値:
 *   ESP_OK                 成功
 *   ESP_ERR_NOT_FOUND      アドレスにACKが返らない(未接続・電源なし・配線違い)
 *   ESP_FAIL               レジスタ書き込み/リード開始のACKが返らない
 *   ESP_ERR_TIMEOUT        クロックストレッチが上限を超えた
 *   ESP_ERR_INVALID_STATE  soft_i2c_init()前
 * 失敗時は内部でバスクリア(SCLを最大9発+STOP)を行ってから戻る。 */
esp_err_t soft_i2c_write_read(uint8_t addr7, uint8_t reg, uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif
