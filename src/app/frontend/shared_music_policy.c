#include "shared_music_policy.h"
#include "capture.h"
#include "shared_audio.h"
#include "shared_register.h"
#include "save_private.h"
#include "qa/audio_save.h"
#include "qa/application_startup_prepare.h"
#include "qa/source_save.h"
#include "qa/text.h"
#include <math.h>
#include <stdio.h>

enum { MUSIC_LIMIT = 4095 };
typedef struct music_source {
    qa_catalog *catalog;
    qa_product_id product, fallback_product;
    qa_vfs *files, *fallback_files;
    qa_audio_bank *bank, *fallback_bank;
    char **tracks;
    size_t track_count;
} music_source;
typedef struct music_state {
    char *track, *menu_track;
    size_t source, *bag, bag_count, bag_position;
    uint64_t random, completed;
    bool initialized, automatic, shuffle, looping;
} music_state;
struct frontend_music_policy {
    qa_frontend *frontend;
    qa_application *application;
    qa_audio_engine *engine;
    qa_audio_music *music;
    frontend_music_policy **slot;
    uint64_t bus;
    uint32_t audience;
    float bus_gain;
    music_source *sources;
    size_t source_count;
    char *authored_cue;
    music_state state;
    frontend_shared_music *selection;
    bool menu, busy, restoring, restore_attached, external_player;
};
struct frontend_shared_music {
    frontend_music_policy *policy;
    qa_frontend *frontend;
    qa_application *application;
    const qa_launch_snapshot *candidate;
    const qa_cvars_edit *edit;
    qa_console *root_console;
    qa_cvars *root_cvars;
    qa_command_context root_command;
    const frontend_shared_audio *audio;
    qa_audio_engine_gains *gains;
    qa_audio_music_selection *playback;
    music_state next;
    char *shuffle_text, *menu_text;
    float music_gain;
    bool ready;
};
static bool fail(qa_error *e, const char *text) { return frontend_fail(e, QA_ERROR_ARGUMENT, text); }
static char *copy(const char *text, qa_error *e) {
    if (!text) return NULL;
    size_t n = strlen(text) + 1; char *result = malloc(n);
    if (!result) { frontend_fail(e, QA_ERROR_MEMORY, "Retaining music declaration text"); return NULL; }
    memcpy(result, text, n); return result;
}
static void state_free(music_state *state) {
    free(state->track); free(state->menu_track); free(state->bag); *state = (music_state){0};
}
static bool state_copy(const music_state *source, music_state *out, qa_error *e) {
    *out = *source; out->track = out->menu_track = NULL; out->bag = NULL;
    if (source->track && !(out->track = copy(source->track, e))) goto failed;
    if (source->menu_track && !(out->menu_track = copy(source->menu_track, e))) goto failed;
    if (source->bag_count) {
        out->bag = malloc(source->bag_count * sizeof(*out->bag));
        if (!out->bag) { frontend_fail(e, QA_ERROR_MEMORY, "Retaining music shuffle bag"); goto failed; }
        memcpy(out->bag, source->bag, source->bag_count * sizeof(*out->bag));
    }
    return true;
failed: state_free(out); return false;
}
static const qa_product *product(const music_source *source) {
    return qa_catalog_product(source->catalog, source->product);
}
static qa_audio_family family(const music_source *source) {
    const qa_product *row = product(source);
    return row->family == QA_GAME_Q1 ? QA_AUDIO_Q1 : row->family == QA_GAME_Q2 ? QA_AUDIO_Q2 : QA_AUDIO_Q3;
}
static bool whitespace(const char *);
static char *file_cue(const char *, qa_error *);
static bool print(frontend_music_policy *, const qa_command_invocation *, const char *, qa_error *);
static bool counterpart(const qa_product *row, const qa_product *other) {
    static const char *const pairs[3][2] = {
        {"q1-classic-id1", "q1-rerelease-id1"}, {"q1-classic-hipnotic", "q1-rerelease-hipnotic"},
        {"q1-classic-rogue", "q1-rerelease-rogue"}
    };
    if (!row || !other) return false;
    for (size_t i = 0; i < 3; ++i) if ((!strcmp(row->key, pairs[i][0]) && !strcmp(other->key, pairs[i][1])) ||
        (!strcmp(row->key, pairs[i][1]) && !strcmp(other->key, pairs[i][0]))) return true;
    return false;
}
static bool parent_current(const frontend_music_policy *owner) {
    return owner && !owner->restoring && owner->frontend && owner->slot && *owner->slot == owner &&
        owner->frontend->application == owner->application && owner->frontend->audio == owner->engine &&
        owner->music && (!qa_audio_engine_bus_music(owner->engine, owner->bus) ||
            qa_audio_engine_bus_music(owner->engine, owner->bus) == owner->music);
}
bool frontend_music_policy_idle(const frontend_music_policy *owner) {
    return !owner || (!owner->busy && !owner->selection && parent_current(owner) && qa_audio_music_idle(owner->music));
}
bool frontend_music_policy_binding_is(const frontend_music_policy *owner, const qa_frontend *f,
    const qa_audio_engine *engine, uint64_t bus, bool menu) {
    return owner && f && owner->frontend == f && owner->application == f->application && owner->engine == engine &&
        engine == f->audio && owner->slot && *owner->slot == owner && owner->bus == bus && owner->menu == menu &&
        (owner->restoring ? f->source_restoring : parent_current(owner));
}
qa_audio_music *frontend_music_policy_player(const frontend_music_policy *owner) {
    if (!owner || !owner->slot || *owner->slot != owner || owner->frontend->application != owner->application ||
        owner->frontend->audio != owner->engine) return NULL;
    if (owner->restoring) return owner->music ? owner->music :
        owner->restore_attached ? qa_audio_engine_bus_music(owner->engine, owner->bus) : NULL;
    return parent_current(owner) ? owner->music : NULL;
}
static bool attach_player(frontend_music_policy *owner, qa_error *e) {
    if (!parent_current(owner) || !qa_audio_music_idle(owner->music) || owner->frontend->capture || owner->frontend->source_restoring)
        return fail(e, "Music output requires its actual returned retained policy/player");
    qa_audio_music *attached = qa_audio_engine_bus_music(owner->engine, owner->bus);
    if (attached) return attached == owner->music;
    if (!qa_audio_music_retain(owner->music, e)) return false;
    if (qa_audio_engine_music_source(owner->engine, owner->bus, owner->audience, owner->bus_gain, owner->music,
        owner->menu ? QA_AUDIO_MUSIC_MENU : QA_AUDIO_MUSIC_WORLD, false, e)) return true;
    qa_audio_music_release(owner->music); return false;
}
bool frontend_music_policy_attach(frontend_music_policy *owner, qa_error *e) {
    return frontend_music_policy_idle(owner) ? attach_player(owner, e) :
        fail(e, "Music attachment still retains a policy operation or selection");
}
static void source_free(music_source *source) {
    for (size_t i = 0; i < source->track_count; ++i) free(source->tracks[i]);
    free(source->tracks); qa_audio_bank_destroy(source->bank); qa_audio_bank_destroy(source->fallback_bank);
    qa_vfs_destroy(source->files); qa_vfs_destroy(source->fallback_files); qa_catalog_release(source->catalog);
    *source = (music_source){0};
}
static void release(frontend_music_policy *owner) {
    for (size_t i = 0; owner->sources && i < owner->source_count; ++i) source_free(owner->sources + i);
    free(owner->sources); free(owner->authored_cue); state_free(&owner->state);
    qa_audio_music_release(owner->music); free(owner);
}
static bool track_valid(const char *path) {
    size_t at = 0, units = 0; uint32_t point, first = 0, last = 0;
    qa_bytes bytes = {(const uint8_t *)path, strlen(path)};
    while (qa_utf8_next(bytes, &at, &point)) {
        if (!units) first = point;
        last = point;
        units += point > 0xffff ? 2 : 1;
        if (units > 255 || point < 32 || point == 127 || point == '"' || point == '\\') return false;
    }
    if (!units || qa_unicode_whitespace(first) || qa_unicode_whitespace(last)) return false;
    qa_error ignored = {0}; char *normalized = qa_vfs_normalize_path(path, &ignored);
    bool valid = normalized && !strcmp(normalized, path); free(normalized); return valid;
}
static bool add_name(char ***names, size_t *count, const char *name, qa_error *e) {
    for (size_t i = 0; i < *count; ++i) if (!strcmp((*names)[i], name)) return true;
    if (*count == MUSIC_LIMIT) return true;
    char *value = copy(name, e); if (!value) return false;
    char **next = realloc(*names, (*count + 1) * sizeof(**names));
    if (!next) { free(value); return frontend_fail(e, QA_ERROR_MEMORY, "Retaining mounted music names"); }
    *names = next; next[(*count)++] = value; return true;
}
typedef struct music_text { qa_bytes bytes; size_t at; uint32_t pending; } music_text;
static bool text_unit(music_text *text, uint32_t *unit) {
    if (text->pending) { *unit = text->pending; text->pending = 0; return true; }
    uint32_t point; if (!qa_utf8_next(text->bytes, &text->at, &point)) return false;
    if (point > 0xffff) { point -= 0x10000; *unit = 0xd800 + (point >> 10); text->pending = 0xdc00 + (point & 1023); }
    else *unit = point;
    return true;
}
/* Array.sort compares UTF-16 code units, including astral filenames. */
static int name_compare(const void *a, const void *b) {
    const char *left = *(char *const *)a, *right = *(char *const *)b;
    music_text x = {{(const uint8_t *)left, strlen(left)}, 0, 0}, y = {{(const uint8_t *)right, strlen(right)}, 0, 0};
    uint32_t u = 0, v = 0; bool has_x, has_y;
    do { has_x = text_unit(&x, &u); has_y = text_unit(&y, &v);
        if (!has_x || !has_y) return has_x ? 1 : has_y ? -1 : 0;
        if (u != v) return u < v ? -1 : 1;
    } while (has_x && has_y);
    return 0;
}
static char *trimmed(const char *value, qa_error *e) {
    qa_bytes bytes = {(const uint8_t *)value, strlen(value)}; size_t at = 0, first = bytes.size, end = 0; uint32_t point;
    while (at < bytes.size) { size_t before = at; if (!qa_utf8_next(bytes, &at, &point)) break;
        if (!qa_unicode_whitespace(point)) { if (first == bytes.size) first = before; end = at; } }
    if (first == bytes.size) return copy("", e);
    char *out = malloc(end - first + 1); if (!out) { frontend_fail(e, QA_ERROR_MEMORY, "Retaining authored WORLD music cue"); return NULL; }
    memcpy(out, value + first, end - first); out[end - first] = 0; return out;
}
static bool list_tracks(music_source *source, qa_error *e) {
    char **directories = NULL; size_t count = 0;
    bool ok = add_name(&directories, &count, "music", e);
    for (size_t i = 0; ok && i < count && source->track_count < MUSIC_LIMIT; ++i) {
        const char *directory = directories[i]; if (strlen(directory) >= 256) continue;
        const char *extensions[] = {".ogg", ".wav", "/"};
        size_t depth = 1; for (const char *p = directory; *p; ++p) if (*p == '/') ++depth;
        for (size_t ext = 0; ok && ext < 3; ++ext) {
            if (ext == 2 && depth >= 16) continue;
            qa_vfs_listing listing = {0}; ok = qa_vfs_list(source->files, directory, extensions[ext], &listing, e);
            for (size_t j = 0; ok && j < listing.count; ++j) {
                const char *name = listing.names[j]; size_t length = strlen(name);
                if (ext == 2) while (length && (name[length - 1] == '/' || name[length - 1] == '\\')) --length;
                if (!length) continue;
                size_t prefix = strlen(directory); char *path = malloc(prefix + length + 2);
                if (!path) { ok = frontend_fail(e, QA_ERROR_MEMORY, "Retaining mounted music path"); break; }
                memcpy(path, directory, prefix); path[prefix] = '/'; memcpy(path + prefix + 1, name, length);
                path[prefix + length + 1] = 0;
                if (ext == 2) ok = add_name(&directories, &count, path, e);
                else if (track_valid(path)) ok = add_name(&source->tracks, &source->track_count, path, e);
                free(path);
                if (ext < 2 && source->track_count == MUSIC_LIMIT) break;
            }
            qa_vfs_listing_free(&listing);
        }
    }
    for (size_t i = 0; i < count; ++i) free(directories[i]);
    free(directories);
    if (ok && source->track_count) qsort(source->tracks, source->track_count, sizeof(*source->tracks), name_compare);
    return ok;
}
static bool source_create(const frontend_music_content *receipt, music_source *out, bool enumerate, qa_error *e) {
    const qa_product *row = receipt && receipt->catalog ? qa_catalog_product(receipt->catalog, receipt->product) : NULL;
    if (!row || row->availability != QA_CONTENT_INSTALLED || !receipt->files ||
        (row->family != QA_GAME_Q1 && row->family != QA_GAME_Q2 && row->family != QA_GAME_Q3) ||
        (!!receipt->fallback_product != !!receipt->fallback_files)) return fail(e, "Music source requires its actual installed content receipt");
    const qa_product *fallback = receipt->fallback_product ? qa_catalog_product(receipt->catalog, receipt->fallback_product) : NULL;
    if (receipt->fallback_product && (!counterpart(row, fallback) || fallback->availability != QA_CONTENT_INSTALLED ||
        !qa_catalog_product_view_current(receipt->catalog, receipt->fallback_product, receipt->fallback_files)))
        return fail(e, "Music fallback requires its actual official opposite-edition Q1 source");
    out->catalog = receipt->catalog; qa_catalog_retain(out->catalog);
    out->product = receipt->product; out->fallback_product = receipt->fallback_product;
    out->files = qa_vfs_clone(receipt->files, e);
    if (receipt->fallback_files) out->fallback_files = qa_vfs_clone(receipt->fallback_files, e);
    return out->files && (!receipt->fallback_files || out->fallback_files) &&
        qa_audio_bank_create(out->files, &out->bank, e) &&
        (!out->fallback_files || qa_audio_bank_create(out->fallback_files, &out->fallback_bank, e)) && (!enumerate || list_tracks(out, e));
}
bool frontend_music_policy_create(qa_frontend *f, const frontend_music_policy_options *options,
    frontend_music_policy **out, qa_error *e) {
    if (!f || !f->application || !f->audio || !options || !out || *out || !options->music ||
        !options->sources || !options->source_count || options->source_count > (options->menu ? 2u : 1u) ||
        (!options->menu && !options->authored_cue) ||
        (options->external_player && options->menu) ||
        (qa_audio_engine_bus_music(f->audio, options->bus) ?
            qa_audio_engine_bus_music(f->audio, options->bus) != options->music ||
                !qa_audio_engine_music_ready(f->audio, options->bus, options->audience, options->bus_gain) : !options->external_player) ||
        !qa_audio_music_idle(options->music)) return fail(e, "Music policy requires its real constructor content and attached playback owner");
    frontend_music_policy *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(e, QA_ERROR_MEMORY, "Retaining application music policy");
    owner->frontend = f; owner->application = f->application; owner->engine = f->audio;
    if (!qa_audio_music_retain(options->music, e)) { free(owner); return false; }
    owner->music = options->music; owner->slot = out; owner->bus = options->bus;
    owner->audience = options->audience; owner->bus_gain = options->bus_gain; owner->menu = options->menu;
    owner->external_player = options->external_player;
    owner->state.random = options->random_seed; owner->source_count = options->source_count;
    owner->state.source = owner->menu ? owner->source_count - 1 : 0;
    owner->sources = calloc(owner->source_count, sizeof(*owner->sources));
    owner->authored_cue = trimmed(options->authored_cue ? options->authored_cue : "", e);
    owner->state.track = copy("", e);
    bool ok = owner->sources && owner->authored_cue && owner->state.track;
    for (size_t i = 0; ok && i < owner->source_count; ++i) {
        const frontend_music_content *receipt = options->sources + i;
        if (owner->menu && (receipt->fallback_product || receipt->fallback_files ||
            !qa_catalog_product_view_current(receipt->catalog, receipt->product, receipt->files)))
            ok = fail(e, "Menu music requires the actual independent selected product mount plan");
        if (ok) ok = source_create(receipt, owner->sources + i, !owner->external_player, e);
    }
    if (ok && owner->menu && owner->source_count == 2) {
        const qa_product *theme = product(owner->sources);
        ok = theme->family == QA_GAME_Q2 && theme->edition == QA_EDITION_RERELEASE && !strcmp(theme->campaign, "baseq2");
        if (!ok) fail(e, "Menu theme must be the actual selected Q2 rerelease base soundtrack");
    }
    if (ok) ok = qa_audio_music_profile_is(owner->music, qa_audio_engine_rate(owner->engine),
        family(owner->sources + owner->state.source), !owner->menu);
    if (!ok) { if (!e || e->code == QA_OK) frontend_fail(e, QA_ERROR_MEMORY, "Retaining application music source roster"); release(owner); return false; }
    if (owner->external_player) {
        owner->state.initialized = true;
        owner->state.completed = qa_audio_music_completions(owner->music);
    }
    *out = owner; return true;
}
bool frontend_music_policy_destroy(frontend_music_policy **in, qa_error *e) {
    if (!in || !*in) return true;
    if ((*in)->frontend->capture) return fail(e, "Music retirement cannot cross its actual frontend capture lease");
    if (!frontend_music_policy_idle(*in) && !((*in)->restoring && !(*in)->busy && !(*in)->selection && (*in)->slot == in))
        return fail(e, "Music policy still owns a playback operation or preparation");
    frontend_music_policy *owner = *in;
    if (!owner->restoring) {
        owner->busy = true; qa_audio_music_stop(owner->music); owner->busy = false;
        if (!parent_current(owner)) return fail(e, "Music retirement lost its actual retained playback parent");
    }
    *in = NULL; release(owner); return true;
}
static bool extension(const char *path) {
    size_t n = strlen(path); if (n < 4 || path[n - 4] != '.') return false;
    char suffix[4]; for (size_t i = 0; i < 3; ++i) { unsigned char c = (unsigned char)path[n - 3 + i]; suffix[i] = (char)(c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c); }
    suffix[3] = 0; return !strcmp(suffix, "ogg") || !strcmp(suffix, "wav");
}
static bool open_cue(music_source *source, const char *name, qa_audio_stream **out, qa_error *e) {
    qa_audio_bank *banks[] = {source->bank, source->fallback_bank};
    for (size_t i = 0; i < 2; ++i) {
        if (!banks[i]) continue;
        if (!qa_audio_bank_music_cue(banks[i], name, family(source), NULL, NULL, out, e)) return false;
        if (*out) break;
    }
    return true;
}
static bool menu_cue(music_source *source, const char *name, bool enabled,
    qa_audio_stream **out, char **selected, qa_error *e) {
    size_t n = strlen(name); bool prefixed = !strncmp(name, "music/", 6);
    char *path = malloc(n + 11);
    if (!path) return frontend_fail(e, QA_ERROR_MEMORY, "Resolving actual menu soundtrack path");
    snprintf(path, n + 11, "%s%s", prefixed ? "" : "music/", name);
    size_t length = strlen(path); bool explicit = extension(path), ok = true;
    const char *endings[] = {".ogg", ".wav"};
    for (size_t i = 0; ok && !*selected && i < (explicit ? 1u : 2u); ++i) {
        if (!explicit) memcpy(path + length, endings[i], 5);
        bool found = false; uint64_t bytes = 0;
        ok = qa_vfs_probe(source->files, path, &found, &bytes, e);
        if (ok && found) {
            *selected = file_cue(path, e); ok = *selected != NULL;
            if (ok && enabled) ok = qa_audio_bank_music(source->bank, path, NULL, NULL, out, e);
        }
    }
    free(path); return ok;
}
static bool number(const char *text, unsigned *out) {
    if (!text || !*text) return false;
    unsigned value = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < '0' || *p > '9' || value > 255u / 10u) return false;
        value = value * 10u + *p - '0'; if (value > 255) return false;
    }
    *out = value; return true;
}
static bool digits(const char *text) {
    if (!text || !*text) return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) if (*p < '0' || *p > '9') return false;
    return true;
}
static bool unavailable(frontend_music_policy *owner, const music_state *state,
    const qa_command_invocation *call, const char *name, qa_error *e) {
    const qa_product *row = product(owner->sources + state->source);
    size_t n = strlen(row->key) + strlen(name) + 32; char *text = malloc(n);
    if (!text) return frontend_fail(e, QA_ERROR_MEMORY, "Retaining unavailable soundtrack output");
    snprintf(text, n, "Music unavailable: %s/%s\n", row->key, name);
    bool ok;
    if (call) ok = print(owner, call, text, e);
    else { frontend_print(owner->frontend, text); ok = parent_current(owner) || fail(e, "Music output retired its actual playback/content parent"); }
    free(text); return ok;
}
static bool split_cue(const char *cue, char **intro, char **loop, qa_error *e) {
    char **outputs[] = {intro, loop}; qa_bytes bytes = {(const uint8_t *)cue, strlen(cue)};
    size_t at = 0; uint32_t point;
    for (size_t i = 0; i < 2; ++i) {
        while (at < bytes.size) { size_t next = at;
            if (!qa_utf8_next(bytes, &next, &point) || !qa_unicode_whitespace(point)) break;
            at = next;
        }
        if (at == bytes.size) break;
        bool quoted = cue[at] == '"' && strchr(cue + at + 1, '"') != NULL;
        size_t start = at; if (quoted) ++at;
        while (at < bytes.size) {
            if (quoted && cue[at] == '"') break;
            size_t next = at; if (!qa_utf8_next(bytes, &next, &point)) break;
            if (!quoted && qa_unicode_whitespace(point)) break;
            at = next;
        }
        if (quoted && at < bytes.size && cue[at] == '"') ++at;
        size_t end = at;
        if (cue[start] == '"') ++start;
        if (end > start && cue[end - 1] == '"') --end;
        size_t n = end - start; *outputs[i] = malloc(n + 1);
        if (!*outputs[i]) return frontend_fail(e, QA_ERROR_MEMORY, "Preparing authored soundtrack cue");
        memcpy(*outputs[i], cue + start, n); (*outputs[i])[n] = 0;
    }
    return true;
}
static bool prepare_track(frontend_music_policy *owner, music_state *state, const char *cue,
    bool looping, bool numbered, const qa_command_invocation *call,
    qa_audio_stream **intro, qa_audio_stream **loop, unsigned *cd, qa_error *e) {
    music_source *source = owner->sources + state->source; unsigned value;
    *cd = 0;
    if (digits(cue) && (family(source) != QA_AUDIO_Q3 || numbered) && !number(cue, &value))
        return fail(e, "Numbered soundtrack cue exceeds the actual CD track range");
    if (number(cue, &value) && (family(source) != QA_AUDIO_Q3 || numbered)) {
        if (family(source) == QA_AUDIO_Q2) {
            const qa_product *row = product(source);
            value = qa_audio_music_q2_track(value, row->campaign, row->edition == QA_EDITION_RERELEASE);
        }
        value = qa_audio_music_mapped_track(owner->music, value);
        const char *patterns[] = {"music/%02u.ogg", "music/track%02u.ogg", "music/%02u.wav", "music/track%02u.wav"};
        qa_audio_bank *banks[] = {source->bank, source->fallback_bank};
        for (size_t b = 0; value && b < 2 && !*intro; ++b) if (banks[b]) for (size_t i = 0; i < 4 && !*intro; ++i) {
            char path[32]; snprintf(path, sizeof(path), patterns[i], value);
            if (!qa_audio_bank_music(banks[b], path, NULL, NULL, intro, e)) return false;
        }
        if (*intro) { *cd = value; if (looping) *loop = *intro; }
        else if (!unavailable(owner, state, call, cue, e)) return false;
    } else {
        char *name = NULL, *repeat = NULL;
        bool ok = split_cue(cue, &name, &repeat, e);
        if (ok && name && *name) ok = open_cue(source, name, intro, e);
        if (ok && *intro && looping) {
            if (!repeat || !*repeat || !strcmp(name, repeat)) *loop = *intro;
            else ok = open_cue(source, repeat, loop, e);
        }
        if (ok && !*intro) ok = unavailable(owner, state, call, name ? name : "", e);
        free(name); free(repeat); if (!ok) return false;
    }
    char *selected = copy(cue, e); if (!selected) return false;
    free(state->track); state->track = selected; state->looping = *loop != NULL; return true;
}
/* SplitMix64 is this independent native constructor's retained random stream.
 * It never consumes a simulation clock or another subsystem's RNG. */
