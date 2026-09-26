#include "qa/audio.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct audio_seat {
    qa_audio_listener listener;
    qa_audio_mixer *mixer;
    qa_audio_reverb *reverb;
    qa_audio_underwater underwater;
    qa_audio_environment *environment;
} audio_seat;
typedef struct audio_position {
    uint64_t actor;
    qa_vec3 position;
} audio_position;
typedef struct audio_bus {
    uint64_t id;
    uint32_t audience;
    float gain;
    qa_audio_raw_stream *raw;
    qa_audio_music *music;
} audio_bus;
struct qa_audio_engine {
    qa_audio_engine_options options;
    audio_seat **seats;
    size_t seat_count;
    audio_position *positions;
    size_t position_count, position_capacity;
    audio_bus *buses;
    size_t bus_count, bus_capacity;
    float *sum, *seat_scratch;
    int16_t *pcm_scratch;
    uint64_t clock, next_voice;
    double milliseconds;
    float effects_gain;
    bool paused, doppler, destroy_pending, destroying;
    unsigned operation_depth, callback_depth;
    qa_audio_transmission_fn geometry;
    void *geometry_user;
};
static bool fail(qa_error *error, qa_status code, const char *message) {
    qa_error_set(error, code, 0, "%s", message);
    return false;
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
    if (!engine || engine->destroying || engine->destroy_pending)
        return fail(error, QA_ERROR_ARGUMENT, "Audio engine is unavailable");
    if (structural && engine->callback_depth)
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
static bool seat_create(qa_audio_engine *engine, const qa_audio_listener *listener,
                        audio_seat **out, qa_error *error) {
    audio_seat *seat = calloc(1, sizeof(*seat));
    if (!seat)
        return fail(error, QA_ERROR_MEMORY, "Audio seat allocation failed");
    seat->listener = *listener;
    qa_audio_mixer_options options = {.sample_rate = engine->options.sample_rate,
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
    if (!qa_audio_mixer_create(&options, &seat->mixer, error) ||
        !qa_audio_reverb_create(engine->options.sample_rate, &seat->reverb, error))
        goto failed;
    qa_audio_mixer_geometry(seat->mixer, engine->geometry, engine->geometry_user);
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
    engine->doppler = true;
    engine->sum = malloc(frames * 2 * sizeof(float));
    engine->seat_scratch = malloc(frames * 2 * sizeof(float));
    engine->pcm_scratch = malloc(frames * 2 * sizeof(int16_t));
    if (!engine->sum || !engine->seat_scratch || !engine->pcm_scratch) {
        qa_audio_engine_destroy(engine);
        return fail(error, QA_ERROR_MEMORY, "Audio scratch allocation failed");
    }
    *out = engine;
    return true;
}
void qa_audio_engine_destroy(qa_audio_engine *engine) {
    if (!engine || engine->destroying)
        return;
    if (engine->operation_depth || engine->callback_depth) {
        engine->destroy_pending = true;
        return;
    }
    engine->destroying = true;
    for (size_t i = 0; i < engine->seat_count; i++)
        seat_destroy(engine->seats[i]);
    for (size_t i = 0; i < engine->bus_count; i++) {
        qa_audio_raw_destroy(engine->buses[i].raw);
        qa_audio_music_destroy(engine->buses[i].music);
    }
    free(engine->seats);
    free(engine->positions);
    free(engine->buses);
    free(engine->sum);
    free(engine->seat_scratch);
    free(engine->pcm_scratch);
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
        return true;
    }
    audio_seat **next = count ? calloc(count, sizeof(*next)) : NULL;
    bool *created = count ? calloc(count, sizeof(*created)) : NULL;
    if (count && (!next || !created)) {
        free(next);
        free(created);
        return fail(error, QA_ERROR_MEMORY, "Audio listener allocation failed");
    }
    for (size_t i = 0; i < count; i++) {
        next[i] = find_seat(engine, listeners[i].seat);
        if (!next[i]) {
            created[i] = true;
            if (!seat_create(engine, &listeners[i], &next[i], error))
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
    for (size_t i = 0; i < previous_count; i++) {
        bool retained = false;
        for (size_t j = 0; j < count; j++)
            if (next[j] == previous[i]) {
                retained = true;
                break;
            }
        if (!retained)
            seat_destroy(previous[i]);
    }
    free(previous);
    free(created);
    return true;
failed:
    for (size_t i = 0; i < count; i++)
        if (created[i])
            seat_destroy(next[i]);
    free(next);
    free(created);
    return false;
}
bool qa_audio_engine_position(qa_audio_engine *engine, uint64_t actor, qa_vec3 position,
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
    if (!engine || engine->destroying || engine->destroy_pending)
        return;
    engine->geometry = fn;
    engine->geometry_user = user;
    for (size_t i = 0; i < engine->seat_count; i++)
        qa_audio_mixer_geometry(engine->seats[i]->mixer, fn, user);
}
void qa_audio_engine_pause(qa_audio_engine *engine, bool paused) {
    if (engine && !engine->destroying && !engine->destroy_pending)
        engine->paused = paused;
}
void qa_audio_engine_gain(qa_audio_engine *engine, float gain) {
    if (!engine || engine->destroying || engine->destroy_pending || !isfinite(gain) || gain < 0 ||
        (double)(gain * 255.0f) > INT32_MAX)
        return;
    engine->effects_gain = gain;
    for (size_t i = 0; i < engine->seat_count; i++)
        qa_audio_mixer_effects_gain(engine->seats[i]->mixer, gain);
}
void qa_audio_engine_doppler(qa_audio_engine *engine, bool enabled) {
    if (!engine || engine->destroying || engine->destroy_pending)
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
bool qa_audio_engine_loop(qa_audio_engine *engine, const qa_audio_loop *loop, qa_error *error) {
    if (!engine || engine->destroying || engine->destroy_pending || !loop)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid audio loop request");
    for (size_t i = 0; i < engine->seat_count; i++)
        if (selected(loop->sound.audience, engine->seats[i]->listener.seat) &&
            !qa_audio_mixer_loop(engine->seats[i]->mixer, loop, error))
            return false;
    return true;
}
void qa_audio_engine_clear_loops(qa_audio_engine *engine, bool all) {
    if (engine && !engine->destroying && !engine->destroy_pending)
        for (size_t i = 0; i < engine->seat_count; i++)
            qa_audio_mixer_clear_loops(engine->seats[i]->mixer, all);
}
bool qa_audio_engine_end_loop_frame(qa_audio_engine *engine, qa_error *error) {
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
bool qa_audio_engine_stop_loop(qa_audio_engine *engine, uint64_t actor, uint64_t owner,
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
    return true;
}
static void stop_all_impl(qa_audio_engine *engine) {
    if (!engine)
        return;
    for (size_t i = 0; i < engine->seat_count; i++) {
        audio_seat *seat = engine->seats[i];
        qa_audio_mixer_stop_all(seat->mixer);
        qa_audio_reverb_reset(seat->reverb);
        seat->underwater = (qa_audio_underwater){0};
    }
    for (size_t i = 0; i < engine->bus_count; i++) {
        qa_audio_raw_destroy(engine->buses[i].raw);
        qa_audio_music_destroy(engine->buses[i].music);
    }
    engine->bus_count = 0;
}
static bool attach_bus(qa_audio_engine *engine, uint64_t id, uint32_t audience, float gain,
                       qa_audio_raw_stream *raw, qa_audio_music *music, qa_error *error) {
    if (!engine || engine->destroying || engine->destroy_pending || (!raw && !music) ||
        !isfinite(gain) || gain < 0)
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
    if (index < engine->bus_count) {
        if (engine->buses[index].raw != raw)
            qa_audio_raw_destroy(engine->buses[index].raw);
        if (engine->buses[index].music != music)
            qa_audio_music_destroy(engine->buses[index].music);
    }
    engine->buses[index] = (audio_bus){id, audience, gain, raw, music};
    if (index == engine->bus_count)
        engine->bus_count++;
    return true;
}
bool qa_audio_engine_stream(qa_audio_engine *engine, uint64_t id, uint32_t audience, float gain,
                            qa_audio_raw_stream *stream, qa_error *error) {
    return attach_bus(engine, id, audience, gain, stream, NULL, error);
}
bool qa_audio_engine_music(qa_audio_engine *engine, uint64_t id, uint32_t audience, float gain,
                           qa_audio_music *music, qa_error *error) {
    return attach_bus(engine, id, audience, gain, NULL, music, error);
}
static void remove_bus(qa_audio_engine *engine, uint64_t id, bool raw, bool music) {
    if (!engine || engine->destroying || engine->destroy_pending)
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
bool qa_audio_engine_environment(qa_audio_engine *engine, uint32_t id,
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
void qa_audio_engine_update(qa_audio_engine *engine, double milliseconds) {
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
            if (engine->buses[i].music)
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
            if (!qa_audio_mixer_mix(seat->mixer, engine->pcm_scratch, count, error))
                return false;
            for (size_t j = 0; j < count * 2; j++)
                engine->seat_scratch[j] = engine->pcm_scratch[j];
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
    stop_all_impl(engine);
    leave(engine);
}
bool qa_audio_engine_mix(qa_audio_engine *engine, int16_t *stereo, size_t frames, qa_error *error) {
    if (!enter(engine, true, error))
        return false;
    bool result = mix_impl(engine, stereo, frames, error);
    leave(engine);
    return result;
}
bool qa_audio_engine_reset_round(qa_audio_engine *engine, qa_error *error) {
    if (!enter(engine, true, error))
        return false;
    stop_all_impl(engine);
    engine->position_count = 0;
    bool result = listeners_impl(engine, NULL, 0, error);
    leave(engine);
    return result;
}
