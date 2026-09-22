/*
 * main/sound_hooks.h と同じAPI(sound_hooks_init/play/play_from/play_chained)を、
 * ブラウザ上のWeb Audioで実装したもの。main/sound_hooks.cは
 * esp_codec_dev/SDカードに依存するためsim側では使わず、こちらに差し替える。
 *
 * 実機はSDカードの /sdcard/sounds/<preset>/ 配下の各wavファイルを起動時に1回だけ読み込むが、
 * ブラウザはローカルファイルへ自由にアクセスできないため、ページ上の
 * 「音フォルダを選択」ボタンから <input type=file webkitdirectory> で
 * ユーザーにPC上の任意のフォルダを選んでもらい、同名のファイルをWeb Audioで
 * デコードして保持する。
 *
 * 【入力ごとの音の区別(Unit Scroll追加に伴う変更)】
 * 実機のsound_hooks.c(main/sound_hooks.c)と同じフォールバックをJS側で
 * 再現している:
 *   MOVE_CATEGORY/MOVE_PARAM以外: 入力によらず共通の名前(hit/proceed/...)
 *   MOVE_CATEGORY (movecat系):
 *     Encoder: emovecat → 従来のmovecat
 *     Scroll : smovecat → emovecat → 従来のmovecat
 *   MOVE_PARAM (moveprm系): 上記のcat→prmに読み替えたもの
 * フォールバックの解決はJS側(resolveClipName)で行い、実際に鳴らす
 * クリップ名をplayNow()に渡す。
 */
#include "sound_hooks.h"

#if !defined(__EMSCRIPTEN__)
/* ネイティブ(SDL2デスクトップ)ビルド用の何もしないフォールバック。
 * ブラウザ向けWeb Audio実装(EM_JS)はEmscriptenでのみビルドできるため。 */
void sound_hooks_init(void) {}
void sound_hooks_play(ui_sound_id_t id) { (void)id; }
void sound_hooks_play_from(ui_sound_id_t id, input_source_t src) { (void)id; (void)src; }
void sound_hooks_play_chained(ui_sound_id_t id) { (void)id; }

#else
#include <emscripten.h>

EM_JS(void, sim_sound_js_init, (), {
    if (window.__simSound) return;

    var state = {
        ctx: null,
        buffers: {},    // クリップ名(下記allNames参照) -> AudioBuffer
        current: null,  // 再生中のAudioBufferSourceNode(即時割り込み版のみ管理)
        pending: null,  // play_chained()で予約された次のクリップ名
        // 入力によらず共通の音
        commonNames: ["hit", "proceed", "back", "deny", "ready", "done"],
        // 入力別のMoveCat/MovePrm(実機のk_slot_filenames相当)
        moveNames: ["emovecat", "emoveprm", "smovecat", "smoveprm", "movecat", "moveprm"],
    };
    state.allNames = state.commonNames.concat(state.moveNames);
    window.__simSound = state;

    function ensureCtx() {
        if (!state.ctx) {
            state.ctx = new (window.AudioContext || window.webkitAudioContext)();
        }
        if (state.ctx.state === "suspended") {
            state.ctx.resume();
        }
        return state.ctx;
    }

    // 実機のsound_hooks.c resolve_slot()と同じフォールバック順序。
    // id: "movecat"|"moveprm"(移動系)、それ以外の共通音名はそのまま返す。
    // src: "encoder"|"scroll"
    function resolveClipName(id, src) {
        if (id !== "movecat" && id !== "moveprm") {
            return id; // 共通音はフォールバックなし
        }
        var suffix = (id === "movecat") ? "movecat" : "moveprm";
        var candidates = [];
        if (src === "scroll") {
            candidates.push("s" + suffix);
        }
        candidates.push("e" + suffix);
        candidates.push(suffix); // 従来のmovecat/moveprm
        for (var i = 0; i < candidates.length; i++) {
            if (state.buffers[candidates[i]]) {
                return candidates[i];
            }
        }
        return null; // 鳴らせるクリップが無い
    }

    function playNow(name) {
        var buf = state.buffers[name];
        if (!buf) return;
        var ctx = ensureCtx();
        var src = ctx.createBufferSource();
        src.buffer = buf;
        src.connect(ctx.destination);
        src.onended = function() {
            if (state.current !== src) return;
            state.current = null;
            if (state.pending) {
                var next = state.pending;
                state.pending = null;
                playNow(next);
            }
        };
        src.start();
        state.current = src;
    }

    // id/srcはC側のui_sound_id_t/input_source_tを文字列化したもの(下記sim_sound_js_play参照)。
    window.__simSoundPlay = function(id, srcName, chained) {
        var name = resolveClipName(id, srcName);
        if (!name) {
            // 鳴らせるクリップが無くても、割り込み再生なら実機と同じく今の再生は打ち切る
            if (!chained && state.current) {
                try { state.current.stop(); } catch (e) { /* 再生完了直後などは無視 */ }
                state.current = null;
            }
            return;
        }
        if (chained) {
            if (state.current) {
                state.pending = name; // 再生が終わるまで待つ。新しい要求で上書きされる
            } else {
                playNow(name);
            }
        } else {
            state.pending = null; // 割り込み版が来たら予約中のchainedは破棄
            if (state.current) {
                try { state.current.stop(); } catch (e) { /* 再生完了直後などは無視 */ }
            }
            playNow(name);
        }
    };

    var bar = document.createElement('div');
    bar.style.cssText = 'font:13px sans-serif;padding:6px 8px;background:#eee;' +
        'border-bottom:1px solid #ccc;display:flex;align-items:center;gap:8px;';

    var label = document.createElement('span');
    label.textContent = '音: 未読み込み(hit/proceed/back/deny/ready/done.wav、' +
        'emovecat/emoveprm/smovecat/smoveprm.wav、または従来のmovecat/moveprm.wavを' +
        '含むフォルダを選んでください)';

    var btn = document.createElement('button');
    btn.textContent = '音フォルダを選択';

    var input = document.createElement('input');
    input.type = 'file';
    input.webkitdirectory = true;
    input.directory = true;
    input.multiple = true;
    input.style.display = 'none';

    btn.onclick = function() {
        ensureCtx();
        input.click();
    };

    input.onchange = function() {
        ensureCtx();
        var loaded = [];
        var files = Array.prototype.slice.call(input.files);
        var remaining = files.length;
        if (remaining === 0) {
            label.textContent = '音: 選択されたファイルがありません';
            return;
        }
        files.forEach(function(f) {
            var base = f.name.toLowerCase();
            var match = state.allNames.find(function(n) { return base === n + '.wav'; });
            var done = function() {
                remaining--;
                if (remaining === 0) {
                    label.textContent = '音: ' + loaded.length + '/' + state.allNames.length +
                        ' 読み込み済み (' + loaded.sort().join(', ') + ')';
                }
            };
            if (!match) { done(); return; }
            f.arrayBuffer().then(function(buf) {
                return state.ctx.decodeAudioData(buf);
            }).then(function(decoded) {
                state.buffers[match] = decoded;
                loaded.push(match);
                done();
            }).catch(function(e) {
                console.warn('sim sound: failed to decode', f.name, e);
                done();
            });
        });
    };

    bar.appendChild(label);
    bar.appendChild(btn);
    bar.appendChild(input);
    document.body.insertBefore(bar, document.body.firstChild);
});

