#include "mixer_internal.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void flush_notifications(qa_audio_mixer *mixer);

static bool mixer_error(qa_error *error, qa_status status, const char *message) {
    qa_error_set(error, status, 0, "%s", message);
    return false;
}

static void *reserve(void *data, size_t *capacity, size_t needed, size_t width, qa_error *error) {
    if (needed <= *capacity)
        return data;
    if (needed > SIZE_MAX / width) {
        mixer_error(error, QA_ERROR_MEMORY, "Audio allocation size overflow");
        return NULL;
    }
    size_t count = *capacity ? *capacity : 8;
    while (count < needed) {
        if (count > SIZE_MAX / 2) {
            count = needed;
            break;
        }
        count *= 2;
    }
    if (count > SIZE_MAX / width)
        count = needed;
    void *result = realloc(data, count * width);
    if (!result) {
        mixer_error(error, QA_ERROR_MEMORY, "Cannot allocate audio storage");
        return NULL;
    }
    *capacity = count;
    return result;
}

static int setting(qa_audio_mixer *mixer, const char *name) {
    if (!mixer->options.setting)
        return strcmp(name, "s_doppler") == 0;
    mixer->callback_active = true;
    int value = mixer->options.setting(mixer->options.diagnostic_user, name);
    mixer->callback_active = false;
    return value;
}

static void log_message(qa_audio_mixer *mixer, const char *message) {
    if (!mixer->options.log)
        return;
    mixer->callback_active = true;
    mixer->options.log(mixer->options.diagnostic_user, message);
    mixer->callback_active = false;
}

static bool allow_mutation(qa_audio_mixer *mixer, qa_error *error) {
    if (!mixer)
        return mixer_error(error, QA_ERROR_ARGUMENT, "Missing audio mixer");
    if (mixer->callback_active || mixer->round_locked)
        return mixer_error(error, QA_ERROR_ARGUMENT, "Audio callbacks cannot mutate their mixer");
    if (mixer->destroy_requested || mixer->destroying)
        return mixer_error(error, QA_ERROR_ARGUMENT, "Audio mixer is being destroyed");
    return true;
}

bool qa_audio_mixer_callbacks_idle(const qa_audio_mixer *mixer) {
    return mixer && !mixer->callback_active && !mixer->dispatching &&
        !mixer->destroy_requested && !mixer->destroying && !mixer->round_locked;
}

bool qa_audio_mixer_round_lock(qa_audio_mixer *mixer, qa_error *error) {
    if (!qa_audio_mixer_callbacks_idle(mixer))
        return mixer_error(error, QA_ERROR_ARGUMENT, "Audio round mixer still has an operation owner");
    mixer->round_locked = true;
    return true;
}

bool qa_audio_mixer_round_unlock(qa_audio_mixer *mixer) {
    if (!mixer || !mixer->round_locked) return true;
    bool result = !mixer->round_destroy_requested;
    mixer->round_locked = false; mixer->round_destroy_requested = false;
    return result;
}

static void debug_raw(qa_audio_mixer *mixer, const char *operation) {
    if (mixer->options.log && setting(mixer, "developer")) {
        char message[160];
        snprintf(message, sizeof(message), "S_RawSamples: %s %" PRId64 " > %" PRId64 "\n",
                 operation, mixer->raw_end, mixer->sound_time);
        log_message(mixer, message);
    }
}

static bool family_valid(qa_game_family family) {
    return family == QA_GAME_Q1 || family == QA_GAME_Q2 || family == QA_GAME_Q3;
}

static bool selected(const qa_audio_mixer *mixer, uint32_t audience) {
    return audience == QA_AUDIO_WORLD || audience == mixer->listener.seat;
}

static bool validate_play(const qa_audio_play *sound, qa_error *error) {
    if (!sound || !family_valid(sound->family) || !isfinite(sound->volume) || sound->volume < 0 ||
        sound->volume > 1 || !isfinite(sound->attenuation) || sound->attenuation < 0 ||
        !isfinite(sound->delay_seconds) ||
        (sound->has_server_time && !isfinite(sound->server_milliseconds)) ||
        (sound->channel < 0 && !(sound->family == QA_GAME_Q1 && sound->channel == -1)))
        return mixer_error(error, QA_ERROR_ARGUMENT, "Invalid audio play policy");
    switch (sound->origin_kind) {
    case QA_AUDIO_LOCAL:
        return true;
    case QA_AUDIO_FIXED:
        if (qa_vec_finite(sound->origin))
            return true;
        break;
    case QA_AUDIO_ACTOR:
        if (sound->origin_actor != QA_AUDIO_NO_ACTOR)
            return true;
        break;
    }
    return mixer_error(error, QA_ERROR_ARGUMENT, "Invalid audio origin");
}

static uint64_t channel_name(qa_game_family family, int32_t channel) {
    if (channel <= 0)
        return 0;
    if (family != QA_GAME_Q3 && channel <= 4)
        return (uint64_t)channel;
    if (family == QA_GAME_Q3 && channel <= 7) {
        static const uint8_t names[] = {5, 1, 2, 3, 4, 6, 7};
        return names[channel - 1];
    }
    return ((uint64_t)(family + 1) << 32) | (uint32_t)channel;
}

static qa_mixer_prepared *prepared_retain(qa_mixer_prepared *prepared) {
    prepared->references++;
    return prepared;
}

static size_t prepared_hash(const qa_audio_sample *sample, const qa_audio_asset *asset, bool q3) {
    uint64_t hash = (uint64_t)(uintptr_t)sample ^
                    ((uint64_t)(uintptr_t)asset * UINT64_C(0x9e3779b97f4a7c15)) ^ (uint64_t)q3;
    hash ^= hash >> 30;
    hash *= UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= hash >> 27;
    hash *= UINT64_C(0x94d049bb133111eb);
    return (size_t)(hash ^ (hash >> 31));
}

static bool prepared_index_reserve(qa_audio_mixer *mixer, size_t count, qa_error *error) {
    size_t capacity = mixer->prepared_index_capacity;
    if (count <= capacity / 2)
        return true;
    if (!capacity)
        capacity = 16;
    while (count > capacity / 2) {
        if (capacity > SIZE_MAX / 2)
            return mixer_error(error, QA_ERROR_MEMORY, "Too many prepared effects");
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(*mixer->prepared_index))
        return mixer_error(error, QA_ERROR_MEMORY, "Prepared effect index size overflow");
    qa_mixer_prepared **index = calloc(capacity, sizeof(*index));
    if (!index)
        return mixer_error(error, QA_ERROR_MEMORY, "Cannot allocate prepared effect index");
    /* Keep the first occupied slot authoritative, including restored tables. */
    for (size_t i = mixer->prepared_count; i > 0; --i) {
        qa_mixer_prepared *prepared = mixer->prepared[i - 1];
        if (!prepared)
            continue;
        size_t slot = prepared_hash(prepared->sample, prepared->asset, prepared->q3) & (capacity - 1);
        prepared->next = index[slot];
        index[slot] = prepared;
    }
    free(mixer->prepared_index);
    mixer->prepared_index = index;
    mixer->prepared_index_capacity = capacity;
    return true;
}

bool qa_mixer_prepared_index(qa_audio_mixer *mixer, qa_error *error) {
    return prepared_index_reserve(mixer, mixer->prepared_count, error);
}

static void prepared_release(qa_audio_mixer *mixer, qa_mixer_prepared *prepared) {
    if (!prepared)
        return;
    if (--prepared->references == 0) {
        size_t slot = prepared_hash(prepared->sample, prepared->asset, prepared->q3) &
                      (mixer->prepared_index_capacity - 1);
        qa_mixer_prepared **link = &mixer->prepared_index[slot];
        while (*link != prepared)
            link = &(*link)->next;
        *link = prepared->next;
        mixer->prepared[prepared->slot] = NULL;
        qa_audio_sample_release(prepared->sample);
        qa_audio_sample_release(prepared->pcm);
        qa_audio_asset_release(prepared->asset);
        free(prepared->doppler_sums);
        free(prepared);
    }
}

static qa_mixer_prepared *prepare(qa_audio_mixer *mixer, qa_audio_sample *sample,
                                  qa_audio_asset *asset, bool q3, qa_error *error) {
    if (!sample || (asset && qa_audio_asset_sample(asset) != sample) || sample->channels != 1 ||
        !sample->sample_rate || !sample->frame_count || !sample->samples ||
        sample->frame_count > SIZE_MAX / sizeof(int16_t) ||
        (sample->loop_start != QA_AUDIO_NO_LOOP && sample->loop_start >= sample->frame_count)) {
        mixer_error(error, QA_ERROR_ARGUMENT,
                    "Effects require nonempty mono PCM with a valid loop marker");
        return NULL;
    }
    if (mixer->prepared_index_capacity) {
        size_t bucket = prepared_hash(sample, asset, q3) & (mixer->prepared_index_capacity - 1);
        for (qa_mixer_prepared *prepared = mixer->prepared_index[bucket]; prepared;
             prepared = prepared->next)
            if (prepared->sample == sample && prepared->asset == asset && prepared->q3 == q3)
                return prepared_retain(prepared);
    }
    size_t slot = SIZE_MAX;
    for (size_t i = 0; i < mixer->prepared_count; i++) {
        if (!mixer->prepared[i]) {
            slot = i;
            break;
        }
    }
    qa_audio_sample *pcm = NULL;
    bool ready = asset ? qa_audio_asset_resample(asset, mixer->options.sample_rate,
                                                q3 ? QA_GAME_Q3 : QA_GAME_Q1, &pcm, error) :
                         qa_audio_resample_source(sample, mixer->options.sample_rate,
                                                 q3 ? QA_GAME_Q3 : QA_GAME_Q1, &pcm, error);
    if (!ready)
        return NULL;
    if ((!q3 && !pcm->frame_count) || pcm->frame_count > INT64_MAX) {
        qa_audio_sample_release(pcm);
        mixer_error(error, QA_ERROR_ARGUMENT, "Resampled effect length is outside the audio clock");
        return NULL;
    }
    qa_mixer_prepared *prepared = calloc(1, sizeof(*prepared));
    if (!prepared) {
        qa_audio_sample_release(pcm);
        mixer_error(error, QA_ERROR_MEMORY, "Cannot allocate prepared effect");
        return NULL;
    }
    if (!qa_audio_sample_retain(sample)) {
        qa_audio_sample_release(pcm);
        free(prepared);
        mixer_error(error, QA_ERROR_MEMORY, "Audio sample reference count is exhausted");
        return NULL;
    }
    if (asset && !qa_audio_asset_retain(asset)) {
        qa_audio_sample_release(sample);
        qa_audio_sample_release(pcm);
        free(prepared);
        mixer_error(error, QA_ERROR_MEMORY, "Audio asset reference count is exhausted");
        return NULL;
    }
    if (slot == SIZE_MAX) {
        if (mixer->prepared_count == SIZE_MAX) {
            qa_audio_asset_release(asset);
            qa_audio_sample_release(sample);
            qa_audio_sample_release(pcm);
            free(prepared);
            mixer_error(error, QA_ERROR_MEMORY, "Too many prepared effects");
            return NULL;
        }
        qa_mixer_prepared **items = reserve(mixer->prepared, &mixer->prepared_capacity,
                                            mixer->prepared_count + 1, sizeof(*items), error);
        if (!items) {
            qa_audio_asset_release(asset);
            qa_audio_sample_release(sample);
            qa_audio_sample_release(pcm);
            free(prepared);
            return NULL;
        }
        mixer->prepared = items;
        slot = mixer->prepared_count;
    }
    if (!prepared_index_reserve(mixer, slot == mixer->prepared_count ? slot + 1 :
                                   mixer->prepared_count, error)) {
        qa_audio_asset_release(asset);
        qa_audio_sample_release(sample);
        qa_audio_sample_release(pcm);
        free(prepared);
        return NULL;
    }
    if (slot == mixer->prepared_count)
        mixer->prepared_count++;
    prepared->sample = sample;
    prepared->pcm = pcm;
    prepared->asset = asset;
    prepared->q3 = q3;
    prepared->slot = slot;
    mixer->prepared[slot] = prepared;
    size_t bucket = prepared_hash(sample, asset, q3) & (mixer->prepared_index_capacity - 1);
    prepared->next = mixer->prepared_index[bucket];
    mixer->prepared_index[bucket] = prepared;
    return prepared_retain(prepared);
}