static double random_value(music_state *state) {
    uint64_t z = (state->random += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return (double)((z ^ (z >> 31)) >> 11) * (1.0 / 9007199254740992.0);
}
static bool matches_cue(const char *path, const char *cue) {
    if (!whitespace(path)) return !strcmp(path, cue);
    size_t n = strlen(path);
    return strlen(cue) == n + 2 && cue[0] == '"' && cue[n + 1] == '"' && !memcmp(cue + 1, path, n);
}
static char *file_cue(const char *path, qa_error *e) {
    if (!whitespace(path)) return copy(path, e);
    size_t n = strlen(path); char *cue = malloc(n + 3);
    if (!cue) { frontend_fail(e, QA_ERROR_MEMORY, "Retaining mounted soundtrack cue"); return NULL; }
    cue[0] = '"'; memcpy(cue + 1, path, n); cue[n + 1] = '"'; cue[n + 2] = 0; return cue;
}
static bool next_track(frontend_music_policy *owner, music_state *state,
    qa_audio_stream **intro, qa_audio_stream **loop, unsigned *cd, qa_error *e) {
    music_source *source = owner->sources + state->source;
    if (state->bag_position == state->bag_count) {
        free(state->bag); state->bag = NULL; state->bag_count = source->track_count; state->bag_position = 0;
        if (state->bag_count) {
            state->bag = malloc(state->bag_count * sizeof(*state->bag));
            if (!state->bag) return frontend_fail(e, QA_ERROR_MEMORY, "Preparing music shuffle bag");
        }
        for (size_t i = 0; i < state->bag_count; ++i) state->bag[i] = i;
        for (size_t i = state->bag_count; i > 1; --i) {
            size_t chosen = (size_t)(random_value(state) * (double)i), previous = state->bag[i - 1];
            state->bag[i - 1] = state->bag[chosen]; state->bag[chosen] = previous;
        }
        if (state->bag_count > 1 && matches_cue(source->tracks[state->bag[0]], state->track)) {
            size_t first = state->bag[0]; memmove(state->bag, state->bag + 1, (state->bag_count - 1) * sizeof(*state->bag));
            state->bag[state->bag_count - 1] = first;
        }
    }
    while (state->bag_position < state->bag_count && !*intro) {
        qa_audio_music_state player;
        if (!qa_audio_music_state_read(owner->music, &player)) return fail(e, "Music shuffle lost its actual shared controls/player");
        if (!player.enabled) break;
        const char *path = source->tracks[state->bag[state->bag_position++]];
        /* A mounted path is one token even when it contains spaces. */
        if (!open_cue(source, path, intro, e)) return false;
        if (!*intro && !unavailable(owner, state, NULL, path, e)) return false;
        char *track = file_cue(path, e); if (!track) return false;
        free(state->track); state->track = track; state->looping = false;
    }
    *loop = NULL; *cd = 0; return true;
}
static bool prepare_menu(frontend_music_policy *owner, music_state *state, const char *value,
    qa_audio_stream **intro, qa_audio_stream **loop, bool *changed, qa_error *e) {
    if (!frontend_shared_menu_track_valid(value)) return fail(e, "Menu music lacks its actual validated authored preference");
    if (state->menu_track && !strcmp(state->menu_track, value)) return true;
    char *retained = copy(value, e); if (!retained) return false;
    free(state->menu_track); state->menu_track = retained;
    state->automatic = false; state->shuffle = false; state->initialized = true; *changed = true;
    if (!strcmp(value, "0")) return true;
    qa_audio_music_state player;
    if (!qa_audio_music_state_read(owner->music, &player)) return fail(e, "Menu music lost its actual shared CD controls/player");
    bool resolved = false;
    for (size_t i = 0; i < owner->source_count && !resolved; ++i) {
        music_source *source = owner->sources + i; const qa_product *row = product(source);
        const char *names[2]; size_t count; char first[32], second[32]; unsigned numeric;
        if (!strcmp(value, "auto")) {
            if (owner->source_count == 2 && !i) { names[0] = "music/track77"; count = 1; }
            else if (row->family == QA_GAME_Q1) { names[0] = "music/track02"; names[1] = "music/02"; count = 2; }
            else if (row->family == QA_GAME_Q2) { names[0] = "music/02"; names[1] = "music/track02"; count = 2; }
            else { names[0] = "music/sonic5"; count = 1; }
        } else if (number(value, &numeric)) {
            snprintf(first, sizeof(first), "music/%02u", numeric); snprintf(second, sizeof(second), "music/track%02u", numeric);
            names[0] = first; names[1] = second; count = 2;
        } else { names[0] = value; count = 1; }
        for (size_t j = 0; j < count && !resolved; ++j) {
            char *track = NULL;
            if (!menu_cue(source, names[j], player.enabled, intro, &track, e)) { free(track); return false; }
            if (track) {
                resolved = true;
                if (player.enabled) {
                    free(state->track); state->track = track; track = NULL; state->looping = true; *loop = *intro;
                } else if (state->source != i) {
                    free(state->track); state->track = copy("", e); state->looping = false;
                }
                state->source = i; free(track); if (!state->track) return false;
            }
        }
    }
    if (!resolved) {
        if (strcmp(value, "auto")) {
            size_t n = strlen(value) + 32; char *text = malloc(n);
            if (!text) return frontend_fail(e, QA_ERROR_MEMORY, "Retaining unavailable menu soundtrack output");
            snprintf(text, n, "Menu music unavailable: %s\n", value); frontend_print(owner->frontend, text); free(text);
            if (!parent_current(owner)) return fail(e, "Menu music output retired its actual content/playback parent");
        }
    }
    return state->track != NULL;
}
static bool prepare_automatic(frontend_music_policy *owner, music_state *state, bool shuffle,
    qa_audio_stream **intro, qa_audio_stream **loop, unsigned *cd, bool *changed, qa_error *e) {
    qa_audio_music_state player;
    if (!qa_audio_music_state_read(owner->music, &player)) return fail(e, "Automatic music lost its actual player");
    if (!state->initialized) {
        state->initialized = true; state->automatic = *owner->authored_cue && strcmp(owner->authored_cue, "0");
        state->completed = player.completions; *changed = true;
        if (!state->automatic) return true;
    } else if (!state->automatic || !player.enabled || player.paused) return true;
    music_source *source = owner->sources + state->source;
    bool enabled = family(source) == QA_AUDIO_Q2 && shuffle && source->track_count;
    if (*changed || enabled != state->shuffle || (enabled && state->completed != player.completions)) {
        *changed = true; state->shuffle = enabled; state->completed = player.completions;
        if (!player.enabled) return enabled ? next_track(owner, state, intro, loop, cd, e) : true;
        return enabled ? next_track(owner, state, intro, loop, cd, e) :
            prepare_track(owner, state, owner->authored_cue, true, false, NULL, intro, loop, cd, e);
    }
    return true;
}
static void close_streams(qa_audio_stream *intro, qa_audio_stream *loop) {
    qa_audio_stream_close(intro); if (loop != intro) qa_audio_stream_close(loop);
}
static void selection_free(frontend_shared_music *selection) {
    state_free(&selection->next); free(selection->shuffle_text); free(selection->menu_text); free(selection);
}
static const qa_cvar_view *canonical(const qa_cvars_edit *edit, const char *name) {
    const qa_cvar_view *found = qa_cvars_edit_find(edit, name);
    for (size_t i = 0; i < qa_cvars_edit_count(edit); ++i) {
        const qa_cvar_view *row = qa_cvars_edit_at(edit, i);
        if (row == found) return row;
    }
    return NULL;
}
static bool selection_current(const frontend_shared_music *selection) {
    frontend_music_policy *owner = selection ? selection->policy : NULL;
    if (!owner || !parent_current(owner) || owner->selection != selection || owner->busy ||
        selection->frontend != owner->frontend || selection->application != owner->application ||
        owner->frontend->capture || owner->frontend->source_restoring ||
        qa_cvars_edit_registry(selection->edit) != qa_application_cvars(owner->application) ||
        !frontend_seat_callbacks_returned(owner->frontend) ||
        (!qa_application_startup_resource_phase_associated(owner->application, selection->candidate) &&
            !qa_application_startup_publication_consuming(owner->application, selection->candidate))) return false;
    qa_console *console = NULL; qa_cvars *cvars = NULL; qa_command_context command;
    if (!qa_application_startup_root_read(owner->application, selection->candidate,
            &console, &cvars, &command, NULL) || console != selection->root_console ||
        cvars != selection->root_cvars || console != qa_application_console(owner->application) ||
        cvars != qa_cvars_edit_registry(selection->edit)) return false;
    const qa_command_context *held = &selection->root_command;
    if (command.session != held->session || command.owner != held->owner || command.client != held->client ||
        command.seat != held->seat || command.dialect != held->dialect || command.origin != held->origin ||
        command.direct != held->direct || command.console_text != held->console_text || command.script != held->script ||
        command.registry != held->registry || command.generation != held->generation ||
        command.actor.registry != held->actor.registry || command.actor.generation != held->actor.generation ||
        command.actor.slot != held->actor.slot) return false;
    const qa_cvar_view *shuffle = canonical(selection->edit, "music_shuffle"), *menu = canonical(selection->edit, "music_menu_track");
    return shuffle && shuffle->value && menu && menu->value &&
        !strcmp(shuffle->value, selection->shuffle_text) && !strcmp(menu->value, selection->menu_text);
}
bool frontend_shared_music_prepare(qa_frontend *f, const qa_launch_snapshot *candidate, const qa_cvars_edit *edit,
    frontend_music_policy *owner, frontend_shared_music **out, qa_error *e) {
    if (!f || !out || *out || !edit || !owner || owner->frontend != f || !frontend_music_policy_idle(owner) ||
        f->capture || f->source_restoring || !frontend_seat_callbacks_returned(f) ||
        qa_cvars_edit_registry(edit) != qa_application_cvars(f->application) ||
        !qa_application_startup_resource_phase(f->application, candidate))
        return fail(e, "Music preparation requires its actual installed policy and returned canonical ENGINE candidate");
    qa_console *console = NULL; qa_cvars *cvars = NULL; qa_command_context command;
    if (!qa_application_startup_root_read(f->application, candidate, &console, &cvars, &command, e) ||
        console != qa_application_console(f->application) || cvars != qa_cvars_edit_registry(edit))
        return fail(e, "Music preparation lost its actual physical ENGINE startup tuple");
    const qa_cvar_view *shuffle = canonical(edit, "music_shuffle"), *menu = canonical(edit, "music_menu_track");
    if (!shuffle || !shuffle->value || !menu || !menu->value ||
        (strcmp(shuffle->value, "0") && strcmp(shuffle->value, "1"))) return fail(e, "Music preparation lacks its canonical authored setting rows");
    frontend_shared_music *selection = calloc(1, sizeof(*selection));
    if (!selection) return frontend_fail(e, QA_ERROR_MEMORY, "Retaining prepared music policy");
    selection->policy = owner; selection->frontend = f; selection->application = f->application;
    selection->candidate = candidate; selection->edit = edit;
    selection->root_console = console; selection->root_cvars = cvars; selection->root_command = command;
    selection->shuffle_text = copy(shuffle->value, e); selection->menu_text = copy(menu->value, e);
    if (!selection->shuffle_text || !selection->menu_text || !state_copy(&owner->state, &selection->next, e)) { selection_free(selection); return false; }
    owner->busy = true;
    qa_audio_stream *intro = NULL, *loop = NULL; unsigned cd = 0; bool changed = false;
    bool ok = owner->menu ? prepare_menu(owner, &selection->next, menu->value, &intro, &loop, &changed, e) :
        prepare_automatic(owner, &selection->next, !strcmp(shuffle->value, "1"), &intro, &loop, &cd, &changed, e);
    qa_audio_music_source_profile profile = {family(owner->sources + selection->next.source), !owner->menu};
    bool new_source = changed && (!owner->state.initialized || owner->state.source != selection->next.source);
    if (ok) ok = parent_current(owner) && qa_audio_music_selection_prepare(owner->music,
        !changed ? QA_AUDIO_MUSIC_KEEP : intro ? QA_AUDIO_MUSIC_START : QA_AUDIO_MUSIC_STOP,
        intro, loop, cd, new_source ? &profile : NULL, &selection->playback, e);
    owner->busy = false;
    if (!ok) { close_streams(intro, loop); selection_free(selection); return false; }
    owner->selection = selection; *out = selection; return true;
}
bool frontend_shared_music_ready(frontend_shared_music *selection, const frontend_shared_audio *audio, qa_error *e) {
    qa_audio_engine_gains *gains = frontend_shared_audio_gains(audio);
    float gain;
    if (!selection_current(selection) || !frontend_shared_audio_parent_is(audio, selection->frontend, selection->policy->engine) ||
        !gains || !qa_audio_engine_gains_ready(gains, e) ||
        !frontend_shared_audio_music_gain(audio, &gain) || !qa_cvars_edit_ready_is(selection->edit) ||
        !qa_audio_music_selection_volume(selection->playback, gain, e) || !qa_audio_music_selection_ready(selection->playback, e))
        return fail(e, "Prepared music lost its actual policy, sealed settings or retained gain/playback parents");
    if (selection->audio && (selection->audio != audio || selection->gains != gains)) return fail(e, "Prepared music cannot substitute its retained gain parent");
    selection->audio = audio; selection->gains = gains; selection->music_gain = gain; selection->ready = true; return true;
}
bool frontend_shared_music_ready_is(const frontend_shared_music *selection) {
    float gain;
    return selection && selection->ready && selection_current(selection) && qa_cvars_edit_ready_is(selection->edit) &&
        frontend_shared_audio_parent_is(selection->audio, selection->frontend, selection->policy->engine) &&
        frontend_shared_audio_gains(selection->audio) == selection->gains && qa_audio_engine_gains_ready(selection->gains, NULL) &&
        frontend_shared_audio_music_gain(selection->audio, &gain) && gain == selection->music_gain &&
        qa_audio_music_selection_ready_is(selection->playback, selection->policy->music);
}
void frontend_shared_music_publish(frontend_shared_music **in) {
    frontend_shared_music *selection = *in; frontend_music_policy *owner = selection->policy;
    qa_audio_music_selection_publish(&selection->playback);
    state_free(&owner->state); owner->state = selection->next; selection->next = (music_state){0};
    if (owner->state.initialized) owner->state.completed = qa_audio_music_completions(owner->music);
    owner->selection = NULL; selection_free(selection); *in = NULL;
}
bool frontend_shared_music_abort(frontend_shared_music **in, qa_error *e) {
    if (!in || !*in) return true;
    frontend_shared_music *selection = *in;
    if (!selection->policy || selection->policy->selection != selection || selection->policy->busy)
        return fail(e, "Music abort retains its displaced or entered policy owner");
    if (!qa_audio_music_selection_abort(&selection->playback, e)) return false;
    selection->policy->selection = NULL; selection_free(selection); *in = NULL; return true;
}
bool frontend_music_policy_update(frontend_music_policy *owner, qa_error *e) {
    if (!owner || !frontend_music_policy_idle(owner) || owner->frontend->capture || owner->frontend->source_restoring)
        return fail(e, "Music update requires its idle attached policy");
    const qa_cvar_view *shuffle = qa_cvars_find(qa_application_cvars(owner->application), "music_shuffle");
    const qa_cvar_view *menu = qa_cvars_find(qa_application_cvars(owner->application), "music_menu_track");
    if (owner->menu && (!menu || !menu->value)) return fail(e, "Menu music frame lacks its actual canonical preference");
    bool enabled = shuffle && shuffle->number != 0; music_state next = {0};
    if (!state_copy(&owner->state, &next, e)) return false;
    owner->busy = true; qa_audio_stream *intro = NULL, *loop = NULL; unsigned cd = 0; bool changed = false;
    bool ok = owner->menu ? prepare_menu(owner, &next, menu->value, &intro, &loop, &changed, e) :
        prepare_automatic(owner, &next, enabled, &intro, &loop, &cd, &changed, e);
    qa_audio_music_selection *selection = NULL;
    qa_audio_music_source_profile profile = {family(owner->sources + next.source), !owner->menu};
    bool new_source = !owner->state.initialized || owner->state.source != next.source;
    if (ok && changed) ok = parent_current(owner) && qa_audio_music_selection_prepare(owner->music,
        intro ? QA_AUDIO_MUSIC_START : QA_AUDIO_MUSIC_STOP, intro, loop, cd,
        new_source ? &profile : NULL, &selection, e) && qa_audio_music_selection_ready(selection, e);
    if (ok && changed) { qa_audio_music_selection_publish(&selection); state_free(&owner->state); owner->state = next; next = (music_state){0}; owner->state.completed = qa_audio_music_completions(owner->music); }
    else if (selection) (void)qa_audio_music_selection_abort(&selection, NULL);
    else close_streams(intro, loop);
    owner->busy = false; state_free(&next); return ok;
}
bool frontend_music_policy_random(const frontend_music_policy *owner, uint64_t *out) {
    if (!owner || !out || !frontend_music_policy_idle(owner)) return false;
    *out = owner->state.random; return true;
}
static bool command_current(const frontend_music_policy *owner, const qa_command_invocation *call) {
    return parent_current(owner) && call && call->console == qa_application_console(owner->application) &&
        qa_application_command_context_active(owner->application, &call->context);
}
static bool print(frontend_music_policy *owner, const qa_command_invocation *call, const char *text, qa_error *e) {
    frontend_console_print(owner->frontend, &call->context, text);
    return command_current(owner, call) || fail(e, "Music command output retired its actual invocation");
}
static bool nonempty(const char *text) {
    size_t at = 0; uint32_t point; qa_bytes bytes = {(const uint8_t *)text, strlen(text)};
    while (qa_utf8_next(bytes, &at, &point)) if (!qa_unicode_whitespace(point)) return true;
    return false;
}
static bool whitespace(const char *text) {
    size_t at = 0; uint32_t point; qa_bytes bytes = {(const uint8_t *)text, strlen(text)};
    while (qa_utf8_next(bytes, &at, &point)) if (qa_unicode_whitespace(point)) return true;
    return false;
}
static void manual_state(music_state *state) {
    state->initialized = true; state->automatic = state->shuffle = false;
    free(state->bag); state->bag = NULL; state->bag_count = state->bag_position = 0;
}
static bool manual_track(frontend_music_policy *owner, const qa_command_invocation *call,
    const char *cue, bool looping, bool numbered, qa_error *e) {
    manual_state(&owner->state); owner->state.initialized = true;
    qa_audio_music_state player; qa_audio_music_state_read(owner->music, &player);
    if (!player.enabled) return true;
    unsigned mapped = 0; bool numeric = number(cue, &mapped) && (family(owner->sources + owner->state.source) != QA_AUDIO_Q3 || numbered);
    if (numeric && family(owner->sources + owner->state.source) == QA_AUDIO_Q2) {
        const qa_product *row = product(owner->sources + owner->state.source);
        mapped = qa_audio_music_q2_track(mapped, row->campaign, row->edition == QA_EDITION_RERELEASE);
    }
    if (numeric) mapped = qa_audio_music_mapped_track(owner->music, mapped);
    if (player.playing && !strcmp(owner->state.track, cue) && owner->state.looping == looping &&
        (!numeric || mapped == player.cd_track)) return true;
    char *track = copy(cue, e); if (!track) return false;
    free(owner->state.track); owner->state.track = track; owner->state.looping = looping;
    qa_audio_music_stop(owner->music);
    if (!attach_player(owner, e)) return false;
    music_state next = {0}; if (!state_copy(&owner->state, &next, e)) return false;
    qa_audio_stream *intro = NULL, *loop = NULL; unsigned cd = 0;
    bool ok = prepare_track(owner, &next, cue, looping, numbered, call, &intro, &loop, &cd, e);
    qa_audio_music_selection *selection = NULL;
    if (ok) ok = command_current(owner, call) && qa_audio_music_selection_prepare(owner->music,
        intro ? QA_AUDIO_MUSIC_START : QA_AUDIO_MUSIC_STOP, intro, loop, cd, NULL, &selection, e) &&
        qa_audio_music_selection_ready(selection, e);
    if (ok && selection) qa_audio_music_selection_publish(&selection);
    else if (selection) (void)qa_audio_music_selection_abort(&selection, NULL);
    else close_streams(intro, loop);
    if (ok) { state_free(&owner->state); owner->state = next; next = (music_state){0}; }
    state_free(&next); return ok;
}
bool frontend_music_policy_manual_start(const frontend_music_policy *owner, const qa_command_invocation *call, bool *out) {
    if (!owner || !out || !command_current(owner, call) || !call->argc || !call->argv) return false;
    for (size_t i = 0; i < call->argc; ++i) if (!call->argv[i]) return false;
    *out = false;
    qa_audio_music_state player; if (!qa_audio_music_state_read(owner->music, &player)) return false;
    if (!strcmp(call->argv[0], "music")) {
        *out = player.enabled && call->argc >= 2 && call->argc <= 3 && nonempty(call->argv[1]) &&
            (call->argc != 3 || nonempty(call->argv[2])); return true;
    }
    if (strcmp(call->argv[0], "cd") || call->argc < 2) return true;
    qa_buffer lowered = {0}; qa_error ignored = {0};
    if (!qa_utf8_lower((qa_bytes){(const uint8_t *)call->argv[1], strlen(call->argv[1])}, &lowered, &ignored)) return false;
    const char *command = (const char *)lowered.data; unsigned track;
    *out = !strcmp(command, "off") || !strcmp(command, "stop") || !strcmp(command, "reset") ||
        (player.enabled && (!strcmp(command, "play") || !strcmp(command, "loop")) &&
            call->argc == 3 && number(call->argv[2], &track) && track);
    qa_buffer_free(&lowered); return true;
}
bool frontend_music_policy_explicit(frontend_music_policy *owner, const char *intro, const char *loop, bool looping, qa_error *e) {
    if (!frontend_music_policy_idle(owner) || !owner->external_player || !intro || !loop)
        return fail(e, "Explicit music continuation lost its actual source-owned player");
    char *track = NULL;
    if (*intro) {
        char *first = file_cue(intro, e), *second = *loop ? file_cue(loop, e) : NULL;
        if (!first || (*loop && !second)) { free(first); free(second); return false; }
        size_t size = strlen(first) + (second ? strlen(second) + 1 : 0) + 1;
        track = malloc(size);
        if (track) snprintf(track, size, "%s%s%s", first, second ? " " : "", second ? second : "");
        free(first); free(second);
        if (!track) return frontend_fail(e, QA_ERROR_MEMORY, "Retaining actual explicit music cue");
    }
    owner->state.initialized = true; manual_state(&owner->state);
    if (track) { free(owner->state.track); owner->state.track = track; owner->state.looping = looping; }
    owner->state.completed = qa_audio_music_completions(owner->music); return true;
}
bool frontend_music_policy_command(frontend_music_policy *owner, const qa_command_invocation *call, qa_error *e) {
    if (!owner || !frontend_music_policy_idle(owner) || !call || !call->argv || !call->argc ||
        call->argc > 1024 || owner->frontend->capture || owner->frontend->source_restoring ||
        !command_current(owner, call)) return fail(e, "Music command requires its idle soundtrack and genuine captured invocation");
    for (size_t i = 0; i < call->argc; ++i) if (!call->argv[i]) return fail(e, "Music command has an absent source token");
    owner->busy = true; bool ok = true;
    if (!strcmp(call->argv[0], "music")) {
        if (call->argc < 2 || call->argc > 3 || !nonempty(call->argv[1]) || (call->argc == 3 && !nonempty(call->argv[2])))
            ok = print(owner, call, "music <intro> [loop]\n", e);
        else {
            size_t n = strlen(call->argv[1]) + (call->argc == 3 ? strlen(call->argv[2]) : 0);
            char *cue = malloc(n + 8);
            if (!cue) ok = frontend_fail(e, QA_ERROR_MEMORY, "Retaining explicit music command cue");
            else { bool first = whitespace(call->argv[1]), second = call->argc == 3 && whitespace(call->argv[2]);
                snprintf(cue, n + 8, "%s%s%s%s%s%s%s", first ? "\"" : "", call->argv[1], first ? "\"" : "",
                    call->argc == 3 ? " " : "", second ? "\"" : "", call->argc == 3 ? call->argv[2] : "", second ? "\"" : "");
                ok = manual_track(owner, call, cue, call->argc == 3, false, e); free(cue); }
        }
    } else if (!strcmp(call->argv[0], "cd") && call->argc > 1) {
        qa_buffer lowered = {0};
        if (!qa_utf8_lower((qa_bytes){(const uint8_t *)call->argv[1], strlen(call->argv[1])}, &lowered, e)) {
            owner->busy = false; return false;
        }
        const char *command = (const char *)lowered.data; qa_audio_music_state player; qa_audio_music_state_read(owner->music, &player);
        if (!strcmp(command, "close") || !strcmp(command, "eject")) {
            char text[128]; snprintf(text, sizeof(text), "cd %s: disc tray operations are unavailable with file-backed music.\n", command);
            ok = print(owner, call, text, e);
        } else if (!strcmp(command, "on")) qa_audio_music_enable(owner->music, true);
        else if (!strcmp(command, "off") || !strcmp(command, "stop") || !strcmp(command, "reset")) {
            manual_state(&owner->state);
            if (!strcmp(command, "reset")) qa_audio_music_reset(owner->music);
            else { qa_audio_music_stop(owner->music); if (!strcmp(command, "off")) qa_audio_music_enable(owner->music, false); }
        } else if (!strcmp(command, "pause")) qa_audio_music_pause(owner->music, true);
        else if (!strcmp(command, "resume")) { if (player.enabled) qa_audio_music_pause(owner->music, false); }
        else if (!strcmp(command, "play") || !strcmp(command, "loop")) {
            unsigned track;
            if (call->argc != 3 || !number(call->argv[2], &track) || !track) {
                char text[64]; snprintf(text, sizeof(text), "cd %s <track 1..255>\n", command); ok = print(owner, call, text, e);
            } else ok = manual_track(owner, call, call->argv[2], !strcmp(command, "loop"), true, e);
        } else if (!strcmp(command, "remap")) {
            if (call->argc == 2) {
                for (unsigned i = 1; ok && i < 100; ++i) {
                    unsigned mapped = qa_audio_music_mapped_track(owner->music, i);
                    if (mapped != i) { char text[64]; snprintf(text, sizeof(text), "  %u -> %u\n", i, mapped); ok = print(owner, call, text, e); }
                }
            } else {
                uint8_t values[99]; bool valid = call->argc - 2 <= 99;
                for (size_t i = 2; valid && i < call->argc; ++i) { unsigned value; valid = number(call->argv[i], &value); if (valid) values[i - 2] = (uint8_t)value; }
                ok = valid ? qa_audio_music_remap(owner->music, values, call->argc - 2, e) :
                    print(owner, call, "cd remap requires at most 99 track numbers from 0 through 255.\n", e);
            }
        } else if (!strcmp(command, "info")) {
            if (!player.enabled) ok = print(owner, call, "CD music is disabled.\n", e);
            else if (!player.playing) ok = print(owner, call, "Not playing.\n", e);
            else {
                size_t n = strlen(owner->state.track) + 128; char *text = malloc(n);
                if (!text) ok = frontend_fail(e, QA_ERROR_MEMORY, "Retaining music status output");
                else { char mapped[48] = {0}, number_text[32]; snprintf(number_text, sizeof(number_text), "%u", player.cd_track);
                    if (player.cd_track && strcmp(owner->state.track, number_text)) snprintf(mapped, sizeof(mapped), " (mapped to %u)", player.cd_track);
                    snprintf(text, n, "%s %s track %s%s\n", player.paused ? "Paused" : "Currently", owner->state.looping ? "looping" : "playing", owner->state.track, mapped);
                    ok = print(owner, call, text, e); free(text); }
            }
            if (ok) { char value[32], text[64]; ok = qa_format_ecmascript_number(player.target_volume, value, e);
                if (ok) { snprintf(text, sizeof(text), "Volume is %s\n", value); ok = print(owner, call, text, e); } }
        } else { size_t n = strlen(command) + 32; char *text = malloc(n);
            if (!text) ok = frontend_fail(e, QA_ERROR_MEMORY, "Retaining unknown CD command output");
            else { snprintf(text, n, "Unknown cd command: %s.\n", command); ok = print(owner, call, text, e); free(text); } }
        qa_buffer_free(&lowered);
    } else if (strcmp(call->argv[0], "cd")) ok = fail(e, "That invocation is not an application music command");
    owner->busy = false; return ok && command_current(owner, call);
}
size_t frontend_music_policy_bank_count(const frontend_music_policy *owner) {
    size_t count = 0; if (owner) for (size_t i = 0; i < owner->source_count; ++i) count += owner->sources[i].fallback_bank ? 2u : 1u;
    return count;
}
qa_audio_bank *frontend_music_policy_bank_at(const frontend_music_policy *owner, size_t ordinal) {
    if (owner) for (size_t i = 0; i < owner->source_count; ++i) {
        if (!ordinal--) return owner->sources[i].bank;
        if (owner->sources[i].fallback_bank && !ordinal--) return owner->sources[i].fallback_bank;
    }
    return NULL;
}
static const char *merged_track(const frontend_music_policy *owner, size_t ordinal, size_t *total) {
    if (!owner) { if (total) *total = 0; return NULL; }
    size_t positions[2] = {0}, count = 0; const char *result = NULL;
    while (positions[0] < owner->sources[0].track_count ||
        (owner->source_count == 2 && positions[1] < owner->sources[1].track_count)) {
        const char *left = positions[0] < owner->sources[0].track_count ? owner->sources[0].tracks[positions[0]] : NULL;
        const char *right = owner->source_count == 2 && positions[1] < owner->sources[1].track_count ? owner->sources[1].tracks[positions[1]] : NULL;
        int comparison = left && right ? name_compare(&left, &right) : left ? -1 : 1;
        const char *next = comparison <= 0 ? left : right;
        if (comparison <= 0) ++positions[0];
        if (comparison >= 0) ++positions[1];
        if (count++ == ordinal) result = next;
    }
    if (total) *total = count;
    return result;
}
size_t frontend_music_policy_track_count(const frontend_music_policy *owner) {
    size_t count; (void)merged_track(owner, SIZE_MAX, &count); return count;
}
const char *frontend_music_policy_track_at(const frontend_music_policy *owner, size_t ordinal) {
    return merged_track(owner, ordinal, NULL);
}
bool frontend_music_policy_content_visit(const frontend_music_policy *owner,
    const qa_application_content_visitor *visitor, qa_error *e) {
    if (!owner) return true;
    if (!frontend_music_policy_idle(owner) || !visitor || !visitor->catalog || !visitor->pool || !visitor->view)
        return fail(e, "Music content inventory requires its genuine idle policy lease");
    frontend_music_policy *held = (frontend_music_policy *)owner;
    held->busy = true; bool ok = true;
    for (size_t i = 0; ok && i < owner->source_count; ++i) {
        const music_source *source = owner->sources + i;
        ok = visitor->catalog(visitor->context, source->catalog, e) && parent_current(owner) &&
            visitor->pool(visitor->context, qa_vfs_resources(source->files), e) && parent_current(owner) &&
            visitor->view(visitor->context, source->files, e) && parent_current(owner);
        if (ok && source->fallback_files) ok =
            visitor->pool(visitor->context, qa_vfs_resources(source->fallback_files), e) && parent_current(owner) &&
            visitor->view(visitor->context, source->fallback_files, e) && parent_current(owner);
    }
    held->busy = false;
    return ok || fail(e, "Music content visitor retired its actual policy parent");
}
void frontend_music_policy_rebind(frontend_music_policy *owner, qa_frontend *f, frontend_music_policy **slot) {
    owner->frontend = f; owner->application = f->application; owner->engine = f->audio; owner->slot = slot;
}
static bool bytes_field(qa_source_save_io *io, qa_bytes *bytes) {
    size_t size = bytes->size;
    if (!qa_source_save_count(io, &size, io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, (void *)bytes->data, size);
    if (size > io->input.size - io->offset) return false;
    *bytes = (qa_bytes){io->input.data + io->offset, size}; io->offset += size; return true;
}
static bool source_fields(qa_source_save_io *io, qa_application_content_graph *graph, music_source *source) {
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint64_t catalog = reading ? 0 : qa_application_content_catalog_id(graph, source->catalog);
    uint64_t files = reading ? 0 : qa_application_content_view_id(graph, source->files);
    uint64_t fallback = reading ? 0 : qa_application_content_view_id(graph, source->fallback_files);
    char *key = reading ? NULL : (char *)product(source)->key;
    const qa_product *alternate = !reading && source->fallback_product ? qa_catalog_product(source->catalog, source->fallback_product) : NULL;
    char *fallback_key = alternate ? (char *)alternate->key : NULL;
    bool ok = qa_source_save_u64(io, &catalog) && catalog && qa_source_save_u64(io, &files) && files &&
        qa_source_save_u64(io, &fallback) && frontend_save_text(io, &key) && key &&
        frontend_save_text(io, &fallback_key) && (!!fallback == !!fallback_key);
    if (reading && ok) {
        ok = qa_application_content_retain_catalog(graph, catalog, &source->catalog, io->error) &&
            qa_application_content_claim_view(graph, files, &source->files, io->error) &&
            (!fallback || qa_application_content_claim_view(graph, fallback, &source->fallback_files, io->error));
        const qa_product *row = ok ? qa_catalog_find(source->catalog, key) : NULL;
        const qa_product *other = ok && fallback_key ? qa_catalog_find(source->catalog, fallback_key) : NULL;
        ok = ok && row && row->availability == QA_CONTENT_INSTALLED &&
            (row->family == QA_GAME_Q1 || row->family == QA_GAME_Q2 || row->family == QA_GAME_Q3) &&
            (!fallback_key || (counterpart(row, other) && other->availability == QA_CONTENT_INSTALLED &&
                qa_catalog_product_view_current(source->catalog, other->id, source->fallback_files)));
        if (ok) { source->product = row->id; source->fallback_product = other ? other->id : 0; }
    }
    if (reading) { free(key); free(fallback_key); }
    size_t count = source->track_count;
    if (ok) ok = qa_source_save_count(io, &count, MUSIC_LIMIT);
    if (reading && ok && count) {
        source->tracks = calloc(count, sizeof(*source->tracks));
        if (!source->tracks) ok = frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring mounted music roster");
    }
    if (reading) source->track_count = source->tracks ? count : 0;
    for (size_t i = 0; ok && i < count; ++i) {
        ok = frontend_save_text(io, source->tracks + i) && source->tracks[i] && track_valid(source->tracks[i]) && extension(source->tracks[i]) &&
            (!i || name_compare(source->tracks + i - 1, source->tracks + i) < 0);
    }
    if (reading && ok) ok = qa_audio_bank_create(source->files, &source->bank, io->error) &&
        (!source->fallback_files || qa_audio_bank_create(source->fallback_files, &source->fallback_bank, io->error));
    return ok;
}
static bool state_valid(const frontend_music_policy *owner) {
    const music_state *state = &owner->state;
    if ((owner->external_player && (owner->menu || owner->source_count != 1 || owner->sources[0].track_count ||
            state->automatic || state->shuffle || state->bag_count || !state->initialized)) ||
        !state->track || state->source >= owner->source_count || state->bag_position > state->bag_count ||
        (state->bag_count && (!state->bag || state->bag_count != owner->sources[state->source].track_count)) ||
        (owner->menu && (state->automatic || state->shuffle || state->bag_count ||
            (state->initialized && !frontend_shared_menu_track_valid(state->menu_track)))) ||
        (!owner->menu && state->menu_track) ||
        (state->shuffle && (!state->automatic || family(owner->sources + state->source) != QA_AUDIO_Q2 || !owner->sources[state->source].track_count)) ||
        (!state->initialized && (state->automatic || state->shuffle || state->looping || *state->track || state->bag_count || state->completed))) return false;
    for (size_t i = 0; i < state->bag_count; ++i) {
        if (state->bag[i] >= owner->sources[state->source].track_count) return false;
        for (size_t j = 0; j < i; ++j) if (state->bag[j] == state->bag[i]) return false;
    }
    return true;
}
static bool fields(qa_source_save_io *io, qa_application_content_graph *graph, const qa_audio_checkpoint_refs *refs,
    frontend_music_policy *owner) {
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[4] = {'Q', 'F', 'M', 'P'}; uint32_t version = 3; qa_buffer encoded = {0}; qa_bytes bus = {0};
    bool ok = qa_source_save_bytes(io, magic, 4) && !memcmp(magic, "QFMP", 4) && qa_source_save_u32(io, &version) && version == 3 &&
        qa_source_save_bool(io, &owner->external_player) && qa_source_save_bool(io, &owner->menu) &&
        (!owner->external_player || !owner->menu) && qa_source_save_u32(io, &owner->audience) && qa_source_save_f32(io, &owner->bus_gain) &&
        isfinite(owner->bus_gain) && owner->bus_gain >= 0;
    if (ok && !reading) {
        ok = refs && refs->encode && refs->encode(refs->context, QA_AUDIO_REFERENCE_BUS, owner->bus, &encoded, io->error);
        bus = (qa_bytes){encoded.data, encoded.size};
    }
    if (ok) ok = bytes_field(io, &bus) && bus.size;
    if (ok && reading) ok = refs && refs->decode && refs->decode(refs->context, QA_AUDIO_REFERENCE_BUS, bus, &owner->bus, io->error);
    qa_buffer_free(&encoded);
    bool attached = !reading && qa_audio_engine_bus_music(owner->engine, owner->bus) == owner->music;
    qa_bytes private_player = {0};
    if (ok) ok = qa_source_save_bool(io, &attached);
    if (ok && !reading && !attached && !owner->external_player) {
        ok = qa_audio_music_checkpoint(owner->music, &encoded, io->error);
        private_player = (qa_bytes){encoded.data, encoded.size};
    }
    if (ok) ok = bytes_field(io, &private_player) && (!!private_player.size == (!attached && !owner->external_player));
    if (ok && reading) {
        owner->restore_attached = attached;
        if (!attached && !owner->external_player) ok = qa_audio_music_restore(private_player, &owner->music, io->error);
    }
    qa_buffer_free(&encoded);
    if (ok) ok = frontend_save_text(io, &owner->authored_cue) && owner->authored_cue &&
        qa_source_save_count(io, &owner->source_count, owner->menu ? 2 : 1) && owner->source_count;
    if (reading && ok) {
        owner->sources = calloc(owner->source_count, sizeof(*owner->sources));
        if (!owner->sources) ok = frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring actual music sources");
    }
    for (size_t i = 0; ok && i < owner->source_count; ++i) ok = source_fields(io, graph, owner->sources + i);
    if (ok && owner->menu) for (size_t i = 0; ok && i < owner->source_count; ++i)
        ok = !owner->sources[i].fallback_product && qa_catalog_product_view_current(owner->sources[i].catalog,
            owner->sources[i].product, owner->sources[i].files);
    if (ok && owner->menu && owner->source_count == 2) {
        const qa_product *theme = product(owner->sources);
        ok = theme->family == QA_GAME_Q2 && theme->edition == QA_EDITION_RERELEASE && !strcmp(theme->campaign, "baseq2");
    }
    music_state *state = &owner->state;
    if (ok) ok = frontend_save_text(io, &state->track) && frontend_save_text(io, &state->menu_track) &&
        qa_source_save_count(io, &state->source, owner->source_count - 1) && qa_source_save_u64(io, &state->random) &&
        qa_source_save_u64(io, &state->completed) && qa_source_save_bool(io, &state->initialized) &&
        qa_source_save_bool(io, &state->automatic) && qa_source_save_bool(io, &state->shuffle) && qa_source_save_bool(io, &state->looping) &&
        qa_source_save_count(io, &state->bag_count, MUSIC_LIMIT) && qa_source_save_count(io, &state->bag_position, state->bag_count);
    if (reading && ok && state->bag_count) {
        state->bag = calloc(state->bag_count, sizeof(*state->bag));
        if (!state->bag) ok = frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring music shuffle continuation");
    }
    for (size_t i = 0; ok && i < state->bag_count; ++i) ok = qa_source_save_count(io, state->bag + i, MUSIC_LIMIT - 1);
    return ok && state_valid(owner);
}
bool frontend_music_policy_checkpoint(const frontend_music_policy *owner, const qa_application_content_graph *graph,
    const qa_audio_checkpoint_refs *refs, qa_buffer *out, qa_error *e) {
    if (!owner || !graph || graph != qa_application_content_graph_read(owner->application) || !out || out->data || out->size ||
        !owner->frontend->capture || !frontend_music_policy_idle(owner) || !state_valid(owner))
        return fail(e, "Music capture requires its actual content/audio lease and idle installed continuation");
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, e) && fields(&io, (qa_application_content_graph *)graph, refs, (frontend_music_policy *)owner) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok || frontend_fail(e, QA_ERROR_FORMAT, "Music continuation leaves its actual source/content/audio graph");
}
bool frontend_music_policy_restore_prepare(qa_frontend *f, qa_application_content_graph *graph,
    const qa_audio_checkpoint_refs *refs, qa_bytes bytes, frontend_music_policy **out, qa_error *e) {
    if (!f || !f->application || !f->audio || !f->source_restoring || f->capture || !graph ||
        graph != qa_application_content_graph_read(f->application) || !out || *out)
        return fail(e, "Music import requires its actual isolated frontend/content candidate");
    frontend_music_policy *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(e, QA_ERROR_MEMORY, "Restoring application music continuation");
    owner->frontend = f; owner->application = f->application; owner->engine = f->audio; owner->slot = out; owner->restoring = true;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, e) && fields(&io, graph, refs, owner) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) { release(owner); return frontend_fail(e, QA_ERROR_FORMAT, "Invalid saved music source/playlist continuation"); }
    *out = owner; return true;
}
bool frontend_music_policy_restore_finish(frontend_music_policy *owner, qa_error *e) {
    if (!owner || !owner->restoring || owner->busy || owner->selection || !owner->slot || *owner->slot != owner ||
        owner->frontend->application != owner->application || owner->frontend->audio != owner->engine)
        return fail(e, "Music import lost its actual prepared frontend/audio candidate");
    qa_audio_music *attached = qa_audio_engine_bus_music(owner->engine, owner->bus);
    qa_audio_music *music = owner->external_player ? owner->music : owner->restore_attached ? attached : owner->music;
    qa_audio_music_state player;
    if ((owner->restore_attached ? !qa_audio_engine_music_ready(owner->engine, owner->bus, owner->audience, owner->bus_gain) : attached != NULL) ||
        !qa_audio_music_idle(music) || !qa_audio_music_state_read(music, &player) ||
        player.family != family(owner->sources + owner->state.source) || player.source_volume != !owner->menu ||
        owner->state.completed > player.completions || !state_valid(owner))
        return fail(e, "Saved music metadata does not own its actual restored playback profile and bus");
    if (owner->restore_attached && !owner->external_player && !qa_audio_music_retain(music, e)) return false;
    owner->music = music; owner->restoring = false; return true;
}
bool frontend_music_policy_restore_player(frontend_music_policy *owner, qa_audio_music *music, qa_error *e) {
    if (!owner || !owner->restoring || !owner->external_player || owner->music || !owner->frontend->source_restoring ||
        !qa_audio_music_idle(music) || (owner->restore_attached && qa_audio_engine_bus_music(owner->engine, owner->bus) != music))
        return fail(e, "Restored music alias does not own its genuine source player");
    if (!qa_audio_music_retain(music, e)) return false;
    owner->music = music; return true;
}
bool frontend_music_policy_source_is(const frontend_music_policy *owner, const frontend_music_content *source) {
    return owner && owner->external_player && source && owner->source_count == 1 &&
        owner->sources[0].catalog == source->catalog && owner->sources[0].product == source->product &&
        !owner->sources[0].fallback_product && qa_vfs_lookup_equal(owner->sources[0].files, source->files);
}
bool frontend_music_policy_world_is(const frontend_music_policy *owner, qa_catalog *catalog,
    qa_product_id product, const qa_vfs *files, qa_product_id fallback_product) {
    if (!owner || owner->menu || owner->external_player || owner->source_count != 1 || !files) return false;
    const music_source *source = owner->sources;
    return source->catalog == catalog && source->product == product &&
        source->fallback_product == fallback_product && qa_vfs_lookup_equal(source->files, files) &&
        (!fallback_product || qa_catalog_product_view_current(catalog, fallback_product, source->fallback_files));
}
