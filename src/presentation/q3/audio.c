#include "internal.h"

qa_audio_asset *q3p_sound(const qa_q3_presentation_assets *a, int32_t handle)
{
    return handle == 0 ? a->options.zero_sound :
        handle > 0 && (size_t)handle <= a->sound_count ? a->sounds[handle - 1] : NULL;
}

bool qa_q3_register_sound(qa_q3_presentation_assets *a, const char *path, bool compressed,
                            int32_t *out, qa_error *error)
{
    if (!a || a->busy || a->retired || !path || !out)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 sound registration");
    if (!*path || *path == '*') { *out = 0; return true; }
    q3p_name *prior = q3p_find_name(a, Q3P_SOUND, path);
    if (prior) { *out = prior->handle; return true; }
    if (!a->options.sounds) return q3p_fail(error, QA_ERROR_UNSUPPORTED, "Q3 sound bank is unavailable");
    ++a->busy;
    qa_audio_asset *asset = NULL;
    bool ok = qa_audio_bank_register(a->options.sounds, path, QA_GAME_Q3, &asset, error);
    int32_t handle = 0;
    if (ok && asset && qa_audio_asset_sample(asset) != qa_audio_asset_sample(a->options.zero_sound)) {
        for (size_t i = 0; i < a->sound_count; ++i)
            if (qa_audio_asset_sample(a->sounds[i]) == qa_audio_asset_sample(asset)) { handle = (int32_t)i + 1; break; }
        if (!handle) {
            ok = a->sound_count < INT32_MAX && q3p_reserve((void **)&a->sounds, &a->sound_capacity,
                a->sound_count + 1, sizeof(*a->sounds), error);
            if (!ok && (!error || error->code == QA_OK)) q3p_fail(error, QA_ERROR_MEMORY, "Q3 sound handle capacity exceeded");
            if (ok) { a->sounds[a->sound_count++] = asset; asset = NULL; handle = (int32_t)a->sound_count; }
        }
    }
    if (ok) ok = q3p_add_name(a, Q3P_SOUND, path, handle, compressed, error);
    if (ok) *out = handle;
    qa_audio_asset_release(asset); --a->busy; return ok;
}

static bool sound_valid(qa_q3_presentation *p, int32_t handle)
{
    bool valid = handle >= 0 && (size_t)handle <= p->options.assets->sound_count;
    if (!valid && p->options.print) p->options.print(p->options.context, "^3");
    return valid;
}

bool qa_q3_presentation_sound_valid(qa_q3_presentation *p, int32_t handle)
{
    qa_error ignored = {0};
    if (!q3p_begin(p, &ignored)) return false;
    return q3p_end(p, sound_valid(p, handle));
}

static bool actor(qa_q3_presentation *p, int32_t source, uint64_t *out, qa_error *error)
{
    return p->options.audio_actor ? p->options.audio_actor(p->options.context, source, out, error) :
        q3p_fail(error, QA_ERROR_UNSUPPORTED, "Q3 source audio identity service is unavailable");
}

static qa_audio_play sound(qa_q3_presentation *p, qa_audio_asset *asset)
{
    qa_resource *resource = qa_audio_asset_resource(asset);
    return (qa_audio_play){.sample = qa_audio_asset_sample(asset), .asset = asset,
        .resource_id = resource ? qa_resource_id(resource) : 0,
        .name = qa_audio_asset_name(asset), .family = QA_GAME_Q3,
        .actor = QA_AUDIO_NO_ACTOR, .origin_actor = QA_AUDIO_NO_ACTOR,
        .owner = p->options.owner, .audience = p->options.seat, .volume = 1, .attenuation = 1};
}

bool qa_q3_presentation_sound(qa_q3_presentation *p, int32_t handle, const qa_vec3 *origin,
                              int32_t entity, int32_t channel, bool local, qa_error *error)
{
    if (!local && !origin && (entity < 0 || entity > 1024))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "S_StartSound: bad entitynum");
    if (!q3p_begin(p, error)) return false;
    if (!sound_valid(p, handle)) return q3p_end(p, true);
    qa_audio_asset *asset = q3p_sound(p->options.assets, handle);
    if (!asset) return q3p_end(p, true);
    bool ok = p->options.audio && p->options.milliseconds;
    if (!ok) q3p_fail(error, QA_ERROR_UNSUPPORTED, "Q3 shared audio output or clock is unavailable");
    qa_audio_play play = sound(p, asset); play.channel = channel;
    if (local) { play.origin_kind = QA_AUDIO_LOCAL; play.attenuation = 0; }
    else {
        if (ok && entity >= 0) ok = actor(p, entity, &play.actor, error);
        play.origin_kind = origin ? QA_AUDIO_FIXED : QA_AUDIO_ACTOR;
        if (origin) play.origin = *origin; else play.origin_actor = play.actor;
    }
    if (ok) ok = qa_audio_engine_q3_submit(p->options.audio,
        &(qa_audio_q3_operation){.kind = QA_AUDIO_Q3_PLAY, .sound = play,
            .milliseconds = p->options.milliseconds(p->options.context)}, error);
    return q3p_end(p, ok);
}