static int16_t effect_sample(const qa_mixer_prepared *prepared, uint64_t frame) {
    return frame < prepared->pcm->frame_count ? prepared->pcm->samples[(size_t)frame] : 0;
}

bool qa_mixer_prepared_doppler(qa_mixer_prepared *prepared, qa_error *error) {
    if (prepared->doppler_sums)
        return true;
    if (prepared->pcm->frame_count > SIZE_MAX - QA_MIXER_CHUNK_FRAMES)
        return mixer_error(error, QA_ERROR_MEMORY, "Doppler sample period overflows storage");
    size_t period = ((size_t)prepared->pcm->frame_count + QA_MIXER_CHUNK_FRAMES - 1) /
                    QA_MIXER_CHUNK_FRAMES * QA_MIXER_CHUNK_FRAMES;
    if (period >= SIZE_MAX / sizeof(double))
        return mixer_error(error, QA_ERROR_MEMORY, "Doppler sample sums overflow storage");
    double *sums = malloc((period + 1) * sizeof(*sums));
    if (!sums)
        return mixer_error(error, QA_ERROR_MEMORY, "Cannot allocate Doppler sample sums");
    sums[0] = 0;
    for (size_t i = 0; i < period; i++)
        sums[i + 1] = sums[i] + (i < prepared->pcm->frame_count ? effect_sample(prepared, i) : 0);
    prepared->doppler_sums = sums;
    prepared->doppler_period = period;
    return true;
}

static qa_vec3 actor_position(const qa_audio_mixer *mixer, uint64_t actor, uint64_t owner,
                              bool scoped) {
    qa_vec3 fallback = qa_v3(0, 0, 0);
    for (size_t i = 0; i < mixer->position_count; i++) {
        const qa_mixer_position *position = &mixer->positions[i];
        if (position->actor != actor)
            continue;
        if (!position->scoped)
            fallback = position->origin;
        else if (scoped && position->owner == owner)
            return position->origin;
    }
    return fallback;
}

static qa_vec3 sound_position(const qa_audio_mixer *mixer, const qa_audio_play *sound) {
    switch (sound->origin_kind) {
    case QA_AUDIO_LOCAL:
        return mixer->listener.origin;
    case QA_AUDIO_FIXED:
        return sound->origin;
    case QA_AUDIO_ACTOR:
        return actor_position(mixer, sound->origin_actor, sound->owner,
                              sound->family == QA_GAME_Q3 && sound->owner != QA_AUDIO_NO_OWNER);
    }
    return qa_v3(0, 0, 0);
}

static bool transmit(qa_audio_mixer *mixer, qa_vec3 origin, qa_mixer_gain gain,
    qa_mixer_gain *out, qa_error *error) {
    if ((!mixer->transmission && !mixer->transmission_checked) ||
        (gain.left == 0 && gain.right == 0)) {
        *out = gain;
        return true;
    }
    float transmission = 1;
    size_t i;
    for (i = 0; i < mixer->transmission_count; i++) {
        qa_mixer_transmission *cached = &mixer->transmissions[i];
        if (cached->origin.x == origin.x && cached->origin.y == origin.y &&
            cached->origin.z == origin.z) {
            transmission = cached->gain;
            break;
        }
    }
    if (i == mixer->transmission_count) {
        mixer->callback_active = true;
        bool ok = true;
        if (mixer->transmission_checked)
            ok = mixer->transmission_checked(mixer->transmission_user, &mixer->listener,
                origin, &transmission, error);
        else
            transmission = mixer->transmission(mixer->transmission_user, &mixer->listener, origin);
        mixer->callback_active = false;
        if (!ok) {
            if (!error || error->code==QA_OK)
                mixer_error(error,QA_ERROR_ARGUMENT,"Actual scene transmission failed");
            return false;
        }
        if (mixer->destroy_requested || mixer->destroying)
            return mixer_error(error, QA_ERROR_ARGUMENT, "Audio owner retired during scene transmission");
        if (mixer->transmission_checked &&
            (!isfinite(transmission) || transmission < 0 || transmission > 1))
            return mixer_error(error, QA_ERROR_ARGUMENT, "Scene transmission returned an invalid gain");
        if (!isfinite(transmission))
            transmission = 1;
        transmission = fmaxf(0, fminf(1, transmission));
        if (i < mixer->transmission_capacity)
            mixer->transmissions[mixer->transmission_count++] =
                (qa_mixer_transmission){origin, transmission};
    }
    if (transmission != 1) {
        gain.left = trunc(gain.left * transmission);
        gain.right = trunc(gain.right * transmission);
    }
    *out = gain;
    return true;
}

static bool spatialize_q3(qa_audio_mixer *mixer, qa_vec3 origin, double volume,
    qa_mixer_gain *out, qa_error *error) {
    qa_vec3 delta = qa_vec_sub(origin, mixer->listener.origin);
    float distance = qa_vec_length(delta);
    qa_vec3 direction = qa_vec_normalize(delta);
    float pan = -qa_vec_dot(direction, mixer->listener.axis[1]);
    float loss = fmaxf(0, distance - 80.0f) * 0.0008f;
    float left =
        mixer->options.output_channels == 1 ? 1 : fmaxf(0, (float)(0.5 * (1 - (double)pan)));
    float right =
        mixer->options.output_channels == 1 ? 1 : fmaxf(0, (float)(0.5 * (1 + (double)pan)));
    qa_mixer_gain gain = {fmax(0, truncf((float)(volume * (float)((1 - (double)loss) * left)))),
                          fmax(0, truncf((float)(volume * (float)((1 - (double)loss) * right))))};
    return transmit(mixer, origin, gain, out, error);
}

static bool spatialize_policy(qa_audio_mixer *mixer, const qa_audio_play *sound,
                                       double volume, double attenuation, double offset,
                                       double scale, bool unattenuated_mono, bool personal,
                                       qa_mixer_gain *out, qa_error *error) {
    if (personal && (sound->origin_kind == QA_AUDIO_LOCAL ||
                     (sound->actor != QA_AUDIO_NO_ACTOR && sound->actor == mixer->listener.actor)))
    {
        *out = (qa_mixer_gain){volume, volume};
        return true;
    }
    qa_vec3 position = sound_position(mixer, sound);
    qa_vec3 delta = qa_vec_sub(position, mixer->listener.origin);
    double distance = qa_vec_length(delta);
    double pan = distance == 0 ? 0 : -qa_vec_dot(delta, mixer->listener.axis[1]) / distance;
    double gain = volume * (1 - fmax(0, distance - offset) * attenuation);
    bool mono = mixer->options.output_channels == 1 || (unattenuated_mono && attenuation == 0);
    qa_mixer_gain result = {fmax(0, trunc(gain * (mono ? 1 : scale * (1 - pan)))),
                            fmax(0, trunc(gain * (mono ? 1 : scale * (1 + pan))))};
    if (attenuation != 0) return transmit(mixer, position, result, out, error);
    *out = result;
    return true;
}

static bool spatialize_voice(qa_audio_mixer *mixer, const qa_mixer_voice *voice,
    qa_mixer_gain *out, qa_error *error) {
    if (voice->role == QA_MIXER_AMBIENT) {
        *out = voice->gain;
        return true;
    }
    if (voice->prepared->q3) {
        if (voice->sound.origin_kind == QA_AUDIO_LOCAL ||
            (voice->sound.actor != QA_AUDIO_NO_ACTOR &&
             voice->sound.actor == mixer->listener.actor))
        {
            *out = (qa_mixer_gain){voice->volume, voice->volume};
            return true;
        }
        return spatialize_q3(mixer, sound_position(mixer, &voice->sound), voice->volume, out, error);
    }
    return spatialize_policy(mixer, &voice->sound, voice->volume, voice->attenuation,
                             voice->distance_offset, voice->stereo_scale, voice->unattenuated_mono,
                             true, out, error);
}

static bool reserve_transmission(qa_audio_mixer *mixer, size_t needed, qa_error *error) {
    if ((!mixer->transmission && !mixer->transmission_checked) ||
        needed <= mixer->transmission_capacity)
        return true;
    qa_mixer_transmission *items =
        reserve(mixer->transmissions, &mixer->transmission_capacity, needed, sizeof(*items), error);
    if (!items)
        return false;
    mixer->transmissions = items;
    return true;
}

static bool reserve_notifications(qa_audio_mixer *mixer, qa_error *error) {
    if (!mixer->options.observer || mixer->event_free_count >= 2)
        return true;
    if (mixer->event_count > SIZE_MAX - 2)
        return mixer_error(error, QA_ERROR_MEMORY, "Audio notification allocation overflow");
    qa_mixer_event *events =
        reserve(mixer->events, &mixer->event_capacity,
                mixer->event_count + 2 - mixer->event_free_count, sizeof(*events), error);
    if (!events)
        return false;
    mixer->events = events;
    while (mixer->event_count < mixer->event_capacity) {
        size_t index = mixer->event_count++;
        mixer->events[index].next = mixer->event_free;
        mixer->event_free = index;
        mixer->event_free_count++;
    }
    return true;
}

static size_t claim_event(qa_audio_mixer *mixer) {
    size_t index = mixer->event_free;
    mixer->event_free = mixer->events[index].next;
    mixer->event_free_count--;
    return index;
}

static void release_event(qa_audio_mixer *mixer, size_t index) {
    if (index == SIZE_MAX)
        return;
    mixer->events[index].next = mixer->event_free;
    mixer->event_free = index;
    mixer->event_free_count++;
}

