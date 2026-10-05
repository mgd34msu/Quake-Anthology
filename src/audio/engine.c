#include "engine_internal.h"
#include "qa/audio_save.h"
#include "mixer_internal.h"
#include "music_internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, qa_status code, const char *message) {
    qa_error_set(error, code, 0, "%s", message);
    return false;
}
bool qa_audio_q3_operation_valid(const qa_audio_q3_operation *operation)
{
    if (!operation || (unsigned)operation->kind > QA_AUDIO_Q3_POSITION) return false;
    const qa_audio_play *sound = &operation->sound;
    if (sound->family != QA_AUDIO_Q3 || !sound->owner || sound->owner == QA_AUDIO_NO_OWNER)
        return false;
    if (operation->kind == QA_AUDIO_Q3_PLAY || operation->kind == QA_AUDIO_Q3_LOOP) {
        if (!sound->asset || sound->sample != qa_audio_asset_sample(sound->asset) ||
            sound->name != qa_audio_asset_name(sound->asset) ||
            qa_audio_asset_family(sound->asset) != QA_AUDIO_Q3 ||
            (unsigned)sound->origin_kind > QA_AUDIO_ACTOR || sound->channel < 0 ||
            !isfinite(sound->volume) || sound->volume < 0 || sound->volume > 1 ||
            !isfinite(sound->attenuation) || sound->attenuation < 0 ||
            !isfinite(sound->delay_seconds) || !isfinite(sound->server_milliseconds) ||
            !qa_vec_finite(sound->origin)) return false;
        const qa_resource *resource = qa_audio_asset_resource(sound->asset);
        if (sound->resource_id != (resource ? qa_resource_id(resource) : 0)) return false;
        return operation->kind != QA_AUDIO_Q3_LOOP || qa_vec_finite(operation->velocity);
    }
    if (sound->audience == QA_AUDIO_WORLD || sound->asset || sound->sample || sound->name)
        return false;
    return operation->kind != QA_AUDIO_Q3_POSITION ||
        (sound->actor != QA_AUDIO_NO_ACTOR && qa_vec_finite(sound->origin));
}
static void q3_discard(qa_audio_engine *engine, uint64_t owner, uint32_t audience, bool all_owners)
{
    size_t kept = 0;
    for (size_t i = 0; i < engine->q3_count; ++i) {
        qa_audio_q3_operation operation = engine->q3_operations[engine->q3_first + i];
        if ((all_owners || operation.sound.owner == owner) &&
            (audience == QA_AUDIO_WORLD || operation.sound.audience == audience))
            qa_audio_asset_release(operation.sound.asset);
        else engine->q3_operations[kept++] = operation;
    }
    engine->q3_first = 0; engine->q3_count = kept;
}
static void *grow(void *memory, size_t *capacity, size_t count, size_t size, qa_error *error) {
    if (count <= *capacity)
        return memory;
    if (count > SIZE_MAX / size) {
        fail(error, QA_ERROR_MEMORY, "Audio array size overflow");
        return NULL;
    }
    size_t next = *capacity ? *capacity : 8;
    while (next < count) {
        if (next > SIZE_MAX / 2) {
            next = count;
            break;
        }
        next *= 2;
    }
    if (next > SIZE_MAX / size)
        next = count;
    void *p = realloc(memory, next * size);
    if (!p) {
        fail(error, QA_ERROR_MEMORY, "Audio array allocation failed");
        return NULL;
    }
    *capacity = next;
    return p;
}
static audio_seat *find_seat(qa_audio_engine *engine, uint32_t seat) {
    for (size_t i = 0; i < engine->seat_count; i++)
        if (engine->seats[i]->listener.seat == seat)
            return engine->seats[i];
    return NULL;
}
static uint64_t allocate_voice(void *user) {
    qa_audio_engine *engine = user;
    return ++engine->next_voice;
}
static void observe_voice(void *user, const qa_audio_voice_event *event) {
    qa_audio_engine *engine = user;
    if (!engine->options.observer || engine->destroy_pending)
        return;
    engine->callback_depth++;
    engine->options.observer(engine->options.observer_user, event);
    engine->callback_depth--;
    if (!engine->callback_depth && !engine->operation_depth && engine->destroy_pending)
        qa_audio_engine_destroy(engine);
}
static bool enter(qa_audio_engine *engine, bool structural, qa_error *error) {
    if (!engine || engine->destroying || engine->destroy_pending || engine->round_resetting ||
        engine->acoustics_readers)
        return fail(error, QA_ERROR_ARGUMENT, "Audio engine is unavailable");
    if (structural && (engine->callback_depth || engine->operation_depth))
        return fail(error, QA_ERROR_ARGUMENT,
                    "Audio structural changes require callback completion");
    if (engine->operation_depth == UINT_MAX)
        return fail(error, QA_ERROR_ARGUMENT, "Audio callback recursion limit exceeded");
    engine->operation_depth++;
    return true;
}
static void leave(qa_audio_engine *engine) {
    if (--engine->operation_depth == 0 && engine->destroy_pending)
        qa_audio_engine_destroy(engine);
}
static void seat_destroy(audio_seat *seat) {
    if (!seat)
        return;
    qa_audio_mixer_destroy(seat->mixer);
    qa_audio_reverb_destroy(seat->reverb);
    qa_audio_environment_destroy(seat->environment);
    free(seat);
}
qa_audio_mixer_options qa_audio_engine_mixer_options(qa_audio_engine *engine) {
    return (qa_audio_mixer_options){.sample_rate = engine->options.sample_rate,
                                      .output_channels = engine->options.output_channels,
                                      .initial_voices = engine->options.initial_voices,
                                      .random = engine->options.random,
                                      .random_user = engine->options.random_user,
                                      .milliseconds = engine->options.milliseconds,
                                      .milliseconds_user = engine->options.milliseconds_user,
                                      .observer = engine->options.observer ? observe_voice : NULL,
                                      .observer_user = engine,
                                      .setting = engine->options.setting,
                                      .log = engine->options.log,
                                      .diagnostic_user = engine->options.diagnostic_user,
                                      .allocate_voice_id = allocate_voice,
                                      .voice_id_user = engine};
}
static bool seat_create(qa_audio_engine *engine, const qa_audio_listener *listener,
                        qa_audio_mixer *retained, audio_seat **out, qa_error *error) {
    audio_seat *seat = calloc(1, sizeof(*seat));
    if (!seat)
        return fail(error, QA_ERROR_MEMORY, "Audio seat allocation failed");
    seat->listener = *listener;
    seat->mixer = retained;
    qa_audio_mixer_options options = qa_audio_engine_mixer_options(engine);
    if ((!retained && !qa_audio_mixer_create(&options, &seat->mixer, error)) ||
        !qa_audio_reverb_create(engine->options.sample_rate, &seat->reverb, error))
        goto failed;
    qa_audio_mixer_geometry(seat->mixer, engine->geometry, engine->geometry_user);
    if (engine->acoustics_enabled &&
        !qa_audio_mixer_geometry_checked(seat->mixer, qa_audio_engine_acoustics_transmit,
            engine, error)) goto failed;
    qa_audio_mixer_effects_gain(seat->mixer, engine->effects_gain);
    qa_audio_mixer_doppler(seat->mixer, engine->doppler);
    if (engine->clock > INT64_MAX ||
        !qa_audio_mixer_select_time(seat->mixer, engine->clock, (int64_t)engine->clock, error))
        goto failed;
    for (size_t i = 0; i < engine->position_count; i++)
        if (!qa_audio_mixer_position(seat->mixer, engine->positions[i].actor,
                                     engine->positions[i].position, error))
            goto failed;
    if (!qa_audio_mixer_listener(seat->mixer, listener, error))
        goto failed;
    *out = seat;
    return true;
failed:
    if (retained) seat->mixer = NULL;
    seat_destroy(seat);
    return false;
}
bool qa_audio_engine_create(const qa_audio_engine_options *options, qa_audio_engine **out,
                            qa_error *error) {
    if (!options || !out || options->sample_rate < 8000 || options->sample_rate > 192000 ||
        (options->output_channels != 1 && options->output_channels != 2))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid audio engine options");
    size_t frames = options->mix_frames ? options->mix_frames : 4096;
    if (frames > SIZE_MAX / 2 / sizeof(float))
        return fail(error, QA_ERROR_ARGUMENT, "Audio mix buffer too large");
    qa_audio_engine *engine = calloc(1, sizeof(*engine));
    if (!engine)
        return fail(error, QA_ERROR_MEMORY, "Audio engine allocation failed");
    engine->options = *options;
    engine->options.mix_frames = frames;
    if (!engine->options.initial_voices)
        engine->options.initial_voices = 96;
    engine->effects_gain = 0.7f;
    engine->music_gain = 0.25f;
    engine->doppler = true;
    engine->sum = malloc(frames * 2 * sizeof(float));
    engine->seat_scratch = malloc(frames * 2 * sizeof(float));
    if (!engine->sum || !engine->seat_scratch) {
        qa_audio_engine_destroy(engine);
        return fail(error, QA_ERROR_MEMORY, "Audio scratch allocation failed");
    }
    *out = engine;
    return true;
}
void qa_audio_engine_destroy(qa_audio_engine *engine) {
    if (!engine || engine->destroying)
        return;
    if (engine->round_resetting) {
        engine->round_destroy_requested = true;
        return;
    }
    if (engine->operation_depth || engine->callback_depth || engine->acoustics_readers) {
        engine->destroy_pending = true;
        return;
    }
    engine->destroying = true;
    q3_discard(engine, 0, QA_AUDIO_WORLD, true);
    for (size_t i = 0; i < engine->seat_count; i++)
        seat_destroy(engine->seats[i]);
    for (size_t i = 0; i < engine->round_mixer_count; ++i)
        qa_audio_mixer_destroy(engine->round_mixers[i].mixer);
    for (size_t i = 0; i < engine->bus_count; i++) {
        qa_audio_raw_destroy(engine->buses[i].raw);
        qa_audio_music_destroy(engine->buses[i].music);
    }
    if (engine->acoustics.context) engine->acoustics.release(engine->acoustics.context);
    free(engine->seats);
    free(engine->round_mixers);
    free(engine->positions);
    free(engine->buses);
    free(engine->q3_operations);
    free(engine->sum);
    free(engine->seat_scratch);
    free(engine);
}
static bool listeners_impl(qa_audio_engine *engine, const qa_audio_listener *listeners,
                           size_t count, qa_error *error) {
    if (!engine || (!listeners && count) || count > SIZE_MAX / sizeof(audio_seat *))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid audio listeners");
    for (size_t i = 0; i < count; i++) {
        if (listeners[i].seat == QA_AUDIO_WORLD || !isfinite(listeners[i].gain) ||
            listeners[i].gain < 0 || !qa_vec_finite(listeners[i].origin) ||
            !qa_vec_finite(listeners[i].axis[0]) || !qa_vec_finite(listeners[i].axis[1]) ||
            !qa_vec_finite(listeners[i].axis[2]))
            return fail(error, QA_ERROR_ARGUMENT, "Invalid audio listener state");
        for (size_t j = 0; j < i; j++)
            if (listeners[i].seat == listeners[j].seat)
                return fail(error, QA_ERROR_ARGUMENT, "Duplicate audio listener seat");
    }
    bool same_order = count == engine->seat_count;
    for (size_t i = 0; same_order && i < count; i++)
        same_order = listeners[i].seat == engine->seats[i]->listener.seat;
    if (same_order) {
        for (size_t i = 0; i < count; i++) {
            if (!qa_audio_mixer_listener(engine->seats[i]->mixer, &listeners[i], error))
                return false;
            engine->seats[i]->listener = listeners[i];
            if (engine->seats[i]->environment)
                qa_audio_environment_update(engine->seats[i]->environment, listeners[i].origin,
                                            engine->milliseconds);
        }
        for (size_t i = 0; i < engine->round_mixer_count; ++i)
            qa_audio_mixer_destroy(engine->round_mixers[i].mixer);
        engine->round_mixer_count = 0;
        return true;
    }
    audio_seat **next = count ? calloc(count, sizeof(*next)) : NULL;
    bool *created = count ? calloc(count, sizeof(*created)) : NULL;
    size_t *retained = count ? malloc(count * sizeof(*retained)) : NULL;
    if (count && (!next || !created || !retained)) {
        free(next);
        free(created);
        free(retained);
        return fail(error, QA_ERROR_MEMORY, "Audio listener allocation failed");
    }
    for (size_t i = 0; i < count; i++) {
        retained[i] = SIZE_MAX;
        next[i] = find_seat(engine, listeners[i].seat);
        if (!next[i]) {
            for (size_t j = 0; j < engine->round_mixer_count; ++j)
                if (engine->round_mixers[j].seat == listeners[i].seat) { retained[i] = j; break; }
            created[i] = true;
            qa_audio_mixer *mixer = retained[i] == SIZE_MAX ? NULL : engine->round_mixers[retained[i]].mixer;
            if (!seat_create(engine, &listeners[i], mixer, &next[i], error))
                goto failed;
        }
    }
    for (size_t i = 0; i < count; i++) {
        if (!created[i] && !qa_audio_mixer_listener(next[i]->mixer, &listeners[i], error))
            goto failed;
        next[i]->listener = listeners[i];
        if (next[i]->environment)
            qa_audio_environment_update(next[i]->environment, listeners[i].origin,
                                        engine->milliseconds);
    }
    audio_seat **previous = engine->seats;
    size_t previous_count = engine->seat_count;
    engine->seats = next;
    engine->seat_count = count;
    for (size_t i = 0; i < engine->round_mixer_count; ++i) {
        bool reused = false;
        for (size_t j = 0; j < count; ++j) if (retained[j] == i) reused = true;
        if (!reused) qa_audio_mixer_destroy(engine->round_mixers[i].mixer);
    }
    engine->round_mixer_count = 0;
    for (size_t i = 0; i < previous_count; i++) {
        bool kept = false;
        for (size_t j = 0; j < count; j++)
            if (next[j] == previous[i]) {
                kept = true;
                break;
            }
        if (!kept)
            seat_destroy(previous[i]);
    }
    free(previous);
    free(created);
    free(retained);
    return true;
failed:
    for (size_t i = 0; i < count; i++)
        if (created[i]) {
            if (retained[i] != SIZE_MAX && next[i]) next[i]->mixer = NULL;
            seat_destroy(next[i]);
        }
    free(next);
    free(created);
    free(retained);
    return false;
}
static bool position_impl(qa_audio_engine *engine, uint64_t actor, qa_vec3 position,
                              qa_error *error) {
    if (!engine || engine->destroying || engine->destroy_pending || actor == QA_AUDIO_NO_ACTOR ||
        !qa_vec_finite(position))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid audio actor position");
    size_t index = 0;
    while (index < engine->position_count && engine->positions[index].actor != actor)
        index++;
    if (index == engine->position_count) {
        audio_position *next = grow(engine->positions, &engine->position_capacity, index + 1,
                                    sizeof(*engine->positions), error);
        if (!next)
            return false;
        engine->positions = next;
    }
    for (size_t i = 0; i < engine->seat_count; i++)
        if (!qa_audio_mixer_position(engine->seats[i]->mixer, actor, position, error))
            return false;
    engine->positions[index] = (audio_position){actor, position};
    if (index == engine->position_count)
        engine->position_count++;
    return true;
}
void qa_audio_engine_geometry(qa_audio_engine *engine, qa_audio_transmission_fn fn, void *user) {
    if (!engine || engine->destroying || engine->destroy_pending || engine->round_resetting ||
        engine->acoustics_readers || engine->acoustics.context || engine->acoustics_enabled)
        return;
    engine->geometry = fn;
    engine->geometry_user = user;
    for (size_t i = 0; i < engine->seat_count; i++)
        qa_audio_mixer_geometry(engine->seats[i]->mixer, fn, user);
    for (size_t i = 0; i < engine->round_mixer_count; ++i)
        qa_audio_mixer_geometry(engine->round_mixers[i].mixer, fn, user);
}
void qa_audio_engine_pause(qa_audio_engine *engine, bool paused) {
    if (engine && !engine->destroying && !engine->destroy_pending && !engine->round_resetting &&
        !engine->acoustics_readers)
        engine->paused = paused;
}
void qa_audio_engine_gain(qa_audio_engine *engine, float gain) {
    if (!engine || engine->destroying || engine->destroy_pending || engine->round_resetting ||
        engine->acoustics_readers || !isfinite(gain) || gain < 0 ||
        (double)(gain * 255.0f) > INT32_MAX)
        return;
    engine->effects_gain = gain;
    for (size_t i = 0; i < engine->seat_count; i++)
        qa_audio_mixer_effects_gain(engine->seats[i]->mixer, gain);
}
bool qa_audio_engine_gains_read(const qa_audio_engine *engine, float *effects, float *music) {
    if (!engine || engine->destroying || engine->destroy_pending || !effects || !music)
        return false;
    *effects = engine->effects_gain;
    *music = engine->music_gain;
    return true;
}
bool qa_audio_engine_music_gain(qa_audio_engine *engine, float gain, qa_error *error) {
    if (!isfinite(gain) || gain < 0)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid shared music target");
    if (!enter(engine, true, error)) return false;
    bool ready = true;
    for (size_t i = 0; i < engine->bus_count; ++i)
        if (engine->buses[i].music &&
            !qa_audio_music_target_mutation_ready(engine->buses[i].music, error))
            ready = false;
    if (!ready) { leave(engine); return false; }
    engine->music_gain = gain;
    for (size_t i = 0; i < engine->bus_count; ++i)
        if (engine->buses[i].music)
            qa_audio_music_target_publish(engine->buses[i].music, gain);
    leave(engine);
    return true;
}
void qa_audio_engine_doppler(qa_audio_engine *engine, bool enabled) {
    if (!engine || engine->destroying || engine->destroy_pending || engine->round_resetting ||
        engine->acoustics_readers)
        return;
    engine->doppler = enabled;
    for (size_t i = 0; i < engine->seat_count; i++)
        qa_audio_mixer_doppler(engine->seats[i]->mixer, enabled);
}
static bool selected(uint32_t audience, uint32_t seat) {
    return audience == QA_AUDIO_WORLD || audience == seat;
}
static bool play_impl(qa_audio_engine *engine, const qa_audio_play *sound, int32_t milliseconds,
                      qa_error *error) {
    if (!engine || !sound || !sound->sample || !isfinite(sound->volume) || sound->volume < 0 ||
        sound->volume > 1)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid audio play request");
    for (size_t i = 0; i < engine->seat_count; i++) {
        audio_seat *seat = engine->seats[i];
        if (!selected(sound->audience, seat->listener.seat))
            continue;
        qa_audio_play request = *sound;
        if (request.origin_kind == QA_AUDIO_LOCAL)
            request.actor = seat->listener.actor;
        if (request.actor == seat->listener.actor && request.actor != QA_AUDIO_NO_ACTOR)
            request.origin_kind = QA_AUDIO_LOCAL;
        bool accepted;
        if (!qa_audio_mixer_play(seat->mixer, &request, milliseconds, &accepted, error))
            return false;
    }
    return true;
}
static bool loop_impl(qa_audio_engine *engine, const qa_audio_loop *loop, qa_error *error) {
    if (!engine || engine->destroying || engine->destroy_pending || !loop)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid audio loop request");
    for (size_t i = 0; i < engine->seat_count; i++)
        if (selected(loop->sound.audience, engine->seats[i]->listener.seat) &&
            !qa_audio_mixer_loop(engine->seats[i]->mixer, loop, error))
            return false;
    return true;
}
void qa_audio_engine_clear_loops(qa_audio_engine *engine, bool all) {
    if (engine && !engine->destroying && !engine->destroy_pending && !engine->round_resetting &&
        !engine->acoustics_readers)
        for (size_t i = 0; i < engine->seat_count; i++)
            qa_audio_mixer_clear_loops(engine->seats[i]->mixer, all);
}
static bool end_loop_frame_impl(qa_audio_engine *engine, qa_error *error) {
    if (!engine || engine->destroying || engine->destroy_pending)
        return fail(error, QA_ERROR_ARGUMENT, "Missing audio engine");
    for (size_t i = 0; i < engine->seat_count; i++)
        if (!qa_audio_mixer_end_loop_frame(engine->seats[i]->mixer, error))
            return false;
    return true;
}
static void stop_channel_impl(qa_audio_engine *engine, uint64_t actor, uint64_t owner,
                              qa_audio_family family, int32_t channel) {
    if (engine)
        for (size_t i = 0; i < engine->seat_count; i++)
            qa_audio_mixer_stop_channel(engine->seats[i]->mixer, actor, owner, family, channel);
}
static bool stop_actor_impl(qa_audio_engine *engine, uint64_t actor, uint64_t owner,
                            qa_error *error) {
    if (!engine)
        return fail(error, QA_ERROR_ARGUMENT, "Missing audio engine");
    for (size_t i = 0; i < engine->seat_count; i++) {
        qa_audio_mixer_stop_actor(engine->seats[i]->mixer, actor, owner);
        if (!qa_audio_mixer_end_loop_frame(engine->seats[i]->mixer, error))
            return false;
    }
    return true;
}
static bool stop_loop_impl(qa_audio_engine *engine, uint64_t actor, uint64_t owner,
                               uint32_t audience, qa_error *error) {
    if (!engine || engine->destroying || engine->destroy_pending)
        return fail(error, QA_ERROR_ARGUMENT, "Missing audio engine");
    for (size_t i = 0; i < engine->seat_count; i++)
        if (selected(audience, engine->seats[i]->listener.seat)) {
            qa_audio_mixer_stop_loop(engine->seats[i]->mixer, actor, owner);
            if (!qa_audio_mixer_end_loop_frame(engine->seats[i]->mixer, error))
                return false;
        }
    return true;
}
static bool stop_owner_impl(qa_audio_engine *engine, uint64_t owner, uint32_t audience,
                            qa_error *error) {
    if (!engine)
        return fail(error, QA_ERROR_ARGUMENT, "Missing audio engine");
    for (size_t i = 0; i < engine->seat_count; i++)
        if (selected(audience, engine->seats[i]->listener.seat)) {
            qa_audio_mixer_stop_owner(engine->seats[i]->mixer, owner);
            if (!qa_audio_mixer_end_loop_frame(engine->seats[i]->mixer, error))
                return false;
        }
    q3_discard(engine, owner, audience, false);
    return true;
}
static void stop_all_impl(qa_audio_engine *engine, bool retain_menu) {
    if (!engine)
        return;
    q3_discard(engine, 0, QA_AUDIO_WORLD, true);
    for (size_t i = 0; i < engine->seat_count; i++) {
        audio_seat *seat = engine->seats[i];
        if (engine->round_resetting) qa_audio_mixer_round_stop(seat->mixer);
        else qa_audio_mixer_stop_all(seat->mixer);
        qa_audio_reverb_reset(seat->reverb);
        seat->underwater = (qa_audio_underwater){0};
    }
    size_t retained = 0;
    for (size_t i = 0; i < engine->bus_count; i++) {
        audio_bus bus = engine->buses[i];
        if (retain_menu && bus.music && bus.lifetime == QA_AUDIO_MUSIC_MENU) {
            engine->buses[retained++] = bus;
            continue;
        }
        qa_audio_raw_destroy(bus.raw);
        qa_audio_music_destroy(bus.music);
    }
    engine->bus_count = retained;
}
static bool attach_bus(qa_audio_engine *engine, uint64_t id, uint32_t audience, float gain,
                       qa_audio_raw_stream *raw, qa_audio_music *music,
                       qa_audio_music_lifetime lifetime, bool active, qa_error *error) {
    if (!engine || engine->destroying || engine->destroy_pending || engine->round_resetting ||
        engine->acoustics_readers || (!raw && !music) ||
        !isfinite(gain) || gain < 0 ||
        (lifetime != QA_AUDIO_MUSIC_WORLD && lifetime != QA_AUDIO_MUSIC_MENU) ||
        (raw && (lifetime != QA_AUDIO_MUSIC_WORLD || !active)))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid audio bus");
    if ((raw && qa_audio_raw_rate(raw) != engine->options.sample_rate) ||
        (music && qa_audio_music_rate(music) != engine->options.sample_rate))
        return fail(error, QA_ERROR_ARGUMENT, "Audio bus rate differs from shared mixer");
    for (size_t i = 0; i < engine->bus_count; i++)
        if (engine->buses[i].id != id &&
            ((raw && engine->buses[i].raw == raw) || (music && engine->buses[i].music == music)))
            return fail(error, QA_ERROR_ARGUMENT, "Audio bus already has an owner");
    size_t index = 0;
    while (index < engine->bus_count &&
           (engine->buses[index].id != id || (engine->buses[index].raw != NULL) != (raw != NULL)))
        index++;
    if (index == engine->bus_count) {
        audio_bus *next =
            grow(engine->buses, &engine->bus_capacity, index + 1, sizeof(*engine->buses), error);
        if (!next)
            return false;
        engine->buses = next;
    }
    if (music) qa_audio_music_target_publish(music, engine->music_gain);
    if (index < engine->bus_count) {
        if (engine->buses[index].raw != raw)
            qa_audio_raw_destroy(engine->buses[index].raw);
        if (engine->buses[index].music != music)
            qa_audio_music_destroy(engine->buses[index].music);
    }
    engine->buses[index] = (audio_bus){.id=id, .audience=audience, .gain=gain,
        .raw=raw, .music=music, .lifetime=lifetime, .active=active};
    if (index == engine->bus_count)
        engine->bus_count++;
    return true;
}
bool qa_audio_engine_stream(qa_audio_engine *engine, uint64_t id, uint32_t audience, float gain,
                            qa_audio_raw_stream *stream, qa_error *error) {
    return attach_bus(engine, id, audience, gain, stream, NULL, QA_AUDIO_MUSIC_WORLD, true, error);
}
bool qa_audio_engine_music(qa_audio_engine *engine, uint64_t id, uint32_t audience, float gain,
                           qa_audio_music *music, qa_error *error) {
    return attach_bus(engine, id, audience, gain, NULL, music, QA_AUDIO_MUSIC_WORLD, true, error);
}
bool qa_audio_engine_music_source(qa_audio_engine *engine, uint64_t id, uint32_t audience,
    float gain, qa_audio_music *music, qa_audio_music_lifetime lifetime, bool active, qa_error *error) {
    if (!engine || engine->operation_depth || engine->callback_depth)
        return fail(error, QA_ERROR_ARGUMENT, "Music source attachment requires returned engine operations");
    return attach_bus(engine, id, audience, gain, NULL, music, lifetime, active, error);
}
bool qa_audio_engine_music_output(qa_audio_engine *engine, uint64_t id,
    const qa_audio_music *music, bool active, qa_error *error) {
    if (!music) return fail(error, QA_ERROR_ARGUMENT, "Music output requires its actual player");
    if (!enter(engine, true, error)) return false;
    audio_bus *found = NULL;
    for (size_t i = 0; i < engine->bus_count; ++i)
        if (engine->buses[i].id == id && engine->buses[i].music == music) found = &engine->buses[i];
    if (found) found->active = active;
    bool changed = found != NULL;
    leave(engine);
    return changed || fail(error, QA_ERROR_NOT_FOUND, "Music output lost its actual bus player");
}
bool qa_audio_engine_music_source_is(const qa_audio_engine *engine, uint64_t id,
    const qa_audio_music *music, qa_audio_music_lifetime lifetime, bool active) {
    if (!engine || !music || engine->destroy_pending || engine->destroying ||
        engine->operation_depth || engine->callback_depth || engine->bus_count > engine->bus_capacity ||
        (engine->bus_count && !engine->buses)) return false;
    const audio_bus *found = NULL;
    for (size_t i = 0; i < engine->bus_count; ++i) {
        const audio_bus *row = &engine->buses[i];
        if (row->id != id || !row->music) continue;
        if (found || row->raw || row->music != music) return false;
        found = row;
    }
    return found && found->lifetime == lifetime && found->active == active;
}
bool qa_audio_engine_music_controls_restore_bind(qa_audio_engine *engine,
    qa_audio_music_controls *controls, qa_error *error) {
    if (!controls || !enter(engine, true, error))
        return fail(error, QA_ERROR_ARGUMENT, "Cold music controls require their returned engine owner");
    bool ready = !engine->gains && engine->bus_count <= engine->bus_capacity &&
        (!engine->bus_count || engine->buses);
    size_t pending = 0;
    for (size_t i = 0; ready && i < engine->bus_count; ++i) {
        qa_audio_music *music = engine->buses[i].music;
        if (!music) continue;
        if (engine->buses[i].raw) { ready = false; break; }
        for (size_t j = 0; j < i; ++j)
            if (engine->buses[j].music == music) ready = false;
        if (!qa_audio_music_controls_is(music, controls)) {
            if (!qa_audio_music_controls_restore_pending(music)) ready = false;
            else ++pending;
        }
    }
    ready = ready && qa_audio_music_controls_restore_ready(controls, pending);
    if (!ready) {
        leave(engine);
        return fail(error, QA_ERROR_ARGUMENT, "Cold music roster differs from its imported control owner");
    }
    bool okay = true;
    for (size_t i = 0; okay && i < engine->bus_count; ++i) {
        qa_audio_music *music = engine->buses[i].music;
        if (music && !qa_audio_music_controls_is(music, controls))
            okay = qa_audio_music_controls_bind(music, controls, error);
    }
    leave(engine);
    return okay;
}
static void remove_bus(qa_audio_engine *engine, uint64_t id, bool raw, bool music) {
    if (!engine || engine->destroying || engine->destroy_pending || engine->round_resetting ||
        engine->acoustics_readers)
        return;
    for (size_t i = 0; i < engine->bus_count;)
        if (engine->buses[i].id == id &&
            ((raw && engine->buses[i].raw) || (music && engine->buses[i].music))) {
            qa_audio_raw_destroy(engine->buses[i].raw);
            qa_audio_music_destroy(engine->buses[i].music);
            memmove(engine->buses + i, engine->buses + i + 1,
                    (engine->bus_count - i - 1) * sizeof(*engine->buses));
            engine->bus_count--;
        } else
            i++;
}
void qa_audio_engine_remove_bus(qa_audio_engine *engine, uint64_t id) {
    remove_bus(engine, id, true, true);
}
void qa_audio_engine_remove_stream(qa_audio_engine *engine, uint64_t id) {
    remove_bus(engine, id, true, false);
}
void qa_audio_engine_remove_music(qa_audio_engine *engine, uint64_t id) {
    remove_bus(engine, id, false, true);
}
static bool environment_impl(qa_audio_engine *engine, uint32_t id,
                                 qa_audio_environment *environment, qa_error *error) {
    audio_seat *seat =
        engine && !engine->destroying && !engine->destroy_pending ? find_seat(engine, id) : NULL;
    if (!seat)
        return fail(error, QA_ERROR_NOT_FOUND, "Unknown audio listener");
    if (environment)
        for (size_t i = 0; i < engine->seat_count; i++)
            if (engine->seats[i] != seat && engine->seats[i]->environment == environment)
                return fail(error, QA_ERROR_ARGUMENT, "Audio environment already has an owner");
    if (seat->environment != environment)
        qa_audio_environment_destroy(seat->environment);
    seat->environment = environment;
    qa_audio_reverb_reset(seat->reverb);
    if (environment)
        qa_audio_environment_update(environment, seat->listener.origin, engine->milliseconds);
    return true;
}
static void update_impl(qa_audio_engine *engine, double milliseconds) {
    if (!engine || engine->destroying || engine->destroy_pending || !isfinite(milliseconds))
        return;
    engine->milliseconds = milliseconds;
    for (size_t i = 0; i < engine->seat_count; i++) {
        audio_seat *seat = engine->seats[i];
        if (seat->environment)
            qa_audio_environment_update(seat->environment, seat->listener.origin, milliseconds);
    }
    if (!engine->paused)
        for (size_t i = 0; i < engine->bus_count; i++)
            if (engine->buses[i].music && engine->buses[i].active)
                qa_audio_music_update(engine->buses[i].music);
}
static float audience_gain(qa_audio_engine *engine, uint32_t audience) {
    if (audience == QA_AUDIO_WORLD)
        return 1;
    audio_seat *seat = find_seat(engine, audience);
    return seat ? seat->listener.gain : 0;
}
static bool mix_impl(qa_audio_engine *engine, int16_t *stereo, size_t frames, qa_error *error) {
    if (!engine || (!stereo && frames) || frames > SIZE_MAX / 2 / sizeof(*stereo) ||
        frames > (uint64_t)INT64_MAX - engine->clock)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid shared audio mix range");
    if (engine->paused) {
        if (frames)
            memset(stereo, 0, frames * 2 * sizeof(*stereo));
        return true;
    }
    size_t offset = 0;
    while (offset < frames) {
        size_t count = frames - offset;
        if (count > engine->options.mix_frames)
            count = engine->options.mix_frames;
        memset(engine->sum, 0, count * 2 * sizeof(float));
        for (size_t i = 0; i < engine->seat_count; i++) {
            audio_seat *seat = engine->seats[i];
            if (!qa_audio_mixer_mix_float(seat->mixer, engine->seat_scratch, count, error))
                return false;
            const qa_audio_reverb_params *params =
                seat->environment ? qa_audio_environment_params(seat->environment) : NULL;
            if (params)
                qa_audio_reverb_process(seat->reverb, engine->seat_scratch, count, params);
            if (seat->listener.underwater)
                qa_audio_underwater_process(&seat->underwater, engine->options.sample_rate,
                                            engine->seat_scratch, count, 0.25f);
            else
                seat->underwater = (qa_audio_underwater){0};
            for (size_t j = 0; j < count * 2; j++)
                engine->sum[j] += engine->seat_scratch[j] * seat->listener.gain;
        }
        for (size_t i = 0; i < engine->bus_count; i++) {
            audio_bus *bus = &engine->buses[i];
            if (!bus->active) continue;
            float gain = bus->gain * audience_gain(engine, bus->audience);
            if (bus->raw)
                qa_audio_raw_mix(bus->raw, engine->sum, count, gain);
            else {
                memset(engine->seat_scratch, 0, count * 2 * sizeof(float));
                if (!qa_audio_music_mix(bus->music, engine->seat_scratch, count, error))
                    return false;
                for (size_t j = 0; j < count * 2; j++)
                    engine->sum[j] += engine->seat_scratch[j] * gain;
            }
        }
        for (size_t i = 0; i < count * 2; i++) {
            float v = engine->sum[i];
            stereo[offset * 2 + i] = (int16_t)(isnan(v)     ? 0
                                               : v < -32768 ? -32768
                                               : v > 32767  ? 32767
                                                            : v);
        }
        offset += count;
        engine->clock += count;
    }
    return true;
}
uint64_t qa_audio_engine_clock(const qa_audio_engine *engine) { return engine ? engine->clock : 0; }
bool qa_audio_engine_observer_is(const qa_audio_engine *engine, qa_audio_voice_observer observer,
    const void *context)
{
    return engine && !engine->destroy_pending && !engine->destroying &&
        engine->options.observer == observer && engine->options.observer_user == context;
}
bool qa_audio_engine_milliseconds_is(const qa_audio_engine *engine, qa_audio_milliseconds_fn clock,
    const void *context)
{
    if (!engine || engine->destroy_pending || engine->destroying ||
        engine->options.milliseconds != clock || engine->options.milliseconds_user != context ||
        (engine->seat_count && !engine->seats) ||
        (engine->round_mixer_count && !engine->round_mixers)) return false;
    for (size_t i = 0; i < engine->seat_count; ++i) {
        const qa_audio_mixer *mixer = engine->seats[i] ? engine->seats[i]->mixer : NULL;
        if (!mixer || mixer->options.milliseconds != clock ||
            mixer->options.milliseconds_user != context) return false;
    }
    for (size_t i = 0; i < engine->round_mixer_count; ++i) {
        const qa_audio_mixer *mixer = engine->round_mixers[i].mixer;
        if (!mixer || mixer->options.milliseconds != clock ||
            mixer->options.milliseconds_user != context) return false;
    }
    return true;
}
uint32_t qa_audio_engine_rate(const qa_audio_engine *engine) {
    return engine ? engine->options.sample_rate : 0;
}
qa_audio_mixer *qa_audio_engine_seat_mixer(qa_audio_engine *engine, uint32_t seat) {
    audio_seat *state =
        engine && !engine->destroying && !engine->destroy_pending ? find_seat(engine, seat) : NULL;
    return state ? state->mixer : NULL;
}
bool qa_audio_engine_listeners(qa_audio_engine *engine, const qa_audio_listener *listeners,
                               size_t count, qa_error *error) {
    if (!enter(engine, true, error))
        return false;
    bool result = listeners_impl(engine, listeners, count, error);
    leave(engine);
    return result;
}
bool qa_audio_engine_position(qa_audio_engine *engine, uint64_t actor, qa_vec3 position,
    qa_error *error) {
    if (!enter(engine, false, error)) return false;
    bool result = position_impl(engine, actor, position, error);
    leave(engine); return result;
}
bool qa_audio_engine_loop(qa_audio_engine *engine, const qa_audio_loop *loop, qa_error *error) {
    if (!enter(engine, false, error)) return false;
    bool result = loop_impl(engine, loop, error);
    leave(engine); return result;
}
bool qa_audio_engine_q3_submit(qa_audio_engine *engine, const qa_audio_q3_operation *operation,
    qa_error *error)
{
    if (!qa_audio_q3_operation_valid(operation))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid retained Q3 audio operation");
    if (!enter(engine, false, error)) return false;
    if (engine->q3_first && engine->q3_count == engine->q3_capacity - engine->q3_first) {
        memmove(engine->q3_operations, engine->q3_operations + engine->q3_first,
            engine->q3_count * sizeof(*engine->q3_operations));
        engine->q3_first = 0;
    }
    bool okay = engine->q3_count < SIZE_MAX;
    qa_audio_q3_operation *next = okay ? grow(engine->q3_operations, &engine->q3_capacity,
        engine->q3_first + engine->q3_count + 1, sizeof(*next), error) : NULL;
    if (next) engine->q3_operations = next;
    qa_audio_asset *asset = next && operation->sound.asset ?
        qa_audio_asset_retain(operation->sound.asset) : NULL;
    okay = next && (!operation->sound.asset || asset);
    if (okay) {
        engine->q3_operations[engine->q3_first + engine->q3_count] = *operation;
        engine->q3_operations[engine->q3_first + engine->q3_count++].sound.asset = asset;
    } else if (!error || error->code == QA_OK)
        fail(error, QA_ERROR_MEMORY, "Retaining Q3 audio operation");
    leave(engine); return okay;
}
bool qa_audio_engine_q3_publish(qa_audio_engine *engine, qa_error *error)
{
    if (!enter(engine, true, error)) return false;
    bool okay = true;
    while (okay && engine->q3_count) {
        qa_audio_q3_operation operation = engine->q3_operations[engine->q3_first];
        audio_seat *seat = find_seat(engine, operation.sound.audience);
        /* Delivery can fail after an observer has seen it. The attempted
         * operation is consumed once; only the untouched suffix is retried. */
        ++engine->q3_first; --engine->q3_count;
        if (!engine->q3_count) engine->q3_first = 0;
        /* The actual published listener roster selects CG audio recipients.
         * An absent recipient contributes no audio, including during menus. */
        if (operation.sound.audience == QA_AUDIO_WORLD ? !engine->seat_count : !seat) {
            qa_audio_asset_release(operation.sound.asset); continue;
        }
        switch (operation.kind) {
        case QA_AUDIO_Q3_PLAY:
            okay = play_impl(engine, &operation.sound, operation.milliseconds, error); break;
        case QA_AUDIO_Q3_LOOP: {
            qa_audio_loop loop = {.sound = operation.sound, .velocity = operation.velocity,
                .frame_number = operation.frame_number, .persistent = operation.persistent};
            okay = loop_impl(engine, &loop, error); break;
        }
        case QA_AUDIO_Q3_CLEAR:
            qa_audio_mixer_clear_seat_loops(seat->mixer, operation.sound.owner, operation.all); break;
        case QA_AUDIO_Q3_STOP:
            okay = stop_loop_impl(engine, operation.sound.actor, operation.sound.owner,
                operation.sound.audience, error); break;
        case QA_AUDIO_Q3_POSITION:
            okay = qa_audio_mixer_position_owner(seat->mixer, operation.sound.actor,
                operation.sound.owner, operation.sound.origin, error); break;
        }
        qa_audio_asset_release(operation.sound.asset);
        if (engine->destroy_pending)
            okay = fail(error, QA_ERROR_ARGUMENT, "Q3 audio owner retired during delivery");
    }
    leave(engine); return okay;
}
bool qa_audio_engine_end_loop_frame(qa_audio_engine *engine, qa_error *error) {
    if (!enter(engine, true, error)) return false;
    bool result = end_loop_frame_impl(engine, error);
    leave(engine); return result;
}
bool qa_audio_engine_stop_loop(qa_audio_engine *engine, uint64_t actor, uint64_t owner,
    uint32_t audience, qa_error *error) {
    if (!enter(engine, false, error)) return false;
    bool result = stop_loop_impl(engine, actor, owner, audience, error);
    leave(engine); return result;
}
bool qa_audio_engine_environment(qa_audio_engine *engine, uint32_t id,
    qa_audio_environment *environment, qa_error *error) {
    if (!enter(engine, true, error)) return false;
    bool result = environment_impl(engine, id, environment, error);
    leave(engine); return result;
}
void qa_audio_engine_update(qa_audio_engine *engine, double milliseconds) {
    if (!enter(engine, true, NULL)) return;
    update_impl(engine, milliseconds); leave(engine);
}
bool qa_audio_source_milliseconds(double milliseconds, int32_t *out, qa_error *error) {
    if (!out || !isfinite(milliseconds) || milliseconds < -0x1p63 || milliseconds >= 0x1p63) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source audio clock exceeds native millisecond storage");
        return false;
    }
    uint32_t bits = (uint32_t)(int64_t)milliseconds;
    memcpy(out, &bits, sizeof(bits));
    return true;
}
bool qa_audio_engine_play(qa_audio_engine *engine, const qa_audio_play *sound, int32_t milliseconds,
                          qa_error *error) {
    if (!enter(engine, false, error))
        return false;
    bool result = play_impl(engine, sound, milliseconds, error);
    leave(engine);
    return result;
}
void qa_audio_engine_stop_channel(qa_audio_engine *engine, uint64_t actor, uint64_t owner,
                                  qa_audio_family family, int32_t channel) {
    if (!enter(engine, false, NULL))
        return;
    stop_channel_impl(engine, actor, owner, family, channel);
    leave(engine);
}
bool qa_audio_engine_stop_actor(qa_audio_engine *engine, uint64_t actor, uint64_t owner,
                                qa_error *error) {
    if (!enter(engine, false, error))
        return false;
    bool result = stop_actor_impl(engine, actor, owner, error);
    leave(engine);
    return result;
}
bool qa_audio_engine_stop_owner(qa_audio_engine *engine, uint64_t owner, uint32_t audience,
                                qa_error *error) {
    if (!enter(engine, false, error))
        return false;
    bool result = stop_owner_impl(engine, owner, audience, error);
    leave(engine);
    return result;
}
void qa_audio_engine_stop_all(qa_audio_engine *engine) {
    if (!enter(engine, false, NULL))
        return;
    stop_all_impl(engine, false);
    leave(engine);
}
bool qa_audio_engine_mix(qa_audio_engine *engine, int16_t *stereo, size_t frames, qa_error *error) {
    if (!enter(engine, true, error))
        return false;
    bool result = mix_impl(engine, stereo, frames, error);
    leave(engine);
    return result;
}
static bool engine_owners_ready(const qa_audio_engine *engine, bool selections, qa_error *error) {
    if (!engine || engine->operation_depth || engine->callback_depth || engine->acoustics_readers ||
        engine->destroy_pending || engine->destroying || engine->round_resetting)
        return fail(error, QA_ERROR_ARGUMENT, "Audio round requires completed operations and callbacks");
    if (engine->q3_first > engine->q3_capacity || engine->q3_count > engine->q3_capacity - engine->q3_first ||
        (engine->q3_capacity && !engine->q3_operations))
        return fail(error, QA_ERROR_ARGUMENT, "Audio round lost its pending Q3 operations");
    for (size_t i = 0; i < engine->q3_count; ++i)
        if (!qa_audio_q3_operation_valid(engine->q3_operations + engine->q3_first + i))
            return fail(error, QA_ERROR_ARGUMENT, "Audio round lost a pending Q3 source holder");
    for (size_t i = 0; i < engine->seat_count; ++i) {
        if (!engine->seats[i] || !qa_audio_mixer_callbacks_idle(engine->seats[i]->mixer))
            return fail(error, QA_ERROR_ARGUMENT, "Audio round requires actual idle seat mixers");
        for (size_t j = 0; j < i; ++j)
            if (engine->seats[j]->listener.seat == engine->seats[i]->listener.seat)
                return fail(error, QA_ERROR_ARGUMENT, "Audio round has duplicate active mixer ownership");
    }
    for (size_t i = 0; i < engine->round_mixer_count; ++i) {
        if (!qa_audio_mixer_callbacks_idle(engine->round_mixers[i].mixer))
            return fail(error, QA_ERROR_ARGUMENT, "Audio round requires actual idle retained mixers");
        for (size_t j = 0; j < engine->seat_count; ++j)
            if (engine->seats[j]->listener.seat == engine->round_mixers[i].seat)
                return fail(error, QA_ERROR_ARGUMENT, "Audio round mixer has active and inactive owners");
        for (size_t j = 0; j < i; ++j)
            if (engine->round_mixers[j].seat == engine->round_mixers[i].seat)
                return fail(error, QA_ERROR_ARGUMENT, "Audio round has duplicate retained mixer ownership");
    }
    if (engine->bus_count > engine->bus_capacity || (engine->bus_count && !engine->buses))
        return fail(error, QA_ERROR_ARGUMENT, "Audio round bus ownership is invalid");
    for (size_t i = 0; i < engine->bus_count; ++i) {
        const audio_bus *bus = &engine->buses[i];
        if ((bus->raw != NULL) == (bus->music != NULL) ||
            (bus->lifetime != QA_AUDIO_MUSIC_WORLD && bus->lifetime != QA_AUDIO_MUSIC_MENU) ||
            (bus->raw && (bus->lifetime != QA_AUDIO_MUSIC_WORLD || !bus->active)))
            return fail(error, QA_ERROR_ARGUMENT, "Audio round lost its actual source bus route");
        if (bus->music) {
            const qa_audio_music_selection *selection = qa_audio_music_selection_read(bus->music);
            if (selection ? !selections || !qa_audio_music_selection_current(selection, bus->music) :
                !qa_audio_music_idle(bus->music))
                return fail(error, QA_ERROR_ARGUMENT, "Audio round retains a music player operation");
        }
    }
    return true;
}
bool qa_audio_engine_round_ready(const qa_audio_engine *engine, qa_error *error) {
    return engine_owners_ready(engine, false, error);
}
struct qa_audio_stream_cut {
    qa_audio_engine *destination, *source;
    audio_bus *prepared;
    size_t source_index;
    bool published, publishing;
};
static qa_audio_mixer *cut_mixer(qa_audio_engine *engine, size_t index) {
    return index < engine->seat_count ? engine->seats[index]->mixer :
        engine->round_mixers[index - engine->seat_count].mixer;
}
static void cut_unlock(qa_audio_engine *engine) {
    for (size_t i = 0; i < engine->seat_count + engine->round_mixer_count; ++i)
        (void)qa_audio_mixer_round_unlock(cut_mixer(engine, i));
    engine->round_resetting = false;
    engine->round_destroy_requested = false;
}
static bool cut_lock(qa_audio_engine *engine, qa_error *error) {
    size_t count = engine->seat_count + engine->round_mixer_count;
    for (size_t i = 0; i < count; ++i) {
        if (!qa_audio_mixer_round_lock(cut_mixer(engine, i), error)) {
            for (size_t j = 0; j < i; ++j)
                (void)qa_audio_mixer_round_unlock(cut_mixer(engine, j));
            return false;
        }
    }
    engine->round_resetting = true;
    return true;
}
static bool cut_engine_current(qa_audio_engine *engine) {
    if (!engine->round_resetting || engine->round_destroy_requested || engine->destroy_pending ||
        engine->destroying || engine->operation_depth || engine->callback_depth || engine->acoustics_readers)
        return false;
    for (size_t i = 0; i < engine->seat_count + engine->round_mixer_count; ++i) {
        qa_audio_mixer *mixer = cut_mixer(engine, i);
        if (!mixer->round_locked || mixer->round_destroy_requested || mixer->destroy_requested ||
            mixer->destroying || mixer->callback_active || mixer->dispatching) return false;
    }
    return true;
}
typedef struct audio_gain_player {
    qa_audio_music *music;
    qa_audio_music_hold *hold;
    uint64_t id;
    size_t index;
} audio_gain_player;
struct qa_audio_engine_gains {
    qa_audio_engine *engine;
    float effects, music;
    qa_audio_engine_acoustics *acoustics;
    audio_gain_player *players;
    size_t player_count;
};
static void gains_release_players(qa_audio_engine_gains *ticket) {
    for (size_t i = 0; i < ticket->player_count; ++i)
        qa_audio_music_hold_release(&ticket->players[i].hold);
    free(ticket->players);
}
bool qa_audio_engine_gains_prepare(qa_audio_engine *engine, float effects, float music,
    qa_audio_engine_gains **out, qa_error *error) {
    if (!out || *out || !isfinite(effects) || effects < 0 ||
        (double)(effects * 255.0f) > INT32_MAX || !isfinite(music) || music < 0 ||
        !engine_owners_ready(engine, true, error))
        return fail(error, QA_ERROR_ARGUMENT, "Audio gains require actual idle owners and finite targets");
    if (engine->seat_count > SIZE_MAX - engine->round_mixer_count ||
        engine->bus_count > engine->bus_capacity || (engine->bus_count && !engine->buses))
        return fail(error, QA_ERROR_ARGUMENT, "Audio gain ownership inventory is invalid");
    qa_audio_engine_gains *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return fail(error, QA_ERROR_MEMORY, "Retaining prepared audio gains");
    size_t count = 0;
    for (size_t i = 0; i < engine->bus_count; ++i) if (engine->buses[i].music) ++count;
    if (count > SIZE_MAX / sizeof(*ticket->players)) {
        free(ticket); return fail(error, QA_ERROR_MEMORY, "Prepared music ownership overflows");
    }
    ticket->players = count ? calloc(count, sizeof(*ticket->players)) : NULL;
    if (count && !ticket->players) {
        free(ticket); return fail(error, QA_ERROR_MEMORY, "Retaining prepared music owners");
    }
    if (!cut_lock(engine, error)) { free(ticket->players); free(ticket); return false; }
    ticket->engine = engine; ticket->effects = effects; ticket->music = music;
    for (size_t i = 0; i < engine->bus_count; ++i) {
        const audio_bus *bus = &engine->buses[i];
        if (!bus->music) continue;
        audio_gain_player *player = &ticket->players[ticket->player_count];
        *player = (audio_gain_player){.music=bus->music, .id=bus->id, .index=i};
        if (!qa_audio_music_hold_prepare(player->music, &player->hold, error)) {
            gains_release_players(ticket); cut_unlock(engine); free(ticket); return false;
        }
        ++ticket->player_count;
    }
    engine->gains = ticket;
    *out = ticket;
    return true;
}
bool qa_audio_engine_gains_ready(const qa_audio_engine_gains *ticket, qa_error *error) {
    if (!ticket || ticket->engine->gains != ticket || !cut_engine_current(ticket->engine))
        return fail(error, QA_ERROR_ARGUMENT, "Prepared audio gain owners have changed");
    for (size_t i = 0; i < ticket->player_count; ++i) {
        const audio_gain_player *player = &ticket->players[i];
        if (player->index >= ticket->engine->bus_count ||
            ticket->engine->buses[player->index].id != player->id ||
            ticket->engine->buses[player->index].music != player->music ||
            !qa_audio_music_hold_current(player->hold, player->music))
            return fail(error, QA_ERROR_ARGUMENT, "Prepared audio lost its retained music player");
    }
    return true;
}
void qa_audio_engine_gains_publish(qa_audio_engine_gains *ticket) {
    if (!ticket || ticket->acoustics || !qa_audio_engine_gains_ready(ticket, NULL)) return;
    for (size_t i = 0; i < ticket->player_count; ++i)
        if (!qa_audio_music_hold_consumed(ticket->players[i].hold, ticket->players[i].music)) return;
    qa_audio_engine *engine = ticket->engine;
    engine->effects_gain = ticket->effects;
    engine->music_gain = ticket->music;
    for (size_t i = 0; i < engine->seat_count + engine->round_mixer_count; ++i)
        cut_mixer(engine, i)->effects_gain = ticket->effects;
    for (size_t i = 0; i < engine->bus_count; ++i)
        if (engine->buses[i].music)
            qa_audio_music_target_publish(engine->buses[i].music, ticket->music);
    engine->gains = NULL;
    gains_release_players(ticket);
    cut_unlock(engine);
    free(ticket);
}
void qa_audio_engine_gains_abort(qa_audio_engine_gains *ticket) {
    if (!ticket || ticket->acoustics || ticket->engine->gains != ticket) return;
    ticket->engine->gains = NULL;
    gains_release_players(ticket);
    cut_unlock(ticket->engine);
    free(ticket);
}

