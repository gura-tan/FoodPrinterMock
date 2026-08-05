#include "sound_hooks.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "sound_hooks";

/*
 * main/sounds/button.wav, main/sounds/move.wav を EMBED_FILES でファームウェアに
 * 埋め込む(main/CMakeLists.txt の EMBED_FILES 参照)。
 * シンボル名はファイルパスの '/' '.' を '_' に置き換えたもの
 * (_binary_button_wav_start/_end, _binary_move_wav_start/_end) になる。
 * もし「undefined reference」でリンクエラーになる場合は、
 * `find build -name "*.o" | xargs nm 2>/dev/null | grep _wav`
 * で実際のシンボル名を確認してほしい。
 */
extern const uint8_t button_wav_start[] asm("_binary_button_wav_start");
extern const uint8_t button_wav_end[]   asm("_binary_button_wav_end");
extern const uint8_t move_wav_start[] asm("_binary_move_wav_start");
extern const uint8_t move_wav_end[]   asm("_binary_move_wav_end");

#define SOUND_TASK_STACK     4096
#define SOUND_TASK_PRIORITY  5
#define SOUND_OUT_VOLUME_PCT 90.0f  // 0.0〜100.0

/* 再生を「即座に打ち切れる」ようにするためのチャンクサイズ(ms単位)。
 * write()をこの単位に分割し、1チャンク書き終えるたびに新しい再生要求が
 * 来ていないか確認する。小さいほど割り込みの反応が速くなる代わりに
 * write()の呼び出し回数が増える。実機で聴感に応じて調整してよい。 */
#define SOUND_CHUNK_MS  10

typedef struct {
    const uint8_t *pcm_data;
    size_t         pcm_len;
    uint32_t       sample_rate;
    uint16_t       bits_per_sample;
    uint16_t       channels;
} sound_clip_t;

static sound_clip_t s_button_clip;
static bool         s_button_clip_valid;
static sound_clip_t s_move_clip;
static bool         s_move_clip_valid;

static esp_codec_dev_handle_t s_spk_dev;
static QueueHandle_t s_sound_queue;

/* ---- 最小限のWAVパーサ ----
 * RIFF/WAVEのチャンクを順に読み、"fmt " と "data" チャンクを見つける。
 * 非圧縮PCM(audio_format==1)のみ対応。WAVE_FORMAT_EXTENSIBLE等は非対応
 * なので、書き出し時は「WAV signed 16-bit PCM」のようなシンプルな形式で
 * エクスポートしてほしい。
 */
static bool parse_wav(const uint8_t *buf, size_t len, sound_clip_t *out)
{
    if (len < 12 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "not a RIFF/WAVE file (len=%u)", (unsigned)len);
        return false;
    }

    size_t pos = 12;
    bool have_fmt = false;
    bool have_data = false;
    uint16_t audio_format = 0;

    while (pos + 8 <= len) {
        const uint8_t *chunk_id = buf + pos;
        uint32_t chunk_size = buf[pos + 4] | (buf[pos + 5] << 8) |
                              (buf[pos + 6] << 16) | ((uint32_t)buf[pos + 7] << 24);
        size_t chunk_data_pos = pos + 8;

        if (chunk_data_pos + chunk_size > len) {
            ESP_LOGW(TAG, "wav chunk size looks invalid at pos=%u, stopping parse", (unsigned)pos);
            break;
        }

        if (memcmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16) {
            const uint8_t *p = buf + chunk_data_pos;
            audio_format        = p[0] | (p[1] << 8);
            out->channels       = p[2] | (p[3] << 8);
            out->sample_rate    = p[4] | (p[5] << 8) | (p[6] << 16) | ((uint32_t)p[7] << 24);
            out->bits_per_sample = p[14] | (p[15] << 8);
            have_fmt = true;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            out->pcm_data = buf + chunk_data_pos;
            out->pcm_len  = chunk_size;
            have_data = true;
        }

        pos = chunk_data_pos + chunk_size;
        if (chunk_size % 2 == 1) {
            pos++; // チャンクは2byte境界に整列される
        }
    }

    if (!have_fmt || !have_data) {
        ESP_LOGE(TAG, "wav is missing fmt/data chunk (fmt=%d data=%d)", have_fmt, have_data);
        return false;
    }
    if (audio_format != 1 /* PCM */) {
        ESP_LOGE(TAG, "unsupported wav audio_format=%u (uncompressed PCMのみ対応)", audio_format);
        return false;
    }

    ESP_LOGI(TAG, "wav parsed: %luHz, %ubit, %uch, %u bytes PCM",
             (unsigned long)out->sample_rate, out->bits_per_sample, out->channels, (unsigned)out->pcm_len);
    return true;
}