static void notify_voice(qa_audio_mixer *mixer, qa_mixer_voice *voice, bool started,
                         qa_audio_stop_reason reason, int64_t frame) {
    if (voice->role != QA_MIXER_EFFECT)
        return;
    if (started) {
        if (voice->notification != QA_MIXER_UNANNOUNCED)
            return;
        voice->notification = QA_MIXER_ANNOUNCED;
    } else {
        if (voice->notification != QA_MIXER_ANNOUNCED)
            return;
        voice->notification = QA_MIXER_FINISHED;
    }
    if (mixer->options.observer) {
        size_t index = started ? voice->start_event : voice->stop_event;
        if (started)
            voice->start_event = SIZE_MAX;
        else
            voice->stop_event = SIZE_MAX;
        mixer->events[index].event = (qa_audio_voice_event){
            .started = started,
            .seat = mixer->listener.seat,
            .voice_id = voice->id,
            .resource_id = voice->prepared->asset
                               ? qa_resource_id(qa_audio_asset_resource(voice->prepared->asset))
                               : voice->sound.resource_id,
            .output_frame = frame,
            .sample_rate = mixer->options.sample_rate,
            .source_offset_seconds =
                (double)fmaxl(0, (long double)frame - voice->start) / mixer->options.sample_rate,
            .sample = voice->prepared->sample,
            .asset = voice->prepared->asset,
            .reason = reason};
        mixer->events[index].prepared = prepared_retain(voice->prepared);
        mixer->events[index].next = SIZE_MAX;
        if (mixer->event_tail == SIZE_MAX)
            mixer->event_head = index;
        else
            mixer->events[mixer->event_tail].next = index;
        mixer->event_tail = index;
    }
}

static void free_voice(qa_audio_mixer *mixer, size_t index, qa_audio_stop_reason reason) {
    qa_mixer_voice *voice = &mixer->voices[index];
    if (voice->state == QA_MIXER_FREE)
        return;
    notify_voice(mixer, voice, false, reason, mixer->paint_time);
    if (voice->role == QA_MIXER_EFFECT && mixer->options.observer) {
        release_event(mixer, voice->start_event);
        release_event(mixer, voice->stop_event);
    }
    prepared_release(mixer, voice->prepared);
    memset(voice, 0, sizeof(*voice));
    voice->next_free = mixer->free_head;
    mixer->free_head = index;
}

static bool reserve_voice(qa_audio_mixer *mixer, qa_error *error) {
    if (mixer->free_head != SIZE_MAX)
        return true;
    if (mixer->voice_count == SIZE_MAX)
        return mixer_error(error, QA_ERROR_MEMORY, "Audio voice count overflow");
    qa_mixer_voice *items = reserve(mixer->voices, &mixer->voice_capacity, mixer->voice_count + 1,
                                    sizeof(*items), error);
    if (!items)
        return false;
    mixer->voices = items;
    return true;
}

static size_t insert_voice(qa_audio_mixer *mixer, qa_mixer_voice voice) {
    voice.start_event = voice.stop_event = SIZE_MAX;
    if (voice.role == QA_MIXER_EFFECT && mixer->options.observer) {
        voice.start_event = claim_event(mixer);
        voice.stop_event = claim_event(mixer);
    }
    size_t index = mixer->free_head;
    if (index == SIZE_MAX)
        index = mixer->voice_count++;
    else
        mixer->free_head = mixer->voices[index].next_free;
    mixer->voices[index] = voice;
    return index;
}

static void replace_channel(qa_audio_mixer *mixer, uint64_t actor, uint64_t owner, uint64_t channel,
                            bool replace_actor, bool scheduled, qa_audio_stop_reason reason) {
    for (size_t i = 0; i < mixer->voice_count; i++) {
        qa_mixer_voice *voice = &mixer->voices[i];
        if (voice->state == QA_MIXER_FREE || voice->role != QA_MIXER_EFFECT ||
            (!scheduled && voice->state == QA_MIXER_SCHEDULED) || voice->sound.actor != actor ||
            voice->sound.owner != owner)
            continue;
        if (replace_actor || (channel && voice->channel == channel)) {
            free_voice(mixer, i, reason);
            if (replace_actor)
                return;
        }
    }
}

static uint32_t random_value(qa_audio_mixer *mixer) {
    if (mixer->options.random) {
        mixer->callback_active = true;
        uint32_t value = mixer->options.random(mixer->options.random_user);
        mixer->callback_active = false;
        return value;
    }
    uint32_t value = mixer->random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return mixer->random_state = value;
}

static void clear_loop_mixes(qa_audio_mixer *mixer) {
    for (size_t i = 0; i < mixer->loop_mix_count; i++)
        prepared_release(mixer, mixer->loop_mixes[i].prepared);
    mixer->loop_mix_count = 0;
}

bool qa_audio_mixer_create(const qa_audio_mixer_options *options, qa_audio_mixer **out,
                           qa_error *error) {
    if (!options || !out || !options->sample_rate ||
        (options->output_channels != 1 && options->output_channels != 2))
        return mixer_error(error, QA_ERROR_ARGUMENT, "Invalid audio mixer format");
    qa_audio_mixer *mixer = calloc(1, sizeof(*mixer));
    if (!mixer)
        return mixer_error(error, QA_ERROR_MEMORY, "Cannot allocate audio mixer");
    mixer->options = *options;
    mixer->listener.actor = QA_AUDIO_NO_ACTOR;
    mixer->listener.axis[0] = qa_v3(1, 0, 0);
    mixer->listener.axis[1] = qa_v3(0, 1, 0);
    mixer->listener.axis[2] = qa_v3(0, 0, 1);
    mixer->listener.gain = 1;
    mixer->effects_gain = 0.8f;
    mixer->doppler_enabled = true;
    mixer->enabled = true;
    mixer->random_state = 0x6d2b79f5u;
    mixer->free_head = SIZE_MAX;
    mixer->event_free = mixer->event_head = mixer->event_tail = SIZE_MAX;
    if (options->initial_voices) {
        mixer->voices = reserve(NULL, &mixer->voice_capacity, options->initial_voices,
                                sizeof(*mixer->voices), error);
        if (!mixer->voices) {
            free(mixer);
            return false;
        }
        mixer->voice_count = options->initial_voices;
        for (size_t i = 0; i < mixer->voice_count; i++) {
            mixer->voices[i] = (qa_mixer_voice){.next_free = mixer->free_head};
            mixer->free_head = i;
        }
    }
    *out = mixer;
    return true;
}

void qa_audio_mixer_destroy(qa_audio_mixer *mixer) {
    if (mixer && mixer->round_locked) {
        mixer->round_destroy_requested = true;
        return;
    }
    if (!mixer || mixer->callback_active)
        return;
    if (mixer->dispatching) {
        mixer->destroy_requested = true;
        return;
    }
    if (mixer->destroying)
        return;
    if (mixer->destroy_requested) {
        mixer->options.observer = NULL;
        mixer->options.setting = NULL;
        mixer->options.log = NULL;
    }
    mixer->destroy_requested = false;
    mixer->destroying = true;
    qa_audio_mixer_stop_all(mixer);
    while (mixer->event_head != SIZE_MAX) {
        size_t index = mixer->event_head;
        mixer->event_head = mixer->events[index].next;
        prepared_release(mixer, mixer->events[index].prepared);
    }
    free(mixer->voices);
    free(mixer->prepared);
    free(mixer->prepared_index);
    free(mixer->loops);
    free(mixer->loop_mixes);
    free(mixer->positions);
    free(mixer->transmissions);
    free(mixer->events);
    free(mixer->diagnostic_message);
    free(mixer);
}

static void flush_notifications(qa_audio_mixer *mixer) {
    if (mixer->dispatching)
        return;
    mixer->dispatching = true;
    while (mixer->event_head != SIZE_MAX) {
        size_t index = mixer->event_head;
        qa_audio_voice_event event = mixer->events[index].event;
        qa_mixer_prepared *prepared = mixer->events[index].prepared;
        mixer->event_head = mixer->events[index].next;
        if (mixer->event_head == SIZE_MAX)
            mixer->event_tail = SIZE_MAX;
        release_event(mixer, index);
        if (mixer->options.observer)
            mixer->options.observer(mixer->options.observer_user, &event);
        prepared_release(mixer, prepared);
        if (mixer->destroy_requested)
            break;
    }
    mixer->dispatching = false;
    if (mixer->destroy_requested && !mixer->destroying)
        qa_audio_mixer_destroy(mixer);
}

bool qa_audio_mixer_position(qa_audio_mixer *mixer, uint64_t actor, qa_vec3 origin,
                             qa_error *error) {
    if (!allow_mutation(mixer, error))
        return false;
    if (actor == QA_AUDIO_NO_ACTOR || !qa_vec_finite(origin))
        return mixer_error(error, QA_ERROR_ARGUMENT, "Invalid audio actor position");
    bool found = false;
    for (size_t i = 0; i < mixer->position_count; i++)
        if (mixer->positions[i].actor == actor && !mixer->positions[i].scoped)
            found = true;
    if (!found) {
        if (mixer->position_count == SIZE_MAX)
            return mixer_error(error, QA_ERROR_MEMORY, "Audio position count overflow");
        qa_mixer_position *items = reserve(mixer->positions, &mixer->position_capacity,
                                           mixer->position_count + 1, sizeof(*items), error);
        if (!items)
            return false;
        mixer->positions = items;
        mixer->positions[mixer->position_count++] =
            (qa_mixer_position){.actor = actor, .origin = origin};
    }
    for (size_t i = 0; i < mixer->position_count; i++)
        if (mixer->positions[i].actor == actor)
            mixer->positions[i].origin = origin;
    return true;
}

bool qa_audio_mixer_position_owner(qa_audio_mixer *mixer, uint64_t actor, uint64_t owner,
                                   qa_vec3 origin, qa_error *error) {
    if (!allow_mutation(mixer, error))
        return false;
    if (actor == QA_AUDIO_NO_ACTOR || !qa_vec_finite(origin))
        return mixer_error(error, QA_ERROR_ARGUMENT, "Invalid private audio actor position");
    for (size_t i = 0; i < mixer->position_count; i++) {
        qa_mixer_position *position = &mixer->positions[i];
        if (position->actor == actor && position->scoped == (owner != QA_AUDIO_NO_OWNER) &&
            (!position->scoped || position->owner == owner)) {
            position->origin = origin;
            return true;
        }
    }
    if (mixer->position_count == SIZE_MAX)
        return mixer_error(error, QA_ERROR_MEMORY, "Audio position count overflow");
    qa_mixer_position *items = reserve(mixer->positions, &mixer->position_capacity,
                                       mixer->position_count + 1, sizeof(*items), error);
    if (!items)
        return false;
    mixer->positions = items;
    mixer->positions[mixer->position_count++] = (qa_mixer_position){
        .actor = actor, .owner = owner, .origin = origin, .scoped = owner != QA_AUDIO_NO_OWNER};
    return true;
}

void qa_audio_mixer_geometry(qa_audio_mixer *mixer, qa_audio_transmission_fn fn, void *user) {
    if (!mixer || mixer->callback_active || mixer->round_locked)
        return;
    mixer->transmission = fn;
    mixer->transmission_checked = NULL;
    mixer->transmission_user = user;
    mixer->transmission_count = 0;
}

