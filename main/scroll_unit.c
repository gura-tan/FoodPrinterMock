#include "scroll_unit.h"
#include "soft_i2c.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "scroll_unit";

/* ---- バスアクセス層 ----
 * このドライバがI2Cに触れるのは、この2つのstatic関数だけ。将来I2Cハブで
 * ScrollをPort.Aに集約する場合は、この2つの中身をi2c_master系
 * (i2c_master_bus_add_device / i2c_master_transmit_receive)に差し替えるだけで
 * 済む(ドライバ本体・app_main側は変更不要)。 */
static esp_err_t scroll_bus_init(void)
{
    return soft_i2c_init();
}

static esp_err_t scroll_bus_read_reg(uint8_t reg, uint8_t *buf, size_t len)
{
    return soft_i2c_write_read(SCROLL_I2C_ADDR, reg, buf, len);
}

/* ---- ドライバ状態 ---- */
static bool    s_bus_ready;         // バス初期化済み(診断ダンプはこれだけで使える)
static bool    s_initialized;       // カウンタの初期読み出しまで成功(pollに必要)
static bool    s_need_resync;       // 通信失敗の後、次の成功読み出しで基準値を取り直す
static int16_t s_last_counter;
static int32_t s_raw_delta_carry;   // 1ステップに満たない端数の繰り越し

static esp_err_t read_counter16(int16_t *out)
{
    uint8_t raw[2] = {0};
    esp_err_t err = scroll_bus_read_reg(SCROLL_REG_COUNTER, raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }
    *out = (int16_t)(raw[0] | (raw[1] << 8)); // リトルエンディアン, 符号付き
    return ESP_OK;
}

esp_err_t scroll_init(void)
{
    esp_err_t err = scroll_bus_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "soft I2C init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_bus_ready = true;

    int16_t counter = 0;
    err = read_counter16(&counter);
    if (err != ESP_OK) {
        /* 未接続は正常な使い方なのでERRORにはしない。ただし接続しているのに
         * ここに来る場合は、Port.Bの5V供給/配線(黄=SDA,白=SCL)/プルアップ/
         * レジスタアドレス(SCROLL_REG_COUNTER)を疑うこと。 */
        ESP_LOGW(TAG, "initial counter read failed: %s "
                      "(未接続、または配線/5V供給/プルアップ/レジスタ配置が想定と違う可能性)",
                 esp_err_to_name(err));
        return err;
    }

    s_last_counter = counter;
    s_raw_delta_carry = 0;
    s_need_resync = false;
    s_initialized = true;
    ESP_LOGI(TAG, "scroll_unit initialized, initial raw counter=%d "
                  "(reg=0x%02X, ticks/step=%d, sign=%d, pressed level=%d: すべて未検証の仮値)",
             (int)s_last_counter, SCROLL_REG_COUNTER, SCROLL_RAW_TICKS_PER_STEP,
             SCROLL_DIRECTION_SIGN, SCROLL_BUTTON_PRESSED_LEVEL);
    return ESP_OK;
}

esp_err_t scroll_poll(int32_t *out_delta, bool *out_button_pressed)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* カウンタとボタンを両方読み終えてから内部状態を更新する。片方だけ成功して
     * 基準値だけ進んでしまうと、その回転が取りこぼされるため。 */
    int16_t counter = 0;
    esp_err_t err = read_counter16(&counter);
    if (err != ESP_OK) {
        s_need_resync = true;
        return err;
    }

    bool pressed = false;
    if (out_button_pressed) {
        uint8_t btn = 0;
        err = scroll_bus_read_reg(SCROLL_REG_BUTTON, &btn, sizeof(btn));
        if (err != ESP_OK) {
            s_need_resync = true;
            return err;
        }
        pressed = (btn == SCROLL_BUTTON_PRESSED_LEVEL);
    }

    /* 通信失敗の後の最初の成功では、途中の変化量が分からないので差分は0として
     * 基準値だけ取り直す。 */
    int32_t raw_delta = 0;
    if (s_need_resync) {
        s_raw_delta_carry = 0;
        s_need_resync = false;
    } else {
        /* int16のまま引き算して折り返す(32767→-32768をまたいでも+1になる) */
        raw_delta = (int16_t)(counter - s_last_counter);
    }
    s_last_counter = counter;

    if (out_delta) {
        /* encoder_unit.cと同じ端数繰り越し: ポーリングタイミングによって
         * 1ノッチ分の増分が分割されて読めても、取りこぼし/重複を出さずに
         * 1ノッチ=1ステップへ変換する。 */
        int32_t total = s_raw_delta_carry + raw_delta * SCROLL_DIRECTION_SIGN;
        int32_t steps = total / SCROLL_RAW_TICKS_PER_STEP; // 0方向への切り捨て
        s_raw_delta_carry = total - steps * SCROLL_RAW_TICKS_PER_STEP;
        *out_delta = steps;
    }
    if (out_button_pressed) {
        *out_button_pressed = pressed;
    }
    return ESP_OK;
}

void scroll_debug_dump_registers(uint8_t start_reg, uint8_t length)
{
    if (!s_bus_ready) {
        ESP_LOGW(TAG, "debug dump: バス未初期化(scroll_init()が呼ばれていない)");
        return;
    }

    uint8_t buf[64];
    if (length > sizeof(buf)) {
        length = sizeof(buf);
    }
    esp_err_t err = scroll_bus_read_reg(start_reg, buf, length);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "debug dump (reg 0x%02X, len %d) failed: %s", start_reg, length, esp_err_to_name(err));
        return;
    }

    char line[196];
    int pos = snprintf(line, sizeof(line), "reg 0x%02X: ", start_reg);
    for (uint8_t i = 0; i < length && pos < (int)sizeof(line) - 3; i++) {
        pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", buf[i]);
    }
    ESP_LOGI(TAG, "%s", line);
}