static bool ensure_codec_open(const sound_clip_t *clip)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate     = clip->sample_rate,
        .channel         = clip->channels,
        .bits_per_sample = clip->bits_per_sample,
    };

    if (s_codec_open && /* フォーマット一致チェック */ ) {
        return true;
    }
    if (s_codec_open) {
        esp_codec_dev_close(s_spk_dev);
        s_codec_open = false;
    }


    int ret = esp_codec_dev_open(s_spk_dev, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "esp_codec_dev_open failed: %d", ret);
        return false;
    }
    s_codec_open_fmt = fs;
    s_codec_open = true;
    return true;
}

static void play_clip_interruptible(const sound_clip_t *clip)
{
    if (!ensure_codec_open(clip)) {
         return;
     }
    /* pcm_dataはconstだが、esp_codec_dev_write()の引数はvoid*なので
     * キャストしている(内部で書き換えられることはない) */
    size_t bytes_per_frame = (clip->bits_per_sample / 8) * clip->channels;
    size_t chunk_frames = (clip->sample_rate * SOUND_CHUNK_MS) / 1000;
    size_t chunk_bytes = chunk_frames * bytes_per_frame;
    if (bytes_per_frame == 0 || chunk_bytes == 0) {
        chunk_bytes = clip->pcm_len;
     }

    size_t offset = 0;
    while (offset < clip->pcm_len) {
        if (uxQueueMessagesWaiting(s_sound_queue) > 0) {
            ESP_LOGD(TAG, "playback interrupted by a newer sound request");
            return;
        }
        size_t remain = clip->pcm_len - offset;
        size_t n = (remain < chunk_bytes) ? remain : chunk_bytes;
        int ret = esp_codec_dev_write(s_spk_dev, (void *)(clip->pcm_data + offset), n);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "esp_codec_dev_write failed: %d", ret);
            return;
        }
        offset += n;
    }
}

static void sound_task(void *arg)
{
    (void)arg;
    ui_sound_id_t id;

    for (;;) {
        if (xQueueReceive(s_sound_queue, &id, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        const sound_clip_t *clip = NULL;
        switch (id) {
        case UI_SOUND_BUTTON:
            if (s_button_clip_valid) {
                clip = &s_button_clip;
            }
            break;
        case UI_SOUND_MOVE:
            if (s_move_clip_valid) {
                clip = &s_move_clip;
            }
            break;
        default:
            break;
        }

        if (clip == NULL || s_spk_dev == NULL) {
            /* CONFIRM/BACK/DONEはまだ音源が無いのでログのみ(従来通り) */
            ESP_LOGD(TAG, "sound id=%d: no clip yet (stub)", id);
            continue;
        }

        play_clip_interruptible(clip);
    }
}

void sound_hooks_init(void)
{
    s_spk_dev = bsp_audio_codec_speaker_init();
    if (s_spk_dev == NULL) {
        ESP_LOGE(TAG, "bsp_audio_codec_speaker_init failed - 音は鳴りません");
    } else {
        esp_codec_dev_set_out_vol(s_spk_dev, SOUND_OUT_VOLUME_PCT);
    }

    size_t button_wav_len = (size_t)(button_wav_end - button_wav_start);
    s_button_clip_valid = parse_wav(button_wav_start, button_wav_len, &s_button_clip);
    if (!s_button_clip_valid) {
        ESP_LOGW(TAG, "button.wav の解析に失敗しました。main/sounds/button.wav を確認してください");
    }

    size_t move_wav_len = (size_t)(move_wav_end - move_wav_start);
    s_move_clip_valid = parse_wav(move_wav_start, move_wav_len, &s_move_clip);
    if (!s_move_clip_valid) {
        ESP_LOGW(TAG, "move.wav の解析に失敗しました。main/sounds/move.wav を確認してください");
    }

    /* 長さ1のキュー: xQueueOverwrite()で送るため常に「最新の1件」だけが残る */
    s_sound_queue = xQueueCreate(1, sizeof(ui_sound_id_t));

    /* 音声書き込み(I2S)はLVGLタスクとは別のコアの専用タスクで行う。
     * LVGLはスレッドセーフでないため、このタスクからlv_*系APIを直接
     * 呼ばないこと(sound_hooks_play()経由でキューに積むだけにする)。 */
    xTaskCreatePinnedToCore(sound_task, "sound_task", SOUND_TASK_STACK, NULL,
                             SOUND_TASK_PRIORITY, NULL, 0);

    ESP_LOGI(TAG, "sound_hooks_init done (spk_dev=%p, button_clip_valid=%d, move_clip_valid=%d)",
             (void *)s_spk_dev, s_button_clip_valid, s_move_clip_valid);
}

void sound_hooks_play(ui_sound_id_t id)
{
    if (s_sound_queue == NULL) {
        return;
    }
    xQueueOverwrite(s_sound_queue, &id);
}