bool qa_audio_mixer_geometry_checked(qa_audio_mixer *mixer,
    qa_audio_transmission_checked_fn fn, void *user, qa_error *error) {
    if (!allow_mutation(mixer, error)) return false;
    mixer->transmission = NULL;
    mixer->transmission_checked = fn;
    mixer->transmission_user = user;
    mixer->transmission_count = 0;
    return true;
}

void qa_audio_mixer_effects_gain(qa_audio_mixer *mixer, float gain) {
    double scaled = gain * 255.0f;
    if (mixer && !mixer->callback_active && !mixer->round_locked && isfinite(gain) && scaled >= INT32_MIN &&
        scaled <= INT32_MAX)
        mixer->effects_gain = gain;
}

void qa_audio_mixer_doppler(qa_audio_mixer *mixer, bool enabled) {
    if (mixer && !mixer->callback_active && !mixer->round_locked)
        mixer->doppler_enabled = enabled;
}

static uint64_t voice_id(qa_audio_mixer *mixer) {
    if (!mixer->options.allocate_voice_id)
        return ++mixer->next_voice;
    mixer->callback_active = true;
    uint64_t id = mixer->options.allocate_voice_id(mixer->options.voice_id_user);
    mixer->callback_active = false;
    return id;
}

static int32_t allocation_time(qa_audio_mixer *mixer, int32_t fallback) {
    if (!mixer->options.milliseconds)
        return fallback;
    mixer->callback_active = true;
    int32_t milliseconds = mixer->options.milliseconds(mixer->options.milliseconds_user);
    mixer->callback_active = false;
    return milliseconds;
}

bool qa_audio_mixer_play(qa_audio_mixer *mixer, const qa_audio_play *sound, int32_t milliseconds,
                         bool *accepted, qa_error *error) {
    if (accepted)
        *accepted = false;
    if (!allow_mutation(mixer, error))
        return false;
    if (!mixer->enabled)
        return true;
    if (!validate_play(sound, error))
        return false;
    if (!selected(mixer, sound->audience))
        return true;
    if (!mixer->options.allocate_voice_id && mixer->next_voice == UINT64_MAX)
        return mixer_error(error, QA_ERROR_ARGUMENT, "Audio voice identity space is exhausted");
    qa_mixer_prepared *prepared =
        prepare(mixer, sound->sample, sound->asset, sound->family == QA_GAME_Q3, error);
    if (!prepared)
        return false;
    if (sound->family == QA_GAME_Q3 && setting(mixer, "s_show") == 1) {
        const char *name = sound->name    ? sound->name
                           : sound->asset ? qa_audio_asset_name(sound->asset)
                                          : "<unnamed>";
        size_t length = strlen(name);
        if (!mixer->options.log || length > SIZE_MAX - 64) {
            prepared_release(mixer, prepared);
            return mixer_error(error, QA_ERROR_ARGUMENT,
                               "Sound diagnostics require a log callback and valid name");
        }
        char *message =
            reserve(mixer->diagnostic_message, &mixer->diagnostic_capacity, length + 64, 1, error);
        if (!message) {
            prepared_release(mixer, prepared);
            return false;
        }
        mixer->diagnostic_message = message;
        snprintf(message, mixer->diagnostic_capacity, "%" PRId64 " : %s\n", mixer->paint_time,
                 name);
        log_message(mixer, message);
    }
    int32_t allocated_at =
        sound->family == QA_GAME_Q3 ? allocation_time(mixer, milliseconds) : milliseconds;
    if (sound->family == QA_GAME_Q3) {
        uint64_t actor =
            sound->origin_kind == QA_AUDIO_LOCAL ? mixer->listener.actor : sound->actor;
        size_t same = 0;
        for (size_t i = 0; i < mixer->voice_count; i++) {
            const qa_mixer_voice *voice = &mixer->voices[i];
            if (voice->state == QA_MIXER_FREE || voice->role != QA_MIXER_EFFECT ||
                !voice->prepared->q3 || voice->sound.actor != actor ||
                voice->sound.owner != sound->owner || voice->prepared->sample != sound->sample)
                continue;
            uint32_t difference = (uint32_t)allocated_at - (uint32_t)voice->allocated_at;
            int64_t elapsed = difference <= INT32_MAX ? (int64_t)difference
                                                      : (int64_t)difference - INT64_C(4294967296);
            if (elapsed < 50) {
                prepared_release(mixer, prepared);
                return true;
            }
            same++;
        }
        size_t allowed = sound->origin_kind == QA_AUDIO_LOCAL ||
                                 (actor != QA_AUDIO_NO_ACTOR && actor == mixer->listener.actor)
                             ? 8
                             : 4;
        if (same > allowed) {
            prepared_release(mixer, prepared);
            return true;
        }
    }
    if (!reserve_voice(mixer, error) || !reserve_notifications(mixer, error) ||
        mixer->transmission_count == SIZE_MAX ||
        !reserve_transmission(mixer, mixer->transmission_count + 1, error)) {
        prepared_release(mixer, prepared);
        return false;
    }
    qa_mixer_voice voice = {
        .state = QA_MIXER_STARTED,
        .role = QA_MIXER_EFFECT,
        .prepared = prepared,
        .sound = *sound,
        .channel = channel_name(sound->family, sound->channel),
        .loop_start = prepared->pcm->loop_start,
        .allocated_at = allocated_at,
        .start = mixer->paint_time,
        .volume = trunc((double)sound->volume * (sound->family == QA_GAME_Q3 ? 127 : 255)),
        .stereo_scale = 1};
    voice.sound.name = NULL;
    if (voice.sound.origin_kind == QA_AUDIO_LOCAL)
        voice.sound.actor = mixer->listener.actor;
    if (sound->family == QA_GAME_Q3) {
        voice.state = QA_MIXER_PENDING;
        voice.loop_start = QA_AUDIO_NO_LOOP;
        voice.gain = (qa_mixer_gain){voice.volume, voice.volume};
    } else if (sound->family == QA_GAME_Q1) {
        voice.attenuation = (double)sound->attenuation / 1000;
        for (size_t i = 0; i < mixer->voice_count; i++) {
            const qa_mixer_voice *other = &mixer->voices[i];
            if (other->state == QA_MIXER_STARTED && other->role == QA_MIXER_EFFECT &&
                !other->prepared->q3 && other->prepared->sample == sound->sample &&
                other->start == mixer->paint_time) {
                uint64_t range = mixer->options.sample_rate / 10;
                if (!range)
                    range = 1;
                uint64_t offset = random_value(mixer) % range;
                if (offset >= prepared->pcm->frame_count)
                    offset = prepared->pcm->frame_count - 1;
                if (offset > (uint64_t)mixer->paint_time - (uint64_t)INT64_MIN) {
                    prepared_release(mixer, prepared);
                    return mixer_error(error, QA_ERROR_ARGUMENT,
                                       "Sound start precedes the audio clock range");
                }
                voice.start -= (int64_t)offset;
                break;
            }
        }
        voice.allocated_at = allocation_time(mixer, milliseconds);
        if (!spatialize_voice(mixer, &voice, &voice.gain, error)) {
            prepared_release(mixer, prepared);
            return false;
        }
        if (voice.gain.left == 0 && voice.gain.right == 0) {
            prepared_release(mixer, prepared);
            return true;
        }
    } else {
        double server = sound->has_server_time
                            ? sound->server_milliseconds * 0.001 * mixer->options.sample_rate
                            : (double)mixer->paint_time;
        double offset = mixer->source_begin_offset;
        double begin = trunc(server + offset);
        if (begin < (double)mixer->paint_time) {
            begin = (double)mixer->paint_time;
            offset = trunc(begin - server);
        } else if (begin > (double)mixer->paint_time + 0.3 * mixer->options.sample_rate) {
            begin = trunc((double)mixer->paint_time + 0.1 * mixer->options.sample_rate);
            offset = trunc(begin - server);
        } else
            offset -= 10;
        begin = sound->delay_seconds == 0
                    ? (double)mixer->paint_time
                    : trunc(begin + sound->delay_seconds * mixer->options.sample_rate);
        if ((sound->delay_seconds != 0 && (!isfinite(begin) || (long double)begin < INT64_MIN ||
                                           (long double)begin > INT64_MAX)) ||
            !isfinite(offset) || mixer->schedule_order == UINT64_MAX) {
            prepared_release(mixer, prepared);
            return mixer_error(error, QA_ERROR_ARGUMENT,
                               "Sound deadline is outside the audio clock");
        }
        voice.allocated_at = allocation_time(mixer, milliseconds);
        voice.state = QA_MIXER_SCHEDULED;
        voice.start = sound->delay_seconds == 0 ? mixer->paint_time : (int64_t)begin;
        voice.order = mixer->schedule_order++;
        voice.attenuation = (double)sound->attenuation * (sound->attenuation == 3 ? 0.001 : 0.0005);
        voice.distance_offset = 80;
        voice.stereo_scale = 0.5;
        voice.unattenuated_mono = true;
        mixer->source_begin_offset = offset;
    }
    if (voice.state != QA_MIXER_SCHEDULED)
        replace_channel(mixer, voice.sound.actor, sound->owner, voice.channel, sound->channel == -1,
                        false, QA_AUDIO_REPLACED);
    if (sound->family == QA_GAME_Q3 && mixer->free_head != SIZE_MAX)
        voice.allocated_at = allocation_time(mixer, milliseconds);
    voice.id = voice_id(mixer);
    size_t index = insert_voice(mixer, voice);
    if (voice.state == QA_MIXER_STARTED)
        notify_voice(mixer, &mixer->voices[index], true, QA_AUDIO_ENDED, mixer->paint_time);
    if (accepted)
        *accepted = true;
    flush_notifications(mixer);
    return true;
}

static int compare_loops(const void *left, const void *right) {
    const qa_audio_play *a = &((const qa_mixer_loop *)left)->request.sound;
    const qa_audio_play *b = &((const qa_mixer_loop *)right)->request.sound;
    if (a->actor != b->actor)
        return a->actor < b->actor ? -1 : 1;
    if (a->owner != b->owner)
        return a->owner < b->owner ? -1 : 1;
    return (int)a->family - (int)b->family;
}

