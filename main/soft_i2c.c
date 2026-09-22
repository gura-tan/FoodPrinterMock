#include "soft_i2c.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "soft_i2c";
static bool s_initialized;

/* オープンドレイン: level=1は「離す(プルアップでHigh)」、0は「Lowに引く」。
 * GPIO_MODE_INPUT_OUTPUT_ODなので、gpio_get_level()で自分が引いた結果や
 * スレーブが引いた結果(ワイヤードAND)をそのまま読める。 */
static inline void scl_low(void)          { gpio_set_level(SOFT_I2C_SCL_GPIO, 0); }
static inline void scl_high_nowait(void)  { gpio_set_level(SOFT_I2C_SCL_GPIO, 1); }
static inline void sda_low(void)          { gpio_set_level(SOFT_I2C_SDA_GPIO, 0); }
static inline void sda_high(void)         { gpio_set_level(SOFT_I2C_SDA_GPIO, 1); }
static inline void sda_write(int level)   { gpio_set_level(SOFT_I2C_SDA_GPIO, level ? 1 : 0); }
static inline int  sda_read(void)         { return gpio_get_level(SOFT_I2C_SDA_GPIO); }
static inline int  scl_read(void)         { return gpio_get_level(SOFT_I2C_SCL_GPIO); }
static inline void delay_half(void)       { esp_rom_delay_us(SOFT_I2C_HALF_PERIOD_US); }

/* SCLを離し、実際にHighになるまで待つ(クロックストレッチ対応)。
 * esp_timerに依存しないよう、1us待つループの回数で上限を数えている。 */
static esp_err_t scl_release(void)
{
    scl_high_nowait();
    for (int waited = 0; scl_read() == 0; waited++) {
        if (waited >= SOFT_I2C_STRETCH_TIMEOUT_US) {
            return ESP_ERR_TIMEOUT;
        }
        esp_rom_delay_us(1);
    }
    return ESP_OK;
}

/* START条件(SCLがHighの間にSDAが立ち下がる)。リピートスタートも同じ関数で
 * 出せる(呼び出し時点でSCLがLowでも、まずSDAを離してからSCLを上げ直す)。
 * 戻ったときSCL=Low。 */
static esp_err_t send_start(void)
{
    sda_high();
    delay_half();
    esp_err_t err = scl_release();
    if (err != ESP_OK) {
        return err;
    }
    delay_half();
    sda_low();
    delay_half();
    scl_low();
    return ESP_OK;
}

/* STOP条件(SCLがHighの間にSDAが立ち上がる)。呼び出し時点でSCL=Lowであること。 */
static esp_err_t send_stop(void)
{
    sda_low();
    delay_half();
    esp_err_t err = scl_release();
    delay_half();
    sda_high();
    delay_half();
    return err;
}

/* 1バイト送信+ACKビット読み取り。*out_ackedはACKならtrue。
 * 呼び出し時点でSCL=Low、戻ったときもSCL=Low。 */
static esp_err_t write_byte(uint8_t byte, bool *out_acked)
{
    esp_err_t err;
    for (int bit = 7; bit >= 0; bit--) {
        sda_write((byte >> bit) & 1);
        delay_half();
        err = scl_release();
        if (err != ESP_OK) {
            return err;
        }
        delay_half();
        scl_low();
    }

    /* ACKビット: SDAを離し、スレーブがLowに引くのをSCL=Highの間に読む */
    sda_high();
    delay_half();
    err = scl_release();
    if (err != ESP_OK) {
        return err;
    }
    delay_half();
    *out_acked = (sda_read() == 0);
    scl_low();
    return ESP_OK;
}

/* 1バイト受信+ACK/NACK送出(ack=trueでACK=SDAをLowに引く、falseでNACK)。
 * 呼び出し時点でSCL=Low、戻ったときもSCL=Low。 */
