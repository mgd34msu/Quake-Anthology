#include "engine_internal.h"
#include "mixer_internal.h"
#include "checkpoint_internal.h"
#include <limits.h>

static uint32_t callback_mask(const qa_audio_engine_options *o) {
    return (o->random ? 1u : 0u) | (o->milliseconds ? 2u : 0u) | (o->observer ? 4u : 0u) |
        (o->setting ? 8u : 0u) | (o->log ? 16u : 0u);
}
static bool put_listener(qa_ac_writer *w, const qa_audio_checkpoint_refs *refs, const qa_audio_listener *v) {
    bool ok = qa_ac_u32(w, v->seat) && qa_ac_ref(w, refs, QA_AUDIO_REFERENCE_ACTOR, v->actor) && qa_ac_vec(w, v->origin);
    for (size_t i = 0; ok && i < 3; ++i) ok = qa_ac_vec(w, v->axis[i]);
    return ok && qa_ac_float(w, v->gain) && qa_ac_u32(w, v->underwater);
}
static void get_listener(qa_ac_reader *r, const qa_audio_checkpoint_refs *refs, qa_audio_listener *v) {
    v->seat = qa_ac_get32(r); v->actor = qa_ac_getref(r, refs, QA_AUDIO_REFERENCE_ACTOR); v->origin = qa_ac_getvec(r);
    for (size_t i = 0; i < 3; ++i) v->axis[i] = qa_ac_getvec(r);
    v->gain = qa_ac_getfloat(r); v->underwater = qa_ac_bool(r);
    if (v->seat == QA_AUDIO_WORLD || v->gain < 0) qa_ac_bad(r, "Invalid engine listener");
}
static bool same_vector(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static bool same_listener(const qa_audio_listener *a, const qa_audio_listener *b) {
    if (a->seat != b->seat || a->actor != b->actor || a->gain != b->gain || a->underwater != b->underwater ||
        !same_vector(a->origin, b->origin)) return false;
    for (size_t i = 0; i < 3; ++i) if (!same_vector(a->axis[i], b->axis[i])) return false;
    return true;
}
static void nested(qa_ac_writer *w, bool success, qa_buffer *bytes) {
    if (!success) w->failed = true;
    else qa_ac_blob(w, (qa_bytes){bytes->data, bytes->size});
    qa_buffer_free(bytes);
}
static const qa_audio_mixer *owned_mixer(const qa_audio_engine *engine, size_t index)
{
    return index < engine->seat_count ?
        (engine->seats[index] ? engine->seats[index]->mixer : NULL) :
        engine->round_mixers[index - engine->seat_count].mixer;
}
void qa_audio_engine_discard(qa_audio_engine *engine)
{
    if (!engine) return;
    engine->options.observer = NULL;
    for (size_t i = 0; i < engine->seat_count + engine->round_mixer_count; ++i) {
        qa_audio_mixer *mixer = i < engine->seat_count
            ? (engine->seats[i] ? engine->seats[i]->mixer : NULL) : engine->round_mixers[i - engine->seat_count].mixer;
        if (!mixer) continue;
        mixer->options.observer = NULL; mixer->options.setting = NULL; mixer->options.log = NULL;
        mixer->options.random = NULL; mixer->options.milliseconds = NULL;
        mixer->options.allocate_voice_id = NULL; mixer->transmission = NULL;
    }
    qa_audio_engine_destroy(engine);
}
bool qa_audio_engine_assets_read(const qa_audio_engine *engine, qa_audio_asset ***out,
    size_t *out_count, qa_error *error)
{
    if (!engine || !out || *out || !out_count || engine->operation_depth || engine->callback_depth ||
        engine->destroy_pending || engine->destroying || (engine->seat_count && !engine->seats) ||
        (engine->round_mixer_count && !engine->round_mixers) ||
        engine->round_mixer_count > SIZE_MAX - engine->seat_count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Audio asset inventory requires an idle engine and empty output");
        return false;
    }
    size_t owners = engine->seat_count + engine->round_mixer_count;
    size_t count = 0;
    for (size_t i = 0; i < owners; ++i) {
        const qa_audio_mixer *mixer = owned_mixer(engine, i);
        if (!qa_audio_mixer_callbacks_idle(mixer) ||
            mixer->prepared_count > mixer->prepared_capacity || (mixer->prepared_capacity && !mixer->prepared)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Audio asset inventory requires actual idle seat mixers");
            return false;
        }
        for (size_t j = 0; j < mixer->prepared_count; ++j) {
            const qa_mixer_prepared *prepared = mixer->prepared[j];
            if (!prepared) continue;
            if (prepared->slot != j || !prepared->references || !prepared->sample ||
                (prepared->asset && qa_audio_asset_sample(prepared->asset) != prepared->sample)) {
                qa_error_set(error, QA_ERROR_FORMAT, 0, "Audio prepared owner is not source-qualified");
                return false;
            }
            if (prepared->asset) {
                if (count == SIZE_MAX / sizeof(qa_audio_asset *)) {
                    qa_error_set(error, QA_ERROR_MEMORY, 0, "Audio asset inventory overflows"); return false;
                }
                ++count;
            }
        }
    }
    qa_audio_asset **assets = count ? malloc(count * sizeof(*assets)) : NULL;
    if (count && !assets) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating borrowed engine asset inventory"); return false;
    }
    size_t used = 0;
    for (size_t i = 0; i < owners; ++i) {
        const qa_audio_mixer *mixer = owned_mixer(engine, i);
        for (size_t j = 0; j < mixer->prepared_count; ++j)
            if (mixer->prepared[j] && mixer->prepared[j]->asset) assets[used++] = mixer->prepared[j]->asset;
    }
    *out = assets; *out_count = count; return true;
}
bool qa_audio_engine_checkpoint(const qa_audio_engine *engine, const qa_audio_checkpoint_refs *refs,
                                 qa_buffer *out, qa_error *error) {
    if (!out || !qa_audio_engine_round_ready(engine, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Engine checkpoint requires completed operations and callbacks"); return false;
    }
    for (size_t i = 0; i < engine->seat_count; ++i)
        if (!same_listener(&engine->seats[i]->listener, &engine->seats[i]->mixer->listener)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Engine checkpoint requires synchronized listener publication"); return false;
        }
    qa_buffer *definitions = engine->seat_count ? calloc(engine->seat_count, sizeof(*definitions)) : NULL;
    size_t *selection = engine->seat_count ? calloc(engine->seat_count, sizeof(*selection)) : NULL;
    if (engine->seat_count && (!definitions || !selection)) {
        free(definitions); free(selection);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining sound environment definitions"); return false;
    }
    qa_ac_writer w = {.error = error}; size_t definition_count = 0;
    for (size_t i = 0; !w.failed && i < engine->seat_count; ++i) {
        selection[i] = SIZE_MAX;
        const qa_audio_environment *environment = engine->seats[i]->environment;
        if (!environment) continue;
        qa_buffer bytes = {0};
        if (!qa_audio_environment_definition_checkpoint(environment, &bytes, error)) { w.failed = true; break; }
        size_t j = 0;
        while (j < definition_count && (definitions[j].size != bytes.size || memcmp(definitions[j].data, bytes.data, bytes.size))) ++j;
        if (j == definition_count) definitions[definition_count++] = bytes;
        else qa_buffer_free(&bytes);
        selection[i] = j;
    }
    qa_ac_write(&w, "QAEN", 4); qa_ac_u32(&w, 3); qa_ac_u32(&w, engine->options.sample_rate);
    qa_ac_u32(&w, engine->options.output_channels); qa_ac_u64(&w, engine->options.mix_frames);
    qa_ac_u64(&w, engine->options.initial_voices); qa_ac_u32(&w, callback_mask(&engine->options));
    qa_ac_u32(&w, engine->geometry != NULL); qa_ac_u64(&w, engine->clock); qa_ac_u64(&w, engine->next_voice);
    qa_ac_double(&w, engine->milliseconds); qa_ac_float(&w, engine->effects_gain);
    qa_ac_float(&w, engine->music_gain);
    qa_ac_u32(&w, engine->paused); qa_ac_u32(&w, engine->doppler);
    qa_ac_u64(&w, engine->seat_count); qa_ac_u64(&w, engine->position_count); qa_ac_u64(&w, engine->bus_count);
    qa_ac_u64(&w, definition_count);
    qa_ac_u64(&w, engine->round_mixer_count);
    for (size_t i = 0; !w.failed && i < definition_count; ++i)
        qa_ac_blob(&w, (qa_bytes){definitions[i].data, definitions[i].size});
    for (size_t i = 0; !w.failed && i < engine->seat_count; ++i) {
        const audio_seat *seat = engine->seats[i]; qa_buffer bytes = {0};
        put_listener(&w, refs, &seat->listener);
        nested(&w, qa_audio_mixer_checkpoint(seat->mixer, refs, &bytes, error), &bytes);
        if (!w.failed) nested(&w, qa_audio_reverb_checkpoint(seat->reverb, &bytes, error), &bytes);
        for (size_t j = 0; j < 2; ++j) { qa_ac_float(&w, seat->underwater.z1[j]); qa_ac_float(&w, seat->underwater.z2[j]); }
        qa_ac_u64(&w, selection[i] == SIZE_MAX ? UINT64_MAX : selection[i]);
        if (!w.failed && seat->environment)
            nested(&w, qa_audio_environment_checkpoint(seat->environment, &bytes, error), &bytes);
    }
    for (size_t i = 0; !w.failed && i < engine->position_count; ++i) {
        qa_ac_ref(&w, refs, QA_AUDIO_REFERENCE_ACTOR, engine->positions[i].actor); qa_ac_vec(&w, engine->positions[i].position);
    }
    for (size_t i = 0; !w.failed && i < engine->bus_count; ++i) {
        const audio_bus *bus = &engine->buses[i]; qa_buffer bytes = {0};
        qa_ac_ref(&w, refs, QA_AUDIO_REFERENCE_BUS, bus->id); qa_ac_u32(&w, bus->audience); qa_ac_float(&w, bus->gain);
        qa_ac_u32(&w, bus->raw != NULL);
        if (!w.failed) nested(&w, bus->raw ? qa_audio_raw_checkpoint(bus->raw, &bytes, error) :
                             qa_audio_music_checkpoint(bus->music, &bytes, error), &bytes);
    }
    for (size_t i = 0; !w.failed && i < engine->round_mixer_count; ++i) {
        const audio_round_mixer *retained = &engine->round_mixers[i]; qa_buffer bytes = {0};
        qa_ac_u32(&w, retained->seat);
        nested(&w, qa_audio_mixer_checkpoint(retained->mixer, refs, &bytes, error), &bytes);
    }
    for (size_t i = 0; i < definition_count; ++i) qa_buffer_free(&definitions[i]);
    free(definitions); free(selection); return qa_ac_finish(&w, out);
}
static size_t get_count(qa_ac_reader *r, size_t size) {
    uint64_t count = qa_ac_get64(r);
    if (count > SIZE_MAX / size || count > r->bytes.size / 8) {
        qa_ac_bad(r, "Saved engine owner table exceeds its record"); return 0;
    }
    return (size_t)count;
}
static void *get_array(qa_ac_reader *r, size_t count, size_t size) {
    if (r->failed) return NULL;
    void *memory = count ? calloc(count, size) : NULL;
    if (count && !memory) {
        qa_error_set(r->error, QA_ERROR_MEMORY, r->offset, "Restoring audio engine owners"); r->failed = true;
    }
    return memory;
}
bool qa_audio_engine_restore(qa_bytes bytes, const qa_audio_engine_options *options,
                             const qa_audio_checkpoint_refs *refs, qa_audio_engine **out, qa_error *error) {
    if (!options || !out || (!bytes.data && bytes.size)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid isolated audio engine destination"); return false;
    }
    qa_ac_reader r = {.bytes = bytes, .error = error}; qa_bytes magic;
    if (!qa_ac_read(&r, 4, &magic) || memcmp(magic.data, "QAEN", 4) || qa_ac_get32(&r) != 3 ||
        qa_ac_get32(&r) != options->sample_rate || qa_ac_get32(&r) != options->output_channels ||
        qa_ac_get64(&r) != (options->mix_frames ? options->mix_frames : 4096) ||
        qa_ac_get64(&r) != (options->initial_voices ? options->initial_voices : 96) ||
        qa_ac_get32(&r) != callback_mask(options) || qa_ac_bool(&r) != (refs && refs->geometry))
        return qa_ac_bad(&r, "Saved engine format or candidate service admission differs");
    qa_audio_engine *engine = NULL;
    if (r.failed || !qa_audio_engine_create(options, &engine, error)) return false;
    engine->geometry = refs ? refs->geometry : NULL; engine->geometry_user = refs ? refs->geometry_context : NULL;
    engine->clock = qa_ac_get64(&r); engine->next_voice = qa_ac_get64(&r); engine->milliseconds = qa_ac_getdouble(&r);
    engine->effects_gain = qa_ac_getfloat(&r); engine->music_gain = qa_ac_getfloat(&r);
    engine->paused = qa_ac_bool(&r); engine->doppler = qa_ac_bool(&r);
    if (engine->clock > INT64_MAX || engine->effects_gain < 0 || engine->music_gain < 0 ||
        (double)(engine->effects_gain * 255.0f) > INT32_MAX)
        qa_ac_bad(&r, "Invalid saved engine clock or gain");
    size_t seats = get_count(&r, sizeof(*engine->seats)), positions = get_count(&r, sizeof(*engine->positions));
    size_t buses = get_count(&r, sizeof(*engine->buses)), definition_count = get_count(&r, sizeof(qa_audio_environments *));
    size_t retained = get_count(&r, sizeof(*engine->round_mixers));
    if (definition_count > seats) qa_ac_bad(&r, "Unowned audio environment definitions");
    engine->seats = get_array(&r, seats, sizeof(*engine->seats));
    engine->positions = get_array(&r, positions, sizeof(*engine->positions));
    engine->buses = get_array(&r, buses, sizeof(*engine->buses));
    engine->round_mixers = get_array(&r, retained, sizeof(*engine->round_mixers));
    qa_audio_environments **definitions = get_array(&r, definition_count, sizeof(*definitions));
    size_t *definition_uses = get_array(&r, definition_count, sizeof(*definition_uses));
    if (!r.failed) {
        engine->position_capacity = positions; engine->bus_capacity = buses;
        engine->round_mixer_capacity = retained;
    }
    for (size_t i = 0; !r.failed && i < definition_count; ++i) {
        qa_bytes data;
        if (qa_ac_getblob(&r, &data) && !qa_audio_environments_restore(data, &definitions[i], error)) r.failed = true;
    }
    qa_audio_mixer_options mixer_options = qa_audio_engine_mixer_options(engine);
    for (size_t i = 0; !r.failed && i < seats; ++i) {
        audio_seat *seat = get_array(&r, 1, sizeof(*seat));
        if (!seat) break;
        engine->seats[engine->seat_count++] = seat; get_listener(&r, refs, &seat->listener);
        for (size_t j = 0; j < i; ++j)
            if (engine->seats[j]->listener.seat == seat->listener.seat) qa_ac_bad(&r, "Duplicate restored listener seat");
        qa_bytes data;
        if (qa_ac_getblob(&r, &data) && !qa_audio_mixer_restore(data, &mixer_options, refs, &seat->mixer, error)) r.failed = true;
        if (!r.failed && !same_listener(&seat->listener, &seat->mixer->listener))
            qa_ac_bad(&r, "Saved seat and mixer listeners differ");
        for (size_t j = 0; !r.failed && j < seat->mixer->voice_count; ++j) {
            const qa_mixer_voice *voice = &seat->mixer->voices[j];
            if (voice->state == QA_MIXER_FREE || voice->role != QA_MIXER_EFFECT) continue;
            if (voice->id > engine->next_voice) qa_ac_bad(&r, "Restored voice leaves engine allocator epoch");
            for (size_t k = 0; !r.failed && k < i; ++k) {
                const qa_audio_mixer *previous = engine->seats[k]->mixer;
                for (size_t n = 0; n < previous->voice_count; ++n)
                    if (previous->voices[n].state != QA_MIXER_FREE && previous->voices[n].role == QA_MIXER_EFFECT &&
                        previous->voices[n].id == voice->id) qa_ac_bad(&r, "Duplicate voice in engine allocator epoch");
            }
        }
        if (qa_ac_getblob(&r, &data) && !qa_audio_reverb_restore(data, &seat->reverb, error)) r.failed = true;
        if (!r.failed && qa_audio_reverb_rate(seat->reverb) != options->sample_rate)
            qa_ac_bad(&r, "Saved reverb rate differs from its engine");
        for (size_t j = 0; j < 2; ++j) { seat->underwater.z1[j] = qa_ac_getfloat(&r); seat->underwater.z2[j] = qa_ac_getfloat(&r); }
        uint64_t definition = qa_ac_get64(&r);
        if (definition != UINT64_MAX && !r.failed) {
            if (definition >= definition_count) { qa_ac_bad(&r, "Saved environment leaves its definition table"); break; }
            qa_audio_trace_fn trace = NULL; void *user = NULL;
            if (!refs || !refs->environment || !refs->environment(refs->context, seat->listener.seat, &trace, &user, error)) {
                if (!refs || !refs->environment) qa_error_set(error, QA_ERROR_ARGUMENT, r.offset, "Candidate sound environment binding is absent");
                r.failed = true; break;
            }
            ++definition_uses[definition];
            if (qa_ac_getblob(&r, &data) && !qa_audio_environment_restore(data, definitions[definition], trace, user, &seat->environment, error))
                r.failed = true;
        }
    }
    for (size_t i = 0; !r.failed && i < definition_count; ++i)
        if (!definition_uses[i]) qa_ac_bad(&r, "Saved environment definition has no selector");
    for (size_t i = 0; !r.failed && i < positions; ++i) {
        audio_position *position = &engine->positions[i]; position->actor = qa_ac_getref(&r, refs, QA_AUDIO_REFERENCE_ACTOR);
        position->position = qa_ac_getvec(&r);
        if (position->actor == QA_AUDIO_NO_ACTOR) qa_ac_bad(&r, "Saved engine position has no actor");
        for (size_t j = 0; j < i; ++j) if (engine->positions[j].actor == position->actor) qa_ac_bad(&r, "Duplicate restored engine position");
        ++engine->position_count;
    }
    for (size_t i = 0; !r.failed && i < buses; ++i) {
        audio_bus *bus = &engine->buses[engine->bus_count++];
        bus->id = qa_ac_getref(&r, refs, QA_AUDIO_REFERENCE_BUS); bus->audience = qa_ac_get32(&r); bus->gain = qa_ac_getfloat(&r);
        bool raw = qa_ac_bool(&r); qa_bytes data;
        if (bus->gain < 0) qa_ac_bad(&r, "Invalid saved bus gain");
        if (qa_ac_getblob(&r, &data)) {
            bool restored = raw ? qa_audio_raw_restore(data, options->sample_rate, &bus->raw, error) :
                qa_audio_music_restore(data, &bus->music, error);
            if (!restored) r.failed = true;
        }
        if (!r.failed && ((bus->raw && qa_audio_raw_rate(bus->raw) != options->sample_rate) ||
            (bus->music && qa_audio_music_rate(bus->music) != options->sample_rate))) qa_ac_bad(&r, "Saved bus rate differs from engine");
        for (size_t j = 0; !r.failed && j < i; ++j)
            if (engine->buses[j].id == bus->id && (engine->buses[j].raw != NULL) == raw)
                qa_ac_bad(&r, "Duplicate restored audio bus");
    }
    for (size_t i = 0; !r.failed && i < retained; ++i) {
        audio_round_mixer *held = &engine->round_mixers[engine->round_mixer_count++];
        held->seat = qa_ac_get32(&r); qa_bytes data;
        if (held->seat == QA_AUDIO_WORLD) qa_ac_bad(&r, "Retained round mixer has no seat");
        for (size_t j = 0; !r.failed && j < engine->seat_count; ++j)
            if (engine->seats[j]->listener.seat == held->seat) qa_ac_bad(&r, "Retained mixer duplicates an active seat");
        for (size_t j = 0; !r.failed && j < i; ++j)
            if (engine->round_mixers[j].seat == held->seat) qa_ac_bad(&r, "Duplicate retained round mixer seat");
        if (!r.failed && qa_ac_getblob(&r, &data) &&
            !qa_audio_mixer_restore(data, &mixer_options, refs, &held->mixer, error)) r.failed = true;
        if (!r.failed && (held->mixer->listener.seat != held->seat || held->mixer->loop_count ||
            held->mixer->loop_mix_count || held->mixer->position_count || held->mixer->transmission_count ||
            held->mixer->event_head != SIZE_MAX || held->mixer->source_begin_offset != 0 ||
            held->mixer->schedule_order || held->mixer->raw_end != held->mixer->paint_time))
            qa_ac_bad(&r, "Retained mixer is not at its real stopped round boundary");
        for (size_t j = 0; !r.failed && j < held->mixer->voice_count; ++j)
            if (held->mixer->voices[j].state != QA_MIXER_FREE)
                qa_ac_bad(&r, "Retained round mixer contains an active source voice");
    }
    if (!r.failed && r.offset != bytes.size) qa_ac_bad(&r, "Engine checkpoint has trailing fields");
    if (definitions) for (size_t i = 0; i < definition_count; ++i) qa_audio_environments_destroy(definitions[i]);
    free(definitions); free(definition_uses);
    if (r.failed) { qa_audio_engine_discard(engine); return false; }
    *out = engine; return true;
}
bool qa_audio_engine_restore_into(qa_audio_engine *engine, qa_bytes bytes,
    const qa_audio_checkpoint_refs *refs, qa_error *error)
{
    if (!engine || engine->round_resetting || engine->operation_depth || engine->callback_depth || engine->destroy_pending ||
        engine->destroying || engine->seat_count || engine->round_mixer_count || engine->position_count || engine->bus_count ||
        engine->clock || engine->next_voice) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Audio import requires an empty idle candidate engine");
        return false;
    }
    qa_audio_engine *decoded = NULL;
    if (!qa_audio_engine_restore(bytes, &engine->options, refs, &decoded, error)) return false;
    qa_audio_engine previous = *engine;
    *engine = *decoded; *decoded = previous;
    for (size_t i = 0; i < engine->seat_count + engine->round_mixer_count; ++i) {
        qa_audio_mixer *mixer = (qa_audio_mixer *)owned_mixer(engine, i);
        mixer->options.voice_id_user = engine;
        if (engine->options.observer) mixer->options.observer_user = engine;
    }
    qa_audio_engine_discard(decoded);
    return true;
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

bool qa_audio_engine_raw_checkpoint_ready(const qa_audio_engine *engine, uint64_t id,
    uint32_t audience, float gain, qa_bytes saved, qa_error *error)
{
    if (!engine || engine->operation_depth || engine->callback_depth || engine->destroy_pending || engine->destroying ||
        (saved.size && !saved.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Raw queue qualification requires an idle restored engine"); return false;
    }
    const audio_bus *bus = NULL;
    for (size_t i=0;i<engine->bus_count;++i) if (engine->buses[i].id==id && engine->buses[i].raw) { bus=&engine->buses[i]; break; }
    if ((bus!=NULL)!=(saved.size!=0) || (bus && (bus->audience!=audience || memcmp(&bus->gain,&gain,sizeof(gain))))) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Restored cinematic queue presence or route differs"); return false;
    }
    if (!bus) return true;
    qa_buffer actual={0}; bool ok=qa_audio_raw_checkpoint(bus->raw,&actual,error);
    if (ok && (actual.size!=saved.size || memcmp(actual.data,saved.data,saved.size))) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Restored cinematic raw queue differs"); ok=false;
    }
    qa_buffer_free(&actual); return ok;
}