bool qa_audio_mixer_loop(qa_audio_mixer *mixer, const qa_audio_loop *request, qa_error *error) {
    if (!allow_mutation(mixer, error))
        return false;
    if (!mixer->enabled)
        return true;
    if (!request)
        return mixer_error(error, QA_ERROR_ARGUMENT, "Missing looping sound");
    if (!validate_play(&request->sound, error))
        return false;
    if (request->sound.actor == QA_AUDIO_NO_ACTOR || !qa_vec_finite(request->velocity))
        return mixer_error(error, QA_ERROR_ARGUMENT, "Invalid loop actor or velocity");
    if (!selected(mixer, request->sound.audience))
        return true;
    qa_mixer_prepared *prepared = prepare(mixer, request->sound.sample, request->sound.asset,
                                          request->sound.family == QA_GAME_Q3, error);
    if (!prepared)
        return false;
    if (!prepared->pcm->frame_count) {
        prepared_release(mixer, prepared);
        return mixer_error(error, QA_ERROR_ARGUMENT, "Loop sound resamples to zero frames");
    }
    size_t index;
    for (index = 0; index < mixer->loop_count; index++) {
        const qa_audio_play *other = &mixer->loops[index].request.sound;
        if (other->actor == request->sound.actor && other->owner == request->sound.owner &&
            other->family == request->sound.family)
            break;
    }
    qa_mixer_loop loop = {.request = *request,
                          .prepared = prepared,
                          .active = true,
                          .doppler_scale = 1,
                          .old_doppler_scale = 1};
    loop.request.sound.name = NULL;
    qa_vec3 origin = sound_position(mixer, &request->sound);
    if (request->sound.family == QA_GAME_Q3 && !request->persistent && mixer->doppler_enabled &&
        setting(mixer, "s_doppler") && qa_vec_dot(request->velocity, request->velocity) > 0) {
        qa_vec3 listener =
            request->sound.owner == QA_AUDIO_NO_OWNER &&
                    request->sound.actor == mixer->listener.actor
                ? origin
                : actor_position(mixer, mixer->listener.actor, QA_AUDIO_NO_OWNER, false);
        qa_vec3 before = qa_vec_sub(listener, origin);
        qa_vec3 after = qa_vec_sub(listener, qa_vec_add(origin, request->velocity));
        float distance_before = qa_vec_dot(before, before);
        float distance_after = qa_vec_dot(after, after);
        loop.doppler_scale = distance_after / (distance_before * 100.0f);
        if (!isfinite(loop.doppler_scale))
            loop.doppler_scale = 1;
        loop.doppler = loop.doppler_scale > 1;
        if (loop.doppler_scale > QA_MIXER_CHUNK_FRAMES && !qa_mixer_prepared_doppler(prepared, error)) {
            prepared_release(mixer, prepared);
            return false;
        }
    } else if (request->persistent && index < mixer->loop_count) {
        loop.doppler_scale = mixer->loops[index].doppler_scale;
        loop.old_doppler_scale = mixer->loops[index].old_doppler_scale;
    }
    if (index == mixer->loop_count) {
        if (mixer->loop_count == SIZE_MAX) {
            prepared_release(mixer, prepared);
            return mixer_error(error, QA_ERROR_MEMORY, "Audio loop count overflow");
        }
        qa_mixer_loop *items = reserve(mixer->loops, &mixer->loop_capacity, mixer->loop_count + 1,
                                       sizeof(*items), error);
        if (!items) {
            prepared_release(mixer, prepared);
            return false;
        }
        mixer->loops = items;
    }
    if (request->sound.family == QA_GAME_Q3 &&
        !qa_audio_mixer_position_owner(mixer, request->sound.actor, request->sound.owner, origin,
                                       error)) {
        prepared_release(mixer, prepared);
        return false;
    }
    if (index < mixer->loop_count)
        prepared_release(mixer, mixer->loops[index].prepared);
    else
        mixer->loop_count++;
    mixer->loops[index] = loop;
    qsort(mixer->loops, mixer->loop_count, sizeof(*mixer->loops), compare_loops);
    return true;
}

static bool collect_loop_mixes(qa_audio_mixer *mixer, qa_error *error) {
    clear_loop_mixes(mixer);
    for (size_t i = 0; i < mixer->loop_count; i++) {
        qa_mixer_loop *loop = &mixer->loops[i];
        loop->merged = false;
        if (!loop->active)
            continue;
        const qa_audio_play *sound = &loop->request.sound;
        if (sound->family == QA_GAME_Q3) {
            double volume = trunc((double)sound->volume * (loop->request.persistent ? 90 : 127));
            if (!spatialize_q3(mixer,
                                       actor_position(mixer, sound->actor, sound->owner,
                                                      sound->owner != QA_AUDIO_NO_OWNER),
                                       volume, &loop->gain, error)) return false;
        } else {
            bool q1 = sound->family == QA_GAME_Q1;
            if (!spatialize_policy(mixer, sound, trunc((double)sound->volume * 255),
                                           sound->attenuation * (q1 ? 0.001 : 0.003), q1 ? 0 : 80,
                                           q1 ? 1 : 0.5, !q1, true, &loop->gain, error)) return false;
        }
    }
    for (size_t i = 0; i < mixer->loop_count; i++) {
        qa_mixer_loop *loop = &mixer->loops[i];
        if (!loop->active || loop->merged)
            continue;
        qa_game_family family = loop->request.sound.family;
        qa_mixer_gain gain = loop->gain;
        if (family != QA_GAME_Q1) {
            for (size_t j = i + 1; j < mixer->loop_count; j++) {
                qa_mixer_loop *candidate = &mixer->loops[j];
                if (!candidate->active || candidate->request.sound.family != family ||
                    candidate->prepared != loop->prepared ||
                    (family == QA_GAME_Q3 && candidate->doppler))
                    continue;
                candidate->merged = true;
                gain.left += candidate->gain.left;
                gain.right += candidate->gain.right;
            }
            gain.left = fmin(255, gain.left);
            gain.right = fmin(255, gain.right);
        }
        if (gain.left == 0 && gain.right == 0)
            continue;
        mixer->loop_mixes[mixer->loop_mix_count++] =
            (qa_mixer_loop_mix){.prepared = prepared_retain(loop->prepared),
                                .gain = gain,
                                .family = family,
                                .doppler = loop->doppler,
                                .doppler_scale = loop->doppler_scale,
                                .old_doppler_scale = loop->old_doppler_scale};
    }
    return true;
}

qa_vec3 qa_audio_mixer_listener_origin(const qa_audio_mixer *mixer) {
    return mixer->listener.origin;
}
bool qa_audio_mixer_listener(qa_audio_mixer *mixer, const qa_audio_listener *listener,
                             qa_error *error) {
    if (!allow_mutation(mixer, error))
        return false;
    mixer->transmission_count = 0;
    if (!mixer->enabled)
        return true;
    if (!listener || listener->seat == QA_AUDIO_WORLD || !qa_vec_finite(listener->origin) ||
        !qa_vec_finite(listener->axis[0]) || !qa_vec_finite(listener->axis[1]) ||
        !qa_vec_finite(listener->axis[2]) || !isfinite(listener->gain) || listener->gain < 0)
        return mixer_error(error, QA_ERROR_ARGUMENT, "Invalid audio listener");
    if (mixer->loop_count > SIZE_MAX - mixer->voice_count)
        return mixer_error(error, QA_ERROR_MEMORY, "Audio spatialization count overflow");
    if (!reserve_transmission(mixer, mixer->loop_count + mixer->voice_count, error))
        return false;
    if (mixer->loop_count > mixer->loop_mix_capacity) {
        qa_mixer_loop_mix *items = reserve(mixer->loop_mixes, &mixer->loop_mix_capacity,
                                           mixer->loop_count, sizeof(*items), error);
        if (!items)
            return false;
        mixer->loop_mixes = items;
    }
    mixer->listener = *listener;
    mixer->transmission_count = 0;
    for (size_t i = 0; i < mixer->voice_count; i++) {
        qa_mixer_voice *voice = &mixer->voices[i];
        if (voice->state != QA_MIXER_FREE && voice->state != QA_MIXER_SCHEDULED &&
            !spatialize_voice(mixer, voice, &voice->gain, error)) return false;
    }
    return collect_loop_mixes(mixer, error);
}

bool qa_audio_mixer_end_loop_frame(qa_audio_mixer *mixer, qa_error *error) {
    if (!mixer)
        return mixer_error(error, QA_ERROR_ARGUMENT, "Missing audio mixer");
    return qa_audio_mixer_listener(mixer, &mixer->listener, error);
}

void qa_audio_mixer_clear_loops(qa_audio_mixer *mixer, bool all) {
    if (!mixer || mixer->callback_active || mixer->round_locked)
        return;
    for (size_t i = 0; i < mixer->loop_count; i++)
        if (all || !mixer->loops[i].request.persistent)
            mixer->loops[i].active = false;
    clear_loop_mixes(mixer);
}

void qa_audio_mixer_stop_loop(qa_audio_mixer *mixer, uint64_t actor, uint64_t owner) {
    if (!mixer || mixer->callback_active || mixer->round_locked)
        return;
    for (size_t i = 0; i < mixer->loop_count; i++)
        if (mixer->loops[i].request.sound.actor == actor &&
            mixer->loops[i].request.sound.owner == owner)
            mixer->loops[i].active = false;
}

void qa_audio_mixer_clear_seat_loops(qa_audio_mixer *mixer, uint64_t owner, bool all) {
    if (!mixer || mixer->callback_active || mixer->round_locked)
        return;
    for (size_t i = 0; i < mixer->loop_count; i++) {
        qa_mixer_loop *loop = &mixer->loops[i];
        if (loop->request.sound.family == QA_GAME_Q3 &&
            loop->request.sound.audience != QA_AUDIO_WORLD && loop->request.sound.owner == owner &&
            (all || !loop->request.persistent))
            loop->active = false;
    }
}

void qa_audio_mixer_stop_seat_loop(qa_audio_mixer *mixer, uint64_t actor, uint64_t owner) {
    if (!mixer || mixer->callback_active || mixer->round_locked)
        return;
    for (size_t i = 0; i < mixer->loop_count; i++) {
        qa_mixer_loop *loop = &mixer->loops[i];
        if (loop->request.sound.family == QA_GAME_Q3 &&
            loop->request.sound.audience != QA_AUDIO_WORLD && loop->request.sound.actor == actor &&
            loop->request.sound.owner == owner)
            loop->active = false;
    }
}

void qa_audio_mixer_stop_channel(qa_audio_mixer *mixer, uint64_t actor, uint64_t owner,
                                 qa_game_family family, int32_t channel) {
    if (!mixer || mixer->callback_active || mixer->round_locked || !family_valid(family) ||
        (channel < 0 && !(family == QA_GAME_Q1 && channel == -1)))
        return;
    if (channel == 0) {
        for (size_t i = 0; i < mixer->voice_count; i++) {
            qa_mixer_voice *voice = &mixer->voices[i];
            if (voice->state != QA_MIXER_FREE && voice->role == QA_MIXER_EFFECT &&
                voice->sound.actor == actor && voice->sound.owner == owner && !voice->channel) {
                free_voice(mixer, i, QA_AUDIO_STOPPED);
                flush_notifications(mixer);
                return;
            }
        }
    } else
        replace_channel(mixer, actor, owner, channel_name(family, channel), channel == -1, true,
                        QA_AUDIO_STOPPED);
    flush_notifications(mixer);
}

void qa_audio_mixer_stop_actor(qa_audio_mixer *mixer, uint64_t actor, uint64_t owner) {
    if (!mixer || mixer->callback_active || mixer->round_locked)
        return;
    for (size_t i = 0; i < mixer->voice_count; i++)
        if (mixer->voices[i].state != QA_MIXER_FREE && mixer->voices[i].sound.actor == actor &&
            mixer->voices[i].sound.owner == owner)
            free_voice(mixer, i, QA_AUDIO_STOPPED);
    qa_audio_mixer_stop_loop(mixer, actor, owner);
    flush_notifications(mixer);
}