static esp_err_t read_byte(bool ack, uint8_t *out)
{
    esp_err_t err;
    uint8_t value = 0;

    sda_high(); // 受信中はSDAを離す
    for (int bit = 0; bit < 8; bit++) {
        delay_half();
        err = scl_release();
        if (err != ESP_OK) {
            return err;
        }
        delay_half();
        value = (uint8_t)((value << 1) | (sda_read() ? 1 : 0));
        scl_low();
    }

    sda_write(ack ? 0 : 1);
    delay_half();
    err = scl_release();
    if (err != ESP_OK) {
        return err;
    }
    delay_half();
    scl_low();
    sda_high();

    *out = value;
    return ESP_OK;
}

/* 失敗後のバスクリア。スレーブが送信途中でSDAをLowに引いたままになっている
 * 場合、SCLを最大9発叩けば残りのビットを吐き出させて解放させられる。
 * SDAが既にHighなら(アドレスNACKなど、スレーブが引いていない失敗の場合)
 * 叩く必要が無いので、SDAが離れた時点で打ち切る(未接続のポーリングで毎回
 * 9発叩いて時間を浪費しないため)。最後にSTOPを出してバスをアイドルに戻す。
 * SCLがスレーブに固定されている場合はここでは解決できない(放置して戻る)。 */
static void recover_bus(void)
{
    scl_low();
    sda_high();
    delay_half();
    for (int i = 0; i < 9 && sda_read() == 0; i++) {
        scl_high_nowait();
        delay_half();
        scl_low();
        delay_half();
    }
    sda_low();
    delay_half();
    scl_high_nowait();
    delay_half();
    sda_high();
    delay_half();
}

esp_err_t soft_i2c_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << SOFT_I2C_SCL_GPIO) | (1ULL << SOFT_I2C_SDA_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config(SCL=%d, SDA=%d) failed: %s",
                 SOFT_I2C_SCL_GPIO, SOFT_I2C_SDA_GPIO, esp_err_to_name(err));
        return err;
    }

    scl_high_nowait();
    sda_high();
    s_initialized = true;

    if (sda_read() == 0) {
        ESP_LOGW(TAG, "SDA(GPIO%d)がLowのままなのでバスクリアを試みます(スレーブが送信途中の可能性)",
                 SOFT_I2C_SDA_GPIO);
        recover_bus();
    }

    ESP_LOGI(TAG, "soft I2C initialized (SCL=GPIO%d, SDA=GPIO%d, half period=%dus)",
             SOFT_I2C_SCL_GPIO, SOFT_I2C_SDA_GPIO, SOFT_I2C_HALF_PERIOD_US);
    return ESP_OK;
}

esp_err_t soft_i2c_write_read(uint8_t addr7, uint8_t reg, uint8_t *buf, size_t len)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err;
    bool acked = false;

    err = send_start();
    if (err != ESP_OK) {
        goto fail;
    }

    err = write_byte((uint8_t)((addr7 << 1) | 0), &acked);
    if (err != ESP_OK) {
        goto fail;
    }
    if (!acked) {
        err = ESP_ERR_NOT_FOUND; // 未接続・電源なし・アドレス違いは多くの場合ここに来る
        goto fail;
    }

    err = write_byte(reg, &acked);
    if (err != ESP_OK) {
        goto fail;
    }
    if (!acked) {
        err = ESP_FAIL;
        goto fail;
    }

    err = send_start(); // リピートスタート
    if (err != ESP_OK) {
        goto fail;
    }

    err = write_byte((uint8_t)((addr7 << 1) | 1), &acked);
    if (err != ESP_OK) {
        goto fail;
    }
    if (!acked) {
        err = ESP_FAIL;
        goto fail;
    }

    for (size_t i = 0; i < len; i++) {
        err = read_byte(i + 1 < len, &buf[i]); // 最後の1バイトだけNACK
        if (err != ESP_OK) {
            goto fail;
        }
    }

    err = send_stop();
    if (err != ESP_OK) {
        goto fail;
    }
    return ESP_OK;

fail:
    recover_bus();
    return err;
}