struct qa_audio_engine_acoustics {
    qa_audio_engine_gains *parent;
    qa_audio_acoustics_source source;
    bool enabled;
};
static bool acoustics_source_current(const qa_audio_acoustics_source *source) {
    return source && source->context && source->current && source->trace && source->release &&
        source->current(source->context);
}
static bool acoustics_hit(const qa_audio_trace_hit *hit, qa_error *error) {
    return (isfinite(hit->fraction) && hit->fraction >= 0 && hit->fraction <= 1) ||
        fail(error, QA_ERROR_ARGUMENT, "Acoustic scene returned an invalid trace fraction");
}
bool qa_audio_engine_acoustics_transmit(void *context, const qa_audio_listener *listener,
    qa_vec3 end, float *out, qa_error *error) {
    qa_audio_engine *engine = context;
    if (!engine || !listener || !out || !engine->acoustics_enabled ||
        engine->destroying || engine->destroy_pending || engine->acoustics_readers ||
        !acoustics_source_current(&engine->acoustics))
        return fail(error, QA_ERROR_ARGUMENT, "Acoustics requires its retained current listener scene");
    qa_vec3 start = listener->origin;
    double dx = (double)end.x - start.x, dy = (double)end.y - start.y, dz = (double)end.z - start.z;
    double distance = hypot(hypot(dx, dy), dz);
    if (!isfinite(distance)) return fail(error, QA_ERROR_ARGUMENT, "Invalid acoustic trace endpoints");
    if (distance == 0) { *out = 1; return true; }
    engine->acoustics_readers++;
    qa_audio_trace_hit forward = {0}, reverse = {0};
    bool ok = engine->acoustics.trace(engine->acoustics.context, listener, start, end, &forward, error);
    if (ok) ok = acoustics_hit(&forward, error);
    if (ok) ok = !engine->destroy_pending && acoustics_source_current(&engine->acoustics);
    bool clear = ok && forward.fraction == 1 && !forward.start_solid && !forward.all_solid;
    if (ok && !clear) {
        ok = engine->acoustics.trace(engine->acoustics.context, listener, end, start, &reverse, error);
        if (ok) ok = acoustics_hit(&reverse, error);
        if (ok) ok = !engine->destroy_pending && acoustics_source_current(&engine->acoustics);
    }
    engine->acoustics_readers--;
    if (!ok) {
        if (!error || error->code == QA_OK)
            fail(error, QA_ERROR_ARGUMENT, "Acoustic scene changed during its actual trace");
        return false;
    }
    double thickness = forward.all_solid || reverse.all_solid ? distance :
        fmax(0, distance * (1 - (forward.start_solid ? 0 : forward.fraction) -
            (reverse.start_solid ? 0 : reverse.fraction)));
    *out = clear ? 1 : (float)pow(0.5, 1 + thickness / 64);
    return true;
}
void qa_audio_engine_acoustics_rebind(qa_audio_engine *engine) {
    for (size_t i=0; i<engine->seat_count + engine->round_mixer_count; ++i) {
        qa_audio_mixer *mixer = cut_mixer(engine, i);
        mixer->transmission = engine->acoustics_enabled ? NULL : engine->geometry;
        mixer->transmission_checked = engine->acoustics_enabled ? qa_audio_engine_acoustics_transmit : NULL;
        mixer->transmission_user = engine->acoustics_enabled ? engine : engine->geometry_user;
        mixer->transmission_count = 0;
    }
}
bool qa_audio_engine_acoustics_prepare(qa_audio_engine_gains *parent, bool enabled,
    const qa_audio_acoustics_source *source, qa_audio_engine_acoustics **out, qa_error *error) {
    if (!out || *out || !parent || parent->acoustics ||
        !qa_audio_engine_gains_ready(parent, error) ||
        (source ? !acoustics_source_current(source) : enabled))
        return fail(error, QA_ERROR_ARGUMENT, "Acoustics preparation requires its real gains and scene owners");
    qa_audio_engine_acoustics *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return fail(error, QA_ERROR_MEMORY, "Retaining prepared scene acoustics");
    ticket->parent = parent; ticket->enabled = enabled;
    if (source) ticket->source = *source;
    parent->acoustics = ticket; *out = ticket;
    return true;
}
bool qa_audio_engine_acoustics_ready_is(const qa_audio_engine_acoustics *ticket,
    const qa_audio_engine_gains *parent) {
    return ticket && parent && ticket->parent == parent && parent->acoustics == ticket &&
        qa_audio_engine_gains_ready(parent, NULL) &&
        (ticket->source.context ? acoustics_source_current(&ticket->source) : !ticket->enabled);
}
bool qa_audio_engine_acoustics_ready(const qa_audio_engine_acoustics *ticket, qa_error *error) {
    return qa_audio_engine_acoustics_ready_is(ticket, ticket ? ticket->parent : NULL) ||
        fail(error, QA_ERROR_ARGUMENT, "Prepared acoustics lost its exact gains or scene owner");
}
void qa_audio_engine_acoustics_publish(qa_audio_engine_acoustics *ticket) {
    if (!qa_audio_engine_acoustics_ready(ticket, NULL)) return;
    qa_audio_engine *engine = ticket->parent->engine;
    qa_audio_acoustics_source old = engine->acoustics;
    engine->acoustics = ticket->source; engine->acoustics_enabled = ticket->enabled;
    engine->geometry = NULL; engine->geometry_user = NULL;
    qa_audio_engine_acoustics_rebind(engine);
    ticket->parent->acoustics = NULL;
    free(ticket);
    if (old.context) old.release(old.context);
}
bool qa_audio_engine_acoustics_abort(qa_audio_engine_acoustics *ticket, qa_error *error) {
    if (!ticket) return true;
    if (!ticket->parent || ticket->parent->acoustics != ticket ||
        ticket->parent->engine->gains != ticket->parent ||
        !ticket->parent->engine->round_resetting ||
        ticket->parent->engine->operation_depth || ticket->parent->engine->callback_depth ||
        ticket->parent->engine->acoustics_readers || ticket->parent->engine->destroying)
        return fail(error, QA_ERROR_ARGUMENT, "Acoustic cancellation lost its actual owner boundary");
    qa_audio_engine *engine=ticket->parent->engine;
    for (size_t i=0;i<engine->seat_count+engine->round_mixer_count;++i) {
        const qa_audio_mixer *mixer=cut_mixer(engine,i);
        if (!mixer || !mixer->round_locked || mixer->callback_active || mixer->dispatching ||
            mixer->destroying)
            return fail(error,QA_ERROR_ARGUMENT,"Acoustic cancellation requires its exact returned mixer leases");
    }
    ticket->parent->acoustics = NULL;
    if (ticket->source.context) ticket->source.release(ticket->source.context);
    free(ticket); return true;
}
bool qa_audio_engine_acoustics_release(qa_audio_engine *engine, qa_error *error) {
    if (!qa_audio_engine_round_ready(engine, error)) return false;
    qa_audio_acoustics_source source = engine->acoustics;
    engine->acoustics = (qa_audio_acoustics_source){0};
    qa_audio_engine_acoustics_rebind(engine);
    if (source.context) source.release(source.context);
    return true;
}
bool qa_audio_engine_acoustics_enabled(const qa_audio_engine *engine) {
    return engine && engine->acoustics_enabled;
}
bool qa_audio_engine_acoustics_bind(qa_audio_engine *engine, bool enabled,
    const qa_audio_acoustics_source *source, qa_error *error) {
    if (!qa_audio_engine_round_ready(engine, error) ||
        (source ? !acoustics_source_current(source) : enabled))
        return fail(error, QA_ERROR_ARGUMENT, "Acoustic binding requires its admitted idle scene owners");
    qa_audio_acoustics_source old = engine->acoustics;
    engine->acoustics = source ? *source : (qa_audio_acoustics_source){0};
    engine->acoustics_enabled = enabled;
    engine->geometry = NULL; engine->geometry_user = NULL;
    qa_audio_engine_acoustics_rebind(engine);
    if (old.context) old.release(old.context);
    return true;
}
bool qa_audio_engine_stream_cut_prepare(qa_audio_engine *destination, qa_audio_engine *source,
    uint64_t id, uint32_t audience, float gain, qa_audio_stream_cut **out, qa_error *error) {
    if (!out || *out || destination == source || !isfinite(gain) || gain < 0 ||
        !qa_audio_engine_round_ready(destination, error) || !qa_audio_engine_round_ready(source, error))
        return fail(error, QA_ERROR_ARGUMENT, "Audio stream cut requires distinct idle owners");
    if (destination->options.sample_rate != source->options.sample_rate ||
        destination->seat_count > SIZE_MAX - destination->round_mixer_count ||
        source->seat_count > SIZE_MAX - source->round_mixer_count ||
        destination->bus_count > destination->bus_capacity || source->bus_count > source->bus_capacity ||
        (destination->bus_capacity && !destination->buses) || (source->bus_capacity && !source->buses))
        return fail(error, QA_ERROR_ARGUMENT, "Audio stream cut has invalid rate or ownership storage");
    size_t index = SIZE_MAX;
    for (size_t i = 0; i < source->bus_count; ++i) {
        const audio_bus *bus = &source->buses[i];
        if (bus->id != id || !bus->raw) continue;
        if (index != SIZE_MAX || bus->music || bus->audience != audience ||
            memcmp(&bus->gain, &gain, sizeof(gain)) ||
            qa_audio_raw_rate(bus->raw) != destination->options.sample_rate)
            return fail(error, QA_ERROR_ARGUMENT, "Audio stream cut has another source route");
        index = i;
    }
    if (index != SIZE_MAX) {
        qa_audio_raw_stream *raw = source->buses[index].raw;
        for (size_t i = 0; i < source->bus_count; ++i)
            if (i != index && source->buses[i].raw == raw)
                return fail(error, QA_ERROR_ARGUMENT, "Audio stream cut has duplicate source ownership");
        for (size_t i = 0; i < destination->bus_count; ++i)
            if (destination->buses[i].raw == raw)
                return fail(error, QA_ERROR_ARGUMENT, "Audio stream cut raw queue already belongs to destination");
    }
    qa_audio_stream_cut *cut = calloc(1, sizeof(*cut));
    if (!cut) return fail(error, QA_ERROR_MEMORY, "Allocating audio stream cut");
    cut->prepared = malloc(sizeof(*cut->prepared));
    if (!cut->prepared) { free(cut); return fail(error, QA_ERROR_MEMORY, "Reserving audio stream publication"); }
    *cut->prepared = (audio_bus){.id=id, .audience=audience, .gain=gain,
        .raw=index == SIZE_MAX ? NULL : source->buses[index].raw,
        .lifetime=QA_AUDIO_MUSIC_WORLD, .active=true};
    if (!cut_lock(destination, error)) { free(cut->prepared); free(cut); return false; }
    if (!cut_lock(source, error)) {
        cut_unlock(destination); free(cut->prepared); free(cut); return false;
    }
    cut->destination = destination; cut->source = source; cut->source_index = index;
    *out = cut;
    return true;
}
bool qa_audio_engine_stream_cut_current(const qa_audio_stream_cut *cut) {
    if (!cut || cut->published || cut->publishing || !cut->prepared || !cut_engine_current(cut->destination) ||
        !cut_engine_current(cut->source)) return false;
    if (cut->source_index == SIZE_MAX) {
        for (size_t i = 0; i < cut->source->bus_count; ++i)
            if (cut->source->buses[i].id == cut->prepared->id && cut->source->buses[i].raw) return false;
        return cut->prepared->raw == NULL;
    }
    if (cut->source_index >= cut->source->bus_count) return false;
    const audio_bus *bus = &cut->source->buses[cut->source_index];
    return bus->id == cut->prepared->id && bus->audience == cut->prepared->audience &&
        !memcmp(&bus->gain, &cut->prepared->gain, sizeof(bus->gain)) &&
        bus->raw == cut->prepared->raw && !bus->music &&
        qa_audio_raw_rate(bus->raw) == cut->destination->options.sample_rate;
}
void qa_audio_engine_stream_cut_publish(qa_audio_stream_cut *cut) {
    if (!qa_audio_engine_stream_cut_current(cut)) return;
    cut->publishing = true;
    qa_audio_engine *destination = cut->destination, *source = cut->source;
    stop_all_impl(destination, false);
    if (cut->source_index != SIZE_MAX) {
        memmove(source->buses + cut->source_index, source->buses + cut->source_index + 1,
            (source->bus_count - cut->source_index - 1) * sizeof(*source->buses));
        --source->bus_count;
    }
    free(destination->buses);
    destination->buses = cut->prepared; destination->bus_capacity = 1;
    destination->bus_count = cut->prepared->raw ? 1 : 0;
    cut->prepared = NULL; cut->published = true;
    cut_unlock(source); cut_unlock(destination);
    cut->publishing = false;
}
void qa_audio_engine_stream_cut_destroy(qa_audio_stream_cut *cut) {
    if (!cut || cut->publishing) return;
    if (!cut->published) { cut_unlock(cut->source); cut_unlock(cut->destination); }
    free(cut->prepared); free(cut);
}
bool qa_audio_engine_reset_round(qa_audio_engine *engine, qa_error *error) {
    if (!qa_audio_engine_round_ready(engine, error)) return false;
    if (engine->seat_count > SIZE_MAX - engine->round_mixer_count)
        return fail(error, QA_ERROR_MEMORY, "Audio round mixer inventory overflows");
    audio_round_mixer *retained = grow(engine->round_mixers, &engine->round_mixer_capacity,
        engine->round_mixer_count + engine->seat_count, sizeof(*retained), error);
    if (engine->round_mixer_count + engine->seat_count && !retained) return false;
    engine->round_mixers = retained;
    if (!enter(engine, true, error))
        return false;
    engine->round_resetting = true;
    bool locked = true;
    for (size_t i = 0; locked && i < engine->seat_count; ++i)
        locked = qa_audio_mixer_round_lock(engine->seats[i]->mixer, error);
    for (size_t i = 0; locked && i < engine->round_mixer_count; ++i)
        locked = qa_audio_mixer_round_lock(engine->round_mixers[i].mixer, error);
    if (!locked) {
        for (size_t i = 0; i < engine->seat_count; ++i) qa_audio_mixer_round_unlock(engine->seats[i]->mixer);
        for (size_t i = 0; i < engine->round_mixer_count; ++i) qa_audio_mixer_round_unlock(engine->round_mixers[i].mixer);
        engine->round_resetting = false;
        leave(engine); return false;
    }
    stop_all_impl(engine, true);
    engine->position_count = 0;
    for (size_t i = 0; i < engine->seat_count; ++i) {
        audio_seat *seat = engine->seats[i]; size_t at = engine->round_mixer_count++;
        engine->round_mixers[at] = (audio_round_mixer){seat->listener.seat, seat->mixer};
        seat->mixer = NULL; seat_destroy(seat);
    }
    engine->seat_count = 0;
    bool result = !engine->round_destroy_requested;
    for (size_t i = 0; i < engine->round_mixer_count; ++i)
        if (!qa_audio_mixer_round_unlock(engine->round_mixers[i].mixer)) result = false;
    engine->round_resetting = false; engine->round_destroy_requested = false;
    leave(engine);
    if (!result) return fail(error, QA_ERROR_ARGUMENT, "Audio round rejected callback destruction and retained its owners");
    return result;
}