void qa_audio_mixer_stop_owner(qa_audio_mixer *mixer, uint64_t owner) {
    if (!mixer || mixer->callback_active || mixer->round_locked)
        return;
    for (size_t i = 0; i < mixer->voice_count; i++)
        if (mixer->voices[i].state != QA_MIXER_FREE && mixer->voices[i].sound.owner == owner)
            free_voice(mixer, i, QA_AUDIO_STOPPED);
    for (size_t i = 0; i < mixer->loop_count; i++)
        if (mixer->loops[i].request.sound.owner == owner)
            mixer->loops[i].active = false;
    flush_notifications(mixer);
}

static void stop_all_state(qa_audio_mixer *mixer) {
    for (size_t i = 0; i < mixer->voice_count; i++)
        free_voice(mixer, i, QA_AUDIO_STOPPED);
    mixer->free_head = SIZE_MAX;
    for (size_t i = 0; i < mixer->voice_count; i++) {
        mixer->voices[i].next_free = mixer->free_head;
        mixer->free_head = i;
    }
    clear_loop_mixes(mixer);
    for (size_t i = 0; i < mixer->loop_count; i++)
        prepared_release(mixer, mixer->loops[i].prepared);
    mixer->loop_count = 0;
    mixer->position_count = 0;
    mixer->transmission_count = 0;
    mixer->source_begin_offset = 0;
    mixer->schedule_order = 0;
    mixer->raw_end = mixer->paint_time;
    if (mixer->options.log && setting(mixer, "developer"))
        log_message(mixer, "Channel memory manager started\n");
}

void qa_audio_mixer_stop_all(qa_audio_mixer *mixer) {
    if (!mixer || mixer->callback_active || mixer->round_locked)
        return;
    stop_all_state(mixer);
    flush_notifications(mixer);
}

void qa_audio_mixer_round_stop(qa_audio_mixer *mixer) {
    if (!mixer || !mixer->round_locked) return;
    stop_all_state(mixer);
    flush_notifications(mixer);
}

static bool mixer_static(qa_audio_mixer *mixer, uint64_t key, qa_audio_sample *sample,
                           qa_audio_asset *asset, qa_vec3 origin, float volume, float attenuation, qa_error *error) {
    if (!allow_mutation(mixer, error))
        return false;
    if (!mixer->enabled)
        return true;
    if (!sample || sample->loop_start == QA_AUDIO_NO_LOOP || !qa_vec_finite(origin) ||
        !isfinite(volume) || volume < 0 || !isfinite(attenuation) || attenuation < 0)
        return mixer_error(error, QA_ERROR_ARGUMENT,
                           "Static sound requires looped PCM and valid gain");
    qa_mixer_prepared *prepared = prepare(mixer, sample, asset, false, error);
    if (!prepared)
        return false;
    if (!reserve_voice(mixer, error) ||
        !reserve_transmission(mixer, mixer->transmission_count + 1, error)) {
        prepared_release(mixer, prepared);
        return false;
    }
    qa_mixer_voice voice = {.state = QA_MIXER_STARTED,
                            .role = QA_MIXER_STATIC,
                            .prepared = prepared,
                            .sound = {.sample = sample,
                                      .family = QA_GAME_Q1,
                                      .actor = QA_AUDIO_NO_ACTOR,
                                      .owner = QA_AUDIO_NO_OWNER,
                                      .audience = mixer->listener.seat,
                                      .origin_kind = QA_AUDIO_FIXED,
                                      .origin = origin},
                            .key = key,
                            .loop_start = prepared->pcm->loop_start,
                            .start = mixer->paint_time,
                            .volume = trunc((double)volume / 255 * 255),
                            .attenuation = (double)attenuation / 64000,
                            .stereo_scale = 1};
    if (!spatialize_voice(mixer, &voice, &voice.gain, error)) {
        prepared_release(mixer, prepared);
        return false;
    }
    insert_voice(mixer, voice);
    return true;
}

bool qa_audio_mixer_static(qa_audio_mixer *mixer, uint64_t key, qa_audio_sample *sample,
                           qa_vec3 origin, float volume, float attenuation, qa_error *error) {
    return mixer_static(mixer, key, sample, NULL, origin, volume, attenuation, error);
}
bool qa_audio_mixer_static_asset(qa_audio_mixer *mixer, uint64_t key, qa_audio_asset *asset,
    qa_vec3 origin, float volume, float attenuation, qa_error *error) {
    if (!asset) return mixer_error(error, QA_ERROR_ARGUMENT, "Static sound lost its actual bank asset");
    return mixer_static(mixer, key, qa_audio_asset_sample(asset), asset, origin, volume, attenuation, error);
}

void qa_audio_mixer_remove_static(qa_audio_mixer *mixer, uint64_t key) {
    if (!mixer || mixer->callback_active || mixer->round_locked)
        return;
    for (size_t i = 0; i < mixer->voice_count; i++)
        if (mixer->voices[i].state != QA_MIXER_FREE && mixer->voices[i].role == QA_MIXER_STATIC &&
            mixer->voices[i].key == key)
            free_voice(mixer, i, QA_AUDIO_STOPPED);
}

bool qa_audio_mixer_static_read(const qa_audio_mixer *mixer, uint64_t key,
                                qa_audio_static_view *out) {
    if (!mixer || !key || !out || mixer->callback_active || mixer->dispatching ||
        mixer->destroy_requested || mixer->destroying)
        return false;
    for (size_t i = 0; i < mixer->voice_count; ++i) {
        const qa_mixer_voice *voice = mixer->voices + i;
        if (voice->state != QA_MIXER_FREE && voice->role == QA_MIXER_STATIC && voice->key == key) {
            *out = (qa_audio_static_view){voice->prepared->sample, voice->sound.origin,
                voice->volume, voice->attenuation};
            return true;
        }
    }
    return false;
}

bool qa_audio_mixer_ambient(qa_audio_mixer *mixer, qa_audio_sample *sounds[2],
                            const uint8_t levels[2], float elapsed_seconds, float level, float fade,
                            qa_error *error) {
    if (!allow_mutation(mixer, error))
        return false;
    if (!mixer->enabled)
        return true;
    if (!sounds || !levels || !isfinite(elapsed_seconds) || elapsed_seconds < 0 ||
        !isfinite(level) || level < 0 || !isfinite(fade) || fade < 0)
        return mixer_error(error, QA_ERROR_ARGUMENT, "Invalid ambient sound update");
    qa_mixer_prepared *prepared[2] = {NULL, NULL};
    size_t found[2] = {SIZE_MAX, SIZE_MAX};
    for (size_t i = 0; i < mixer->voice_count; i++)
        if (mixer->voices[i].state != QA_MIXER_FREE && mixer->voices[i].role == QA_MIXER_AMBIENT &&
            mixer->voices[i].key < 2)
            found[mixer->voices[i].key] = i;
    for (size_t i = 0; i < 2; i++)
        if (level != 0 && sounds[i] &&
            (found[i] == SIZE_MAX || mixer->voices[found[i]].prepared->sample != sounds[i])) {
            prepared[i] = prepare(mixer, sounds[i], NULL, false, error);
            if (!prepared[i]) {
                prepared_release(mixer, prepared[0]);
                return false;
            }
        }
    /* Reserve both possible admissions before replacing either ambient slot. */
    if (mixer->voice_count > SIZE_MAX - 2) {
        prepared_release(mixer, prepared[0]);
        prepared_release(mixer, prepared[1]);
        return mixer_error(error, QA_ERROR_MEMORY, "Ambient voice allocation overflow");
    }
    qa_mixer_voice *items = reserve(mixer->voices, &mixer->voice_capacity, mixer->voice_count + 2,
                                    sizeof(*items), error);
    if (!items) {
        prepared_release(mixer, prepared[0]);
        prepared_release(mixer, prepared[1]);
        return false;
    }
    mixer->voices = items;
    for (size_t key = 0; key < 2; key++) {
        size_t index = found[key];
        if (!sounds[key] || level == 0) {
            if (index != SIZE_MAX)
                free_voice(mixer, index, QA_AUDIO_STOPPED);
            continue;
        }
        if (prepared[key]) {
            if (index != SIZE_MAX)
                free_voice(mixer, index, QA_AUDIO_STOPPED);
            qa_mixer_voice voice = {.state = QA_MIXER_STARTED,
                                    .role = QA_MIXER_AMBIENT,
                                    .prepared = prepared[key],
                                    .key = key,
                                    .loop_start = 0,
                                    .start = mixer->paint_time,
                                    .sound = {.sample = sounds[key],
                                              .family = QA_GAME_Q1,
                                              .actor = QA_AUDIO_NO_ACTOR,
                                              .owner = QA_AUDIO_NO_OWNER,
                                              .audience = mixer->listener.seat,
                                              .origin_kind = QA_AUDIO_LOCAL}};
            index = insert_voice(mixer, voice);
        }
        qa_mixer_voice *voice = &mixer->voices[index];
        double target = (double)level * levels[key];
        if (target < 8)
            target = 0;
        double step = (double)elapsed_seconds * fade;
        double gain = voice->gain.left < target ? fmin(target, voice->gain.left + step)
                                                : fmax(target, voice->gain.left - step);
        voice->gain = (qa_mixer_gain){gain, gain};
    }
    return true;
}

static int32_t signed_product(int32_t value, int32_t gain) {
    uint32_t bits = (uint32_t)value * (uint32_t)gain;
    return bits <= INT32_MAX ? (int32_t)bits : (int32_t)((int64_t)bits - INT64_C(4294967296));
}

