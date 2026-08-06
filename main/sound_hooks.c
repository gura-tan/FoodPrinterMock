#include "sound_hooks.h"
#include "sd_storage.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static const char *TAG = "sound_hooks";

/*
 * 試作0の音トリガー用フック(SDカード読み込み版)。
 *
 * これまでmain/sounds/*.wavをEMBED_FILESでファームウェアに直接埋め込んで
 * いたが、SDカードに移行した。SD上の
 *   <mount_point>/sounds/<preset>/{button,move,confirm,back,done}.wav
 * を起動時に一度だけ全部読み込み、ヒープ上のバッファに保持したまま
 * 使い回す(毎回SDから読むとファイルI/Oのレイテンシが再生の遅延・
 * 音飛びに直結するため、これまでのEMBED_FILES版と同じく「起動時に一度だけ
 * ロードしてRAM上のバッファを再生する」方式を維持している)。
 *
 * <preset>は preset.txt (1行目の文字列) から決める。無ければ"default"。
 * 実行中の切り替えはまだ実装していない(切り替えるには再起動が必要)。
 * SDカード上の配置ルールは main/SD_CARD_SOUND_SETUP.md 参照。
 *
 * 【重要】sdkconfigは CONFIG_FATFS_LFN_NONE=y (LFN無効・8.3短名のみ対応)。
 * LFNを有効化するとファイルを開くたびに長い名前用のバッファを追加で
 * 確保することになりメモリを圧迫するため、有効化はせず、SDカード上の
 * ファイル名・フォルダ名はすべて「名前部分8文字以内・拡張子3文字以内・
 * 半角英数字のみ」に収める方針にしている。実機ログで
 * "Warning: Long filenames on SD card are disabled in menuconfig!" が
 * 出るのは想定通りで問題ない(この制限内に収まっている限り実害はない)。
 * 新しい音源やプリセット名を追加するときもこの制限を守ること。
 *
 * 【要確認】現在sdkconfigではPSRAM(CONFIG_SPIRAM)が無効になっている。
 * 5クリップ分をすべて内部SRAMに保持するとヒープを圧迫する可能性があるため、
 * 同梱したsdkconfig.defaultsでPSRAMを有効化することを推奨する
 * (`idf.py fullclean && idf.py build`で反映)。
 */

#define SOUND_TASK_STACK     4096
#define SOUND_TASK_PRIORITY  5
#define SOUND_OUT_VOLUME_PCT 90.0f  // 0.0〜100.0
#define DEFAULT_PRESET_NAME  "default"
#define PRESET_NAME_MAX_LEN  48
#define PATH_MAX_LEN         160

/* 再生を「即座に打ち切れる」ようにするためのチャンクサイズ(ms単位)。
 * write()をこの単位に分割し、1チャンク書き終えるたびに新しい再生要求が
 * 来ていないか確認する。 */
#define SOUND_CHUNK_MS  10

typedef struct {
    const uint8_t *pcm_data;
    size_t         pcm_len;
    uint32_t       sample_rate;
    uint16_t       bits_per_sample;
    uint16_t       channels;
} sound_clip_t;

typedef struct {
    sound_clip_t clip;
    uint8_t     *file_buf;  // ファイル全体を保持するmalloc済みバッファ。
                             // clip.pcm_dataはこのバッファ内を指しているため、
                             // 有効な間は解放しない(プリセット切り替え機能を
                             // 実装するときに初めて解放処理が要る)
    bool         valid;
} sound_slot_t;

static const char *const k_sound_filenames[UI_SOUND_DONE + 1] = {
    [UI_SOUND_BUTTON]  = "button.wav",
    [UI_SOUND_MOVE]    = "move.wav",
    [UI_SOUND_CONFIRM] = "confirm.wav",
    [UI_SOUND_BACK]    = "back.wav",
    [UI_SOUND_DONE]    = "done.wav",
};

static sound_slot_t s_slots[UI_SOUND_DONE + 1];

static esp_codec_dev_handle_t s_spk_dev;
static QueueHandle_t s_sound_queue;

/* 直前にesp_codec_dev_open()した際のフォーマットをキャッシュしておき、
 * 同じフォーマットのクリップが連続する場合はclose/openし直さない。
 * (これが以前undefinedのまま参照されていて、ビルドが通らない状態だった箇所) */
static bool s_codec_open;
static esp_codec_dev_sample_info_t s_codec_open_fmt;

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

/* ---- SDカードからのファイル読み込み ---- */

static uint8_t *read_file_into_buffer(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "cannot open %s (%s) - このサウンドは無効になります", path, strerror(errno));
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        ESP_LOGW(TAG, "fseek(SEEK_END) failed for %s", path);
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size <= 0) {
        ESP_LOGW(TAG, "%s is empty or ftell failed", path);
        fclose(f);
        return NULL;
    }
    rewind(f);

    uint8_t *buf = malloc((size_t)size);
    if (!buf) {
        ESP_LOGE(TAG, "malloc(%ld bytes) failed for %s - "
                      "ヒープ不足の可能性(sdkconfig.defaultsでPSRAM有効化を検討)",
                 size, path);
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (read_bytes != (size_t)size) {
        ESP_LOGW(TAG, "short read on %s (%u/%ld bytes)", path, (unsigned)read_bytes, size);
        free(buf);
        return NULL;
    }

    *out_len = (size_t)size;
    return buf;
}