qa_audio_music *qa_audio_engine_bus_music(qa_audio_engine *engine, uint64_t bus) {
    if (engine && !engine->destroy_pending && !engine->destroying)
        for (size_t i = 0; i < engine->bus_count; ++i)
            if (engine->buses[i].id == bus && engine->buses[i].music) return engine->buses[i].music;
    return NULL;
}
qa_audio_raw_stream *qa_audio_engine_bus_stream(qa_audio_engine *engine, uint64_t bus) {
    if (engine && !engine->destroy_pending && !engine->destroying)
        for (size_t i = 0; i < engine->bus_count; ++i)
            if (engine->buses[i].id == bus && engine->buses[i].raw) return engine->buses[i].raw;
    return NULL;
}
bool qa_audio_engine_music_ready(const qa_audio_engine *engine, uint64_t id,
    uint32_t audience, float gain)
{
    if (!engine || engine->operation_depth || engine->callback_depth || engine->destroy_pending ||
        engine->destroying || engine->round_resetting || engine->bus_count > engine->bus_capacity ||
        (engine->bus_count && !engine->buses) || !isfinite(gain) || gain < 0) return false;
    const audio_bus *bus = NULL;
    for (size_t i = 0; i < engine->bus_count; ++i) {
        const audio_bus *row = engine->buses + i;
        if (row->id != id || !row->music) continue;
        if (bus || row->raw) return false;
        bus = row;
    }
    return bus && bus->audience == audience && !memcmp(&bus->gain, &gain, sizeof(gain));
}

bool qa_audio_engine_raw_ready(const qa_audio_engine *engine, uint64_t id,
    uint32_t audience, float gain, bool present, qa_error *error)
{
    if (!engine || engine->operation_depth || engine->callback_depth || engine->destroy_pending || engine->destroying) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Raw queue qualification requires an idle restored engine"); return false;
    }
    const audio_bus *bus = NULL;
    for (size_t i=0;i<engine->bus_count;++i) if (engine->buses[i].id==id && engine->buses[i].raw) { bus=&engine->buses[i]; break; }
    if ((bus!=NULL)!=present || (bus && (bus->audience!=audience || memcmp(&bus->gain,&gain,sizeof(gain))))) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Restored cinematic queue presence or route differs"); return false;
    }
    return true;
}