bool qa_audio_mixer_raw(qa_audio_mixer *mixer, qa_bytes bytes, size_t frames, unsigned sample_bytes,
                        unsigned channels, uint32_t rate, float volume, qa_error *error) {
    if (!allow_mutation(mixer, error))
        return false;
    if (!mixer->enabled)
        return true;
    if (!rate || (sample_bytes != 1 && sample_bytes != 2) || (channels != 1 && channels != 2) ||
        frames > SIZE_MAX / (sample_bytes * channels) ||
        bytes.size < frames * sample_bytes * channels || (frames && !bytes.data) ||
        !isfinite(volume) || (double)(256.0f * volume) < INT32_MIN ||
        (double)(256.0f * volume) > INT32_MAX)
        return mixer_error(error, QA_ERROR_ARGUMENT, "Invalid raw audio buffer or gain");
    float scale = (float)((double)rate / mixer->options.sample_rate);
    size_t count = frames;
    if (!(channels == 2 && sample_bytes == 2 && scale == 1)) {
        size_t low = 0, high = SIZE_MAX < (uint64_t)INT64_MAX ? SIZE_MAX : (size_t)INT64_MAX;
        if ((long double)(float)((double)high * scale) < frames)
            return mixer_error(error, QA_ERROR_ARGUMENT,
                               "Raw audio resample length exceeds the clock");
        while (low < high) {
            size_t middle = low + (high - low) / 2;
            if ((long double)(float)((double)middle * scale) < frames)
                low = middle + 1;
            else
                high = middle;
        }
        count = low;
    }
    if (count > INT64_MAX)
        return mixer_error(error, QA_ERROR_ARGUMENT, "Raw audio resample length exceeds the clock");
    int64_t start = mixer->raw_end < mixer->sound_time ? mixer->sound_time : mixer->raw_end;
    if ((uint64_t)count > (uint64_t)INT64_MAX - (uint64_t)start)
        return mixer_error(error, QA_ERROR_ARGUMENT, "Raw audio end exceeds the clock");
    int32_t gain = (int32_t)truncf(256.0f * volume);
    if (sample_bytes == 1)
        gain = signed_product(gain, 256);
    if (mixer->raw_end < mixer->sound_time) {
        if (mixer->options.log && setting(mixer, "developer")) {
            char message[160];
            snprintf(message, sizeof(message),
                     "S_RawSamples: resetting minimum: %" PRId64 " < %" PRId64 "\n", mixer->raw_end,
                     mixer->sound_time);
            log_message(mixer, message);
        }
        mixer->raw_end = mixer->sound_time;
    }
    /* Only the final ring's contents survive an oversized submission. Compute
     * those frames directly, retaining source overwrite and raw-end semantics. */
    size_t first = count > QA_MIXER_RAW_FRAMES ? count - QA_MIXER_RAW_FRAMES : 0;
    for (size_t i = first; i < count; i++) {
        size_t source = channels == 2 && sample_bytes == 2 && scale == 1
                            ? i
                            : (size_t)(float)((double)i * scale);
        size_t destination = ((uint64_t)start + i) & (QA_MIXER_RAW_FRAMES - 1);
        int32_t values[2];
        for (unsigned side = 0; side < 2; side++) {
            size_t offset = (source * channels + (channels == 1 ? 0 : side)) * sample_bytes;
            if (sample_bytes == 2) {
                uint32_t bits = bytes.data[offset] | (uint32_t)bytes.data[offset + 1] << 8;
                values[side] = bits <= INT16_MAX ? (int32_t)bits : (int32_t)bits - 65536;
            } else if (channels == 2)
                values[side] = bytes.data[offset] < 128 ? bytes.data[offset]
                                                        : (int32_t)bytes.data[offset] - 256;
            else
                values[side] = (int32_t)bytes.data[offset] - 128;
            mixer->raw[destination * 2 + side] = signed_product(values[side], gain);
        }
    }
    mixer->raw_end = start + (int64_t)count;
    if (mixer->raw_end > mixer->sound_time &&
        (uint64_t)mixer->raw_end - (uint64_t)mixer->sound_time > QA_MIXER_RAW_FRAMES)
        debug_raw(mixer, "overflowed");
    return true;
}

void qa_audio_mixer_clear_raw(qa_audio_mixer *mixer) {
    if (mixer && !mixer->callback_active && !mixer->round_locked)
        mixer->raw_end = mixer->paint_time;
}

bool qa_audio_mixer_select_time(qa_audio_mixer *mixer, uint64_t delivered_frame,
                                int64_t paint_frame, qa_error *error) {
    if (!allow_mutation(mixer, error))
        return false;
    if (delivered_frame > INT64_MAX || delivered_frame < (uint64_t)mixer->sound_time)
        return mixer_error(error, QA_ERROR_ARGUMENT,
                           "Delivered audio time must advance monotonically");
    mixer->sound_time = (int64_t)delivered_frame;
    mixer->paint_time = paint_frame;
    return true;
}

int64_t qa_audio_mixer_clock(const qa_audio_mixer *mixer) { return mixer ? mixer->paint_time : 0; }

static bool voice_ended(const qa_mixer_voice *voice, int64_t time, int64_t *end) {
    if (voice->state != QA_MIXER_STARTED || voice->loop_start != QA_AUDIO_NO_LOOP ||
        time < voice->start)
        return false;
    uint64_t elapsed = (uint64_t)time - (uint64_t)voice->start;
    if (elapsed < voice->prepared->pcm->frame_count)
        return false;
    *end = voice->start + (int64_t)voice->prepared->pcm->frame_count;
    return true;
}

static bool scan_starts(qa_audio_mixer *mixer) {
    bool started = false;
    for (size_t i = 0; i < mixer->voice_count; i++) {
        qa_mixer_voice *voice = &mixer->voices[i];
        if (voice->state == QA_MIXER_PENDING) {
            voice->state = QA_MIXER_STARTED;
            voice->start = mixer->paint_time;
            notify_voice(mixer, voice, true, QA_AUDIO_ENDED, mixer->paint_time);
            started = true;
        } else {
            int64_t end;
            if (voice_ended(voice, mixer->paint_time, &end)) {
                notify_voice(mixer, voice, false, QA_AUDIO_ENDED, end);
                free_voice(mixer, i, QA_AUDIO_ENDED);
            }
        }
    }
    return started;
}

bool qa_audio_mixer_scan_starts(qa_audio_mixer *mixer) {
    if (!mixer || mixer->callback_active || mixer->round_locked || mixer->destroy_requested || mixer->destroying)
        return false;
    bool started = scan_starts(mixer);
    flush_notifications(mixer);
    return started;
}

static bool issue_scheduled(qa_audio_mixer *mixer, qa_error *error) {
    for (;;) {
        size_t next = SIZE_MAX;
        for (size_t i = 0; i < mixer->voice_count; i++) {
            const qa_mixer_voice *candidate = &mixer->voices[i];
            if (candidate->state != QA_MIXER_SCHEDULED || candidate->start > mixer->paint_time)
                continue;
            if (next == SIZE_MAX || candidate->start < mixer->voices[next].start ||
                (candidate->start == mixer->voices[next].start &&
                 candidate->order > mixer->voices[next].order))
                next = i;
        }
        if (next == SIZE_MAX)
            return true;
        qa_mixer_voice *voice = &mixer->voices[next];
        replace_channel(mixer, voice->sound.actor, voice->sound.owner, voice->channel, false, false,
                        QA_AUDIO_REPLACED);
        qa_mixer_gain gain;
        if (!spatialize_voice(mixer, voice, &gain, error)) return false;
        voice->state = QA_MIXER_STARTED;
        voice->start = mixer->paint_time;
        voice->gain = gain;
        notify_voice(mixer, voice, true, QA_AUDIO_ENDED, mixer->paint_time);
    }
}

/* The Source accumulators contain integral 24.8 contributions. Bound the
 * complete block before choosing integer storage: arbitrary static gains remain
 * valid, and their exceptional double arithmetic must not overflow a C cast. */
static bool integer_paint(const qa_audio_mixer *mixer, double effects) {
    double bound = 0;
    for (size_t i = 0; i < mixer->voice_count; ++i) {
        const qa_mixer_voice *voice = &mixer->voices[i];
        if (voice->state == QA_MIXER_STARTED)
            bound += fmax(fabs(voice->gain.left * effects), fabs(voice->gain.right * effects)) + 1;
    }
    for (size_t i = 0; i < mixer->loop_mix_count; ++i) {
        const qa_mixer_loop_mix *loop = &mixer->loop_mixes[i];
        bound += fmax(fabs(loop->gain.left * effects), fabs(loop->gain.right * effects)) + 1;
    }
    /* Below this bound, all sums (including raw int32 samples) are exactly
     * representable by the previous double accumulator as well as int64. */
    return bound <= 0x1p45;
}

static int64_t source_shift(int64_t value) {
    return value / 256 - (value % 256 < 0);
}

static void add_paint(qa_audio_mixer *mixer, size_t index, double value, bool integer) {
    if (integer)
        mixer->paint.integer[index] += (int64_t)value;
    else
        mixer->paint.wide[index] += value;
}

static void paint_samples(qa_audio_mixer *mixer, size_t frame, const int16_t *samples,
                          size_t count, qa_mixer_gain gain, double effects, bool integer) {
    double left = gain.left * effects, right = gain.right * effects;
    if (integer && fabs(left) <= 0x1p38 && fabs(right) <= 0x1p38 &&
        trunc(left) == left && trunc(right) == right) {
        int64_t lvol = (int64_t)left, rvol = (int64_t)right;
        int64_t *paint = mixer->paint.integer + frame * 2;
        for (size_t i = 0; i < count; ++i) {
            paint[i * 2] += source_shift((int64_t)samples[i] * lvol);
            paint[i * 2 + 1] += source_shift((int64_t)samples[i] * rvol);
        }
    } else {
        /* Ambient fades and extreme scales retain their actual precision. */
        for (size_t i = 0; i < count; ++i) {
            add_paint(mixer, (frame + i) * 2, floor(samples[i] * left / 256), integer);
            add_paint(mixer, (frame + i) * 2 + 1, floor(samples[i] * right / 256), integer);
        }
    }
}

static void paint_voice(qa_audio_mixer *mixer, const qa_mixer_voice *voice, size_t count,
                        double effects, bool integer) {
    if (voice->state != QA_MIXER_STARTED || (voice->gain.left == 0 && voice->gain.right == 0))
        return;
    size_t frame = 0;
    if (voice->start > mixer->paint_time) {
        uint64_t delay = (uint64_t)voice->start - (uint64_t)mixer->paint_time;
        if (delay >= count) return;
        frame = (size_t)delay;
    }
    uint64_t offset = (uint64_t)mixer->paint_time + frame - (uint64_t)voice->start;
    uint64_t length = voice->prepared->pcm->frame_count;
    while (frame < count) {
        if (offset >= length) {
            if (voice->loop_start == QA_AUDIO_NO_LOOP) return;
            offset = voice->loop_start + (offset - length) % (length - voice->loop_start);
        }
        size_t span = count - frame;
        if (length - offset < span) span = (size_t)(length - offset);
        const int16_t *samples = voice->prepared->pcm->samples + (size_t)offset;
        paint_samples(mixer, frame, samples, span, voice->gain, effects, integer);
        frame += span;
        offset += span;
    }
}

static void paint_wide_doppler(qa_audio_mixer *mixer, const qa_mixer_loop_mix *loop, size_t output,
                               size_t count, uint64_t source, double effects, bool integer) {
    const qa_mixer_prepared *prepared = loop->prepared;
    size_t period = prepared->doppler_period;
    const double *sums = prepared->doppler_sums;
    double cycles = floor((double)loop->doppler_scale / (double)period);
    double remainder = fmod(loop->doppler_scale, (double)period);
    double cycle_samples = cycles * (double)period;
    double cycle_total = cycles * sums[period];
    double offset = (double)(source % period);
    for (size_t frame = 0; frame < count; frame++) {
        double end = offset + remainder;
        size_t first = (size_t)offset, last = (size_t)end;
        double tail = sums[last < period ? last : period] - sums[first] +
                      (last > period ? sums[last - period] : 0);
        double average = (cycle_total + tail) / (cycle_samples + (double)last - (double)first);
        add_paint(mixer, (output + frame) * 2,
                  trunc(average * loop->gain.left * effects / 256), integer);
        add_paint(mixer, (output + frame) * 2 + 1,
                  trunc(average * loop->gain.right * effects / 256), integer);
        offset = fmod(end, (double)period);
    }
}