/* id/srcは文字列としてJSへ渡す(フォールバック解決をJS側の名前ベースの
 * ロジックに寄せるため)。UI_SOUND_MOVE_CATEGORY/UI_SOUND_MOVE_PARAM以外は
 * srcは使われない("encoder"を渡しておけば十分)。 */
EM_JS(void, sim_sound_js_play, (const char *id, const char *src_name, int chained), {
    var name = UTF8ToString(id);
    var srcName = UTF8ToString(src_name);
    if (window.__simSoundPlay) {
        window.__simSoundPlay(name, srcName, chained);
    }
});

/* ui_sound_id_t → JS側のクリップ名(共通音はそのままのファイル名、
 * MOVE_CATEGORY/MOVE_PARAMは"movecat"/"moveprm"というidを渡し、
 * フォールバック解決はJS側のresolveClipName()に任せる)。 */
static const char *sound_id_name(ui_sound_id_t id)
{
    switch (id) {
    case UI_SOUND_HIT:           return "hit";
    case UI_SOUND_PROCEED:       return "proceed";
    case UI_SOUND_MOVE_CATEGORY: return "movecat";
    case UI_SOUND_MOVE_PARAM:    return "moveprm";
    case UI_SOUND_BACK:          return "back";
    case UI_SOUND_DENY:          return "deny";
    case UI_SOUND_READY:         return "ready";
    case UI_SOUND_DONE:          return "done";
    default:                     return "";
    }
}

static const char *input_src_name(input_source_t src)
{
    return (src == INPUT_SRC_SCROLL) ? "scroll" : "encoder";
}

void sound_hooks_init(void)
{
    sim_sound_js_init();
}

void sound_hooks_play(ui_sound_id_t id)
{
    sound_hooks_play_from(id, INPUT_SRC_ENCODER);
}

void sound_hooks_play_from(ui_sound_id_t id, input_source_t src)
{
    sim_sound_js_play(sound_id_name(id), input_src_name(src), 0);
}

void sound_hooks_play_chained(ui_sound_id_t id)
{
    sim_sound_js_play(sound_id_name(id), input_src_name(INPUT_SRC_ENCODER), 1);
}

#endif /* __EMSCRIPTEN__ */