bool qa_q3_presentation_loop(qa_q3_presentation *p, int32_t handle, int32_t entity,
                             qa_vec3 origin, qa_vec3 velocity, bool persistent, qa_error *error)
{
    if (!q3p_begin(p, error)) return false;
    if (!sound_valid(p, handle)) return q3p_end(p, true);
    qa_audio_asset *asset = q3p_sound(p->options.assets, handle);
    if (!asset) return q3p_end(p, true);
    bool ok = p->options.audio && p->options.frame_number;
    if (!ok) q3p_fail(error, QA_ERROR_UNSUPPORTED, "Q3 shared loop output or frame clock is unavailable");
    qa_audio_loop loop = {.sound = sound(p, asset), .velocity = velocity, .persistent = persistent};
    loop.sound.origin_kind = QA_AUDIO_FIXED; loop.sound.origin = origin;
    if (ok) ok = actor(p, entity, &loop.sound.actor, error);
    if (ok) {
        loop.frame_number = p->options.frame_number(p->options.context);
        ok = qa_audio_engine_q3_submit(p->options.audio,
            &(qa_audio_q3_operation){.kind = QA_AUDIO_Q3_LOOP, .sound = loop.sound,
                .velocity = loop.velocity, .frame_number = loop.frame_number,
                .persistent = loop.persistent}, error);
    }
    return q3p_end(p, ok);
}

bool qa_q3_presentation_clear_loops(qa_q3_presentation *p, bool all, qa_error *error)
{
    if (!q3p_begin(p, error)) return false;
    bool ok = p->options.audio ? qa_audio_engine_q3_submit(p->options.audio,
        &(qa_audio_q3_operation){.kind = QA_AUDIO_Q3_CLEAR, .all = all,
            .sound = {.family = QA_GAME_Q3, .owner = p->options.owner,
                .actor = QA_AUDIO_NO_ACTOR, .audience = p->options.seat}}, error) :
        q3p_fail(error, QA_ERROR_UNSUPPORTED, "Q3 shared audio output is unavailable");
    return q3p_end(p, ok);
}

bool qa_q3_presentation_stop_loop(qa_q3_presentation *p, int32_t entity, qa_error *error)
{
    if (!q3p_begin(p, error)) return false;
    uint64_t id;
    bool ok = p->options.audio ? actor(p, entity, &id, error) :
        q3p_fail(error, QA_ERROR_UNSUPPORTED, "Q3 shared audio output is unavailable");
    if (ok) ok = qa_audio_engine_q3_submit(p->options.audio,
        &(qa_audio_q3_operation){.kind = QA_AUDIO_Q3_STOP,
            .sound = {.family = QA_GAME_Q3, .actor = id, .owner = p->options.owner,
                .audience = p->options.seat}}, error);
    return q3p_end(p, ok);
}

bool qa_q3_presentation_sound_position(qa_q3_presentation *p, int32_t entity,
                                        qa_vec3 origin, qa_error *error)
{
    if (!q3p_begin(p, error)) return false;
    uint64_t id;
    bool ok = actor(p, entity, &id, error);
    if (ok && !p->options.audio) ok = q3p_fail(error, QA_ERROR_UNSUPPORTED, "Q3 shared audio output is unavailable");
    if (ok) ok = qa_audio_engine_q3_submit(p->options.audio,
        &(qa_audio_q3_operation){.kind = QA_AUDIO_Q3_POSITION,
            .sound = {.family = QA_GAME_Q3, .actor = id, .owner = p->options.owner,
                .audience = p->options.seat, .origin = origin}}, error);
    return q3p_end(p, ok);
}

bool qa_q3_presentation_listener(qa_q3_presentation *p, int32_t entity, qa_vec3 origin,
                                 const qa_vec3 axes[3], qa_error *error)
{
    if (!axes || !q3p_begin(p, error)) return false;
    qa_audio_listener listener = {.seat = p->options.seat, .actor = QA_AUDIO_NO_ACTOR,
        .origin = origin, .axis = {axes[0], axes[1], axes[2]}, .gain = 1};
    bool ok = actor(p, entity, &listener.actor, error);
    if (ok)
        ok = p->options.listener ? p->options.listener(p->options.context, &listener, error) :
            q3p_fail(error, QA_ERROR_UNSUPPORTED, "Q3 listener contribution owner is unavailable");
    return q3p_end(p, ok);
}

bool qa_q3_presentation_music(qa_q3_presentation *p, const char *intro,
                              const char *loop, qa_error *error)
{
    if (!q3p_begin(p, error)) return false;
    bool ok = p->options.music ? p->options.music(p->options.context, intro ? intro : "", loop ? loop : "", error) :
        q3p_fail(error, QA_ERROR_UNSUPPORTED, "selected background music owner is unavailable");
    return q3p_end(p, ok);
}
