#include "encoder_unit.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "encoder_unit";

#define ENCODER_I2C_ADDR        0x40
#define ENCODER_PORTA_SDA_GPIO  2
#define ENCODER_PORTA_SCL_GPIO  1
#define ENCODER_I2C_FREQ_HZ     100000

/* 実機で確認済みのレジスタ配置(2026/07) */
#define ENCODER_REG_COUNTER   0x10  // int16, リトルエンディアン, 2byte, 符号付き。1クリック=±2
#define ENCODER_REG_BUTTON    0x20  // 1byte, 0=押されている / 1=離されている

/* 1クリックの回転で生カウンタが±2変化する。ポーリング周期によっては
 * ±1ずつに分割されて読めることがあるため、read()のたびに端数を
 * s_raw_delta_carryに繰り越し、次回のread()と合算してからステップ数に
 * 変換する(取りこぼし・重複カウントを防ぐ)。 */
#define ENCODER_RAW_TICKS_PER_STEP 2

/* 【2026/09追記・実機確認】起動直後、AXP2101のPort.A電源レールがまだ
 * 安定していないタイミングで初回のカウンタ読み出しを行うと、
 * i2c_master_transmit_receive()がESP_ERR_INVALID_STATE等で失敗することを
 * 実機で確認した(sd_storage.cのbsp_sdcard_sdspi_mount()で見つかった
 * 電源安定待ちの問題と同種。SD_MOUNT_SETTLE_DELAY_MS/RETRY_COUNT参照)。
 * BSPのbsp_feature_tにはPort.A/B(Grove)用の電源有効化フラグが無く
 * (BSP_FEATURE_SD/LCD/TOUCH/SPEAKER/MIC/CAMERA/USBのみ)、明示的に
 * 有効化する手段が無いため、短い安定待ち+数回のリトライで自己回復させる。
 * ただし基板側の電源シーケンス自体が「USB接続だけでは電源が入らず、
 * PWRボタンを押すまでGroveの5Vが供給されない」状態の場合はリトライでも
 * 解決しない(その場合は電源投入のやり直しが必要)。 */
#define ENCODER_INIT_SETTLE_DELAY_MS  50
#define ENCODER_INIT_RETRY_COUNT       3
#define ENCODER_INIT_RETRY_DELAY_MS  150

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static int32_t s_last_counter;
static int32_t s_raw_delta_carry;
static bool s_initialized;

static esp_err_t read_counter_raw(int32_t *out_raw)
{
    uint8_t reg = ENCODER_REG_COUNTER;
    uint8_t raw[2] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, raw, sizeof(raw), 100 /*ms*/);
    if (err != ESP_OK) {
        return err;
    }
    int16_t counter16 = (int16_t)(raw[0] | (raw[1] << 8));
    *out_raw = counter16;
    return ESP_OK;
}

esp_err_t encoder_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        /* 【2026/09 変更】以前は-1(空いているポートを自動選択)だったが、
         * ハードI2Cを I2C_NUM_0 に固定した。ESP32-S3のハードI2Cは2つ
         * (I2C_NUM_0/1)だけで、BSPの内部バス(GPIO11/12)は
         * CONFIG_BSP_I2C_NUM=1 = I2C_NUM_1 を固定で使う。自動選択に任せると
         * 初期化順によってはBSPと同じコントローラを取り合う恐れがあるため、
         * 空いている側(I2C_NUM_0)を明示する。Unit Scroll(Port.B)は
         * ハードI2Cが残っていないのでソフトウェアI2C(soft_i2c.c)で動かしている。 */
        .i2c_port = I2C_NUM_0,
        .sda_io_num = ENCODER_PORTA_SDA_GPIO,
        .scl_io_num = ENCODER_PORTA_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ENCODER_I2C_ADDR,
        .scl_speed_hz = ENCODER_I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 起動直後のカウンタ値を基準値として読み、以降はdeltaだけを返す。
     * 電源レール安定待ち+リトライについては上のENCODER_INIT_*のコメント参照。 */
    vTaskDelay(pdMS_TO_TICKS(ENCODER_INIT_SETTLE_DELAY_MS));

    int32_t raw = 0;
    err = ESP_FAIL;
    for (int attempt = 1; attempt <= ENCODER_INIT_RETRY_COUNT; attempt++) {
        err = read_counter_raw(&raw);
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "initial counter read attempt %d/%d failed: %s",
                 attempt, ENCODER_INIT_RETRY_COUNT, esp_err_to_name(err));
        if (attempt < ENCODER_INIT_RETRY_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(ENCODER_INIT_RETRY_DELAY_MS));
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "initial counter read failed after %d attempts: %s "
                      "(未接続、または電源レールがまだ安定していない可能性。PWRボタンでの電源再投入も試してほしい)",
                 ENCODER_INIT_RETRY_COUNT, esp_err_to_name(err));
        return err;
    }
    s_last_counter = raw;
    s_raw_delta_carry = 0;
    s_initialized = true;
    ESP_LOGI(TAG, "encoder_unit initialized, initial raw counter=%ld", (long)s_last_counter);
    return ESP_OK;
}

esp_err_t encoder_poll(int32_t *out_delta, bool *out_button_pressed)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    int32_t raw = 0;
    esp_err_t err = read_counter_raw(&raw);
    if (err != ESP_OK) {
        return err;
    }
    /* レジスタは16bitで折り返すが、素直に差分を取れば折り返し前後でも
     * 正しいdeltaになる(1ポーリングあたりの回転量が32768を超えることは
     * 現実的にありえないため) */
    int32_t raw_delta = raw - s_last_counter;
    s_last_counter = raw;

    if (out_delta) {
        int32_t total = s_raw_delta_carry + raw_delta;
        int32_t steps = total / ENCODER_RAW_TICKS_PER_STEP; // 0方向への切り捨て
        s_raw_delta_carry = total - steps * ENCODER_RAW_TICKS_PER_STEP;
        *out_delta = steps;
    }

    if (out_button_pressed) {
        uint8_t reg = ENCODER_REG_BUTTON;
        uint8_t btn = 1;
        err = i2c_master_transmit_receive(s_dev, &reg, 1, &btn, sizeof(btn), 100);
        if (err != ESP_OK) {
            return err;
        }
        *out_button_pressed = (btn == 0); // 実機確認済み: 0=押されている
    }
    return ESP_OK;
}

void encoder_debug_dump_registers(uint8_t start_reg, uint8_t length)
{
    uint8_t buf[64];
    if (length > sizeof(buf)) {
        length = sizeof(buf);
    }
    esp_err_t err = i2c_master_transmit_receive(s_dev, &start_reg, 1, buf, length, 100);
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