/* preset.txt の1行目からプリセット名を決める。
 * 無い/読み取れない場合はDEFAULT_PRESET_NAMEを使う。
 * (旧sound_preset.txtは名前部分が12文字で8.3制限を超えていたためpreset.txtに短縮した) */
static void resolve_preset_dir(char *out, size_t out_size)
{
    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/preset.txt", sd_storage_mount_point());

    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGI(TAG, "%s が見つからないため preset=\"%s\" を使用します", path, DEFAULT_PRESET_NAME);
        snprintf(out, out_size, "%s", DEFAULT_PRESET_NAME);
        return;
    }

    if (!fgets(out, (int)out_size, f)) {
        snprintf(out, out_size, "%s", DEFAULT_PRESET_NAME);
    }
    fclose(f);

    /* 改行・末尾の空白を除去 */
    size_t n = strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) {
        out[--n] = '\0';
    }
    if (n == 0) {
        snprintf(out, out_size, "%s", DEFAULT_PRESET_NAME);
    }

    ESP_LOGI(TAG, "sound preset = \"%s\"", out);
}

static void load_sound_slot(ui_sound_id_t id, const char *preset_dir)
{
    sound_slot_t *slot = &s_slots[id];
    slot->valid = false;
    slot->file_buf = NULL;

    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/sounds/%s/%s",
             sd_storage_mount_point(), preset_dir, k_sound_filenames[id]);

    size_t len = 0;
    uint8_t *buf = read_file_into_buffer(path, &len);
    if (!buf) {
        return; // 個別のログはread_file_into_buffer側で出力済み
    }

    if (!parse_wav(buf, len, &slot->clip)) {
        ESP_LOGW(TAG, "%s の解析に失敗しました", path);
        free(buf);
        return;
    }

    slot->file_buf = buf;
    slot->valid = true;
    ESP_LOGI(TAG, "loaded %s", path);
}

static void load_all_sound_slots(void)
{
    char preset_dir[PRESET_NAME_MAX_LEN];
    resolve_preset_dir(preset_dir, sizeof(preset_dir));

    for (int i = 0; i <= UI_SOUND_DONE; i++) {
        load_sound_slot((ui_sound_id_t)i, preset_dir);
    }
}

/* ---- 再生 ---- */

static bool sample_info_equal(const esp_codec_dev_sample_info_t *a, const esp_codec_dev_sample_info_t *b)
{
    return a->sample_rate == b->sample_rate &&
           a->channel == b->channel &&
           a->bits_per_sample == b->bits_per_sample;
}

static bool ensure_codec_open(const sound_clip_t *clip)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate     = clip->sample_rate,
        .channel         = clip->channels,
        .bits_per_sample = clip->bits_per_sample,
    };

    if (s_codec_open && sample_info_equal(&s_codec_open_fmt, &fs)) {
        return true; // 直前と同じフォーマットなので開き直し不要
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

        if (id < 0 || id > UI_SOUND_DONE || !s_slots[id].valid || s_spk_dev == NULL) {
            ESP_LOGD(TAG, "sound id=%d: no clip available", id);
            continue;
        }

        play_clip_interruptible(&s_slots[id].clip);
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

    if (sd_storage_mount() == ESP_OK) {
        load_all_sound_slots();
    } else {
        ESP_LOGW(TAG, "SDカードがマウントできなかったため、すべてのUI音が無効になります");
    }

    /* 長さ1のキュー: xQueueOverwrite()で送るため常に「最新の1件」だけが残る */
    s_sound_queue = xQueueCreate(1, sizeof(ui_sound_id_t));

    /* 音声書き込み(I2S)はLVGLタスクとは別のコアの専用タスクで行う。
     * LVGLはスレッドセーフでないため、このタスクからlv_*系APIを直接
     * 呼ばないこと(sound_hooks_play()経由でキューに積むだけにする)。 */
    xTaskCreatePinnedToCore(sound_task, "sound_task", SOUND_TASK_STACK, NULL,
                             SOUND_TASK_PRIORITY, NULL, 0);

    int valid_count = 0;
    for (int i = 0; i <= UI_SOUND_DONE; i++) {
        if (s_slots[i].valid) {
            valid_count++;
        }
    }
    ESP_LOGI(TAG, "sound_hooks_init done (spk_dev=%p, %d/%d clips loaded)",
             (void *)s_spk_dev, valid_count, UI_SOUND_DONE + 1);
}

void sound_hooks_play(ui_sound_id_t id)
{
    if (s_sound_queue == NULL) {
        return;
    }
    xQueueOverwrite(s_sound_queue, &id);
}