static void paint_doppler(qa_audio_mixer *mixer, const qa_mixer_loop_mix *loop, size_t output,
                          size_t count, uint64_t source, double effects, bool integer) {
    if (loop->doppler_scale > QA_MIXER_CHUNK_FRAMES) {
        paint_wide_doppler(mixer, loop, output, count, source, effects, integer);
        return;
    }
    const qa_mixer_prepared *prepared = loop->prepared;
    uint64_t chunks = (prepared->pcm->frame_count + QA_MIXER_CHUNK_FRAMES - 1) / QA_MIXER_CHUNK_FRAMES;
    uint64_t scaled = (uint64_t)((float)source * loop->old_doppler_scale);
    uint64_t chunk = (scaled / QA_MIXER_CHUNK_FRAMES) % chunks;
    float offset = (float)(scaled % QA_MIXER_CHUNK_FRAMES);
    float left = (float)(loop->gain.left * effects), right = (float)(loop->gain.right * effects);
    for (size_t frame = 0; frame < count; frame++) {
        unsigned first = (unsigned)offset;
        offset += loop->doppler_scale;
        unsigned last = (unsigned)offset;
        float total = 0;
        for (unsigned current = first; current < last; current++) {
            if (current == QA_MIXER_CHUNK_FRAMES) {
                chunk = (chunk + 1) % chunks;
                offset -= QA_MIXER_CHUNK_FRAMES;
            }
            uint64_t position =
                chunk * QA_MIXER_CHUNK_FRAMES + (current & (QA_MIXER_CHUNK_FRAMES - 1));
            total += position < prepared->pcm->frame_count ? effect_sample(prepared, position) : 0;
        }
        float divisor = (float)(256u * (last - first));
        float lvalue = total * left, rvalue = total * right;
        add_paint(mixer, (output + frame) * 2, truncf(lvalue / divisor), integer);
        add_paint(mixer, (output + frame) * 2 + 1, truncf(rvalue / divisor), integer);
    }
}

static uint64_t positive_modulo(int64_t time, uint64_t period) {
    if (time >= 0)
        return (uint64_t)time % period;
    uint64_t distance = (uint64_t)0 - (uint64_t)time;
    uint64_t remainder = distance % period;
    return remainder ? period - remainder : 0;
}

static void paint_loop(qa_audio_mixer *mixer, const qa_mixer_loop_mix *loop, size_t frames,
                       double effects, bool integer) {
    size_t output = 0;
    while (output < frames) {
        int64_t absolute = mixer->paint_time + (int64_t)output;
        if (absolute < 0 && loop->family != QA_GAME_Q3) {
            output++;
            continue;
        }
        uint64_t source = positive_modulo(absolute, loop->prepared->pcm->frame_count);
        size_t count = frames - output;
        if (loop->prepared->pcm->frame_count - source < count)
            count = (size_t)(loop->prepared->pcm->frame_count - source);
        if (mixer->doppler_enabled && loop->doppler && loop->doppler_scale != 1)
            paint_doppler(mixer, loop, output, count, source, effects, integer);
        else
            paint_samples(mixer, output, loop->prepared->pcm->samples + (size_t)source,
                          count, loop->gain, effects, integer);
        output += count;
    }
}

static int16_t clipped_sample(double sample) {
    sample = floor(sample / 256);
    if (sample > INT16_MAX)
        return INT16_MAX;
    if (sample < INT16_MIN)
        return INT16_MIN;
    return isfinite(sample) ? (int16_t)sample : 0;
}

static bool paint_range(qa_audio_mixer *mixer, int64_t start, int16_t *stereo, float *floating,
                        size_t frames, bool consume, qa_error *error) {
    if (!allow_mutation(mixer, error))
        return false;
    size_t sample_size = floating ? sizeof(*floating) : sizeof(*stereo);
    if ((frames && !stereo && !floating) || frames > SIZE_MAX / (2 * sample_size) ||
        frames > INT64_MAX ||
        (uint64_t)frames > (uint64_t)INT64_MAX - (uint64_t)start)
        return mixer_error(error, QA_ERROR_ARGUMENT, "Invalid audio paint range");
    int64_t end = start + (int64_t)frames;
    if (consume && end < mixer->sound_time)
        return mixer_error(error, QA_ERROR_ARGUMENT,
                           "Direct audio consumption cannot rewind delivery");
    mixer->paint_time = start;
    scan_starts(mixer);
    double effects = truncf(mixer->effects_gain * 255.0f);
    size_t output = 0;
    while (output < frames) {
        if (!issue_scheduled(mixer, error)) {
            qa_error diagnostic = error ? *error : (qa_error){0};
            flush_notifications(mixer);
            if (error) *error = diagnostic;
            return false;
        }
        size_t count = frames - output;
        if (count > QA_MIXER_PAINT_FRAMES)
            count = QA_MIXER_PAINT_FRAMES;
        for (size_t i = 0; i < mixer->voice_count; i++) {
            const qa_mixer_voice *voice = &mixer->voices[i];
            if (voice->state == QA_MIXER_SCHEDULED) {
                uint64_t until = (uint64_t)voice->start - (uint64_t)mixer->paint_time;
                if (until < count)
                    count = (size_t)until;
            }
        }
        bool integer = integer_paint(mixer, effects);
        memset(&mixer->paint, 0, count * 2 * sizeof(int64_t));
        for (size_t frame = 0; frame < count; frame++) {
            int64_t absolute = mixer->paint_time + (int64_t)frame;
            if (absolute >= mixer->raw_end)
                break;
            size_t index = (uint64_t)absolute & (QA_MIXER_RAW_FRAMES - 1);
            add_paint(mixer, frame * 2, mixer->raw[index * 2], integer);
            add_paint(mixer, frame * 2 + 1, mixer->raw[index * 2 + 1], integer);
        }
        for (size_t i = 0; i < mixer->voice_count; i++)
            paint_voice(mixer, &mixer->voices[i], count, effects, integer);
        for (size_t i = 0; i < mixer->loop_mix_count; i++)
            paint_loop(mixer, &mixer->loop_mixes[i], count, effects, integer);
        if (setting(mixer, "s_testsound"))
            for (size_t frame = 0; frame < count; frame++) {
                double value =
                    trunc(sin((double)(mixer->paint_time + (int64_t)frame) * 0.1) * 20000 * 256);
                if (integer)
                    mixer->paint.integer[frame * 2] =
                        mixer->paint.integer[frame * 2 + 1] = (int64_t)value;
                else
                    mixer->paint.wide[frame * 2] = mixer->paint.wide[frame * 2 + 1] = value;
            }
        for (size_t sample = 0; sample < count * 2; sample++) {
            int16_t value;
            if (integer) {
                int64_t shifted = source_shift(mixer->paint.integer[sample]);
                value = shifted > INT16_MAX ? INT16_MAX
                        : shifted < INT16_MIN ? INT16_MIN : (int16_t)shifted;
            } else
                value = clipped_sample(mixer->paint.wide[sample]);
            if (floating)
                floating[output * 2 + sample] = value;
            else
                stereo[output * 2 + sample] = value;
        }
        mixer->paint_time += (int64_t)count;
        output += count;
        for (size_t i = 0; i < mixer->voice_count; i++) {
            int64_t ended;
            if (voice_ended(&mixer->voices[i], mixer->paint_time, &ended))
                notify_voice(mixer, &mixer->voices[i], false, QA_AUDIO_ENDED, ended);
        }
    }
    if (consume)
        mixer->sound_time = end;
    flush_notifications(mixer);
    return true;
}

bool qa_audio_mixer_mix(qa_audio_mixer *mixer, int16_t *stereo, size_t frames, qa_error *error) {
    return paint_range(mixer, mixer ? mixer->paint_time : 0, stereo, NULL, frames, true, error);
}

bool qa_audio_mixer_mix_float(qa_audio_mixer *mixer, float *stereo, size_t frames, qa_error *error) {
    return paint_range(mixer, mixer ? mixer->paint_time : 0, NULL, stereo, frames, true, error);
}

bool qa_audio_mixer_paint(qa_audio_mixer *mixer, int64_t start_frame, int16_t *stereo,
                          size_t frames, qa_error *error) {
    return paint_range(mixer, start_frame, stereo, NULL, frames, false, error);
}

bool qa_audio_mixer_rebase(qa_audio_mixer *mixer, uint64_t delivered_frame, qa_error *error) {
    if (!allow_mutation(mixer, error))
        return false;
    const uint64_t epoch = UINT64_C(0x40000000);
    if (delivered_frame > INT64_MAX || delivered_frame <= (uint64_t)mixer->sound_time ||
        delivered_frame < epoch)
        return mixer_error(error, QA_ERROR_ARGUMENT,
                           "Audio rebase requires advancing delivery beyond an epoch");
    int64_t offset = (int64_t)(delivered_frame / epoch * epoch);
    if (mixer->paint_time < INT64_MIN + offset)
        return mixer_error(error, QA_ERROR_ARGUMENT,
                           "Rebased paint time is outside the audio clock");
    mixer->sound_time = (int64_t)delivered_frame - offset;
    mixer->paint_time -= offset;
    return true;
}

void qa_audio_mixer_clear_buffer(qa_audio_mixer *mixer) {
    if (!mixer || mixer->callback_active || mixer->round_locked)
        return;
    double begin_offset = mixer->source_begin_offset;
    uint64_t order = mixer->schedule_order;
    stop_all_state(mixer);
    mixer->source_begin_offset = begin_offset;
    mixer->schedule_order = order;
    mixer->raw_end = 0;
    flush_notifications(mixer);
}

int64_t qa_audio_mixer_raw_end(const qa_audio_mixer *mixer) { return mixer ? mixer->raw_end : 0; }

int32_t *qa_audio_mixer_raw_samples(qa_audio_mixer *mixer) {
    return mixer && !mixer->callback_active && !mixer->round_locked ? mixer->raw : NULL;
}

void qa_audio_mixer_reset_raw(qa_audio_mixer *mixer, bool stopped) {
    if (mixer && !mixer->callback_active && !mixer->round_locked)
        mixer->raw_end = stopped ? 0 : mixer->sound_time;
}

void qa_audio_mixer_enable(qa_audio_mixer *mixer, bool enabled) {
    if (mixer && !mixer->callback_active && !mixer->round_locked)
        mixer->enabled = enabled;
}

uint64_t qa_audio_mixer_sound_clock(const qa_audio_mixer *mixer) {
    return mixer ? (uint64_t)mixer->sound_time : 0;
}

size_t qa_audio_mixer_channel_volumes(const qa_audio_mixer *mixer, qa_audio_channel_volume *out,
                                      size_t capacity) {
    if (!mixer || (capacity && !out))
        return 0;
    size_t count = 0;
    for (size_t i = 0; i < mixer->voice_count; i++) {
        const qa_mixer_voice *voice = &mixer->voices[i];
        if (voice->state == QA_MIXER_FREE || voice->state == QA_MIXER_SCHEDULED ||
            (voice->gain.left == 0 && voice->gain.right == 0))
            continue;
        if (count < capacity)
            out[count] = (qa_audio_channel_volume){voice->prepared->sample, voice->gain.left,
                                                   voice->gain.right};
        count++;
    }
    return count;
}
