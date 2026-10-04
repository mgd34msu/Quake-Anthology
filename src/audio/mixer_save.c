#include "mixer_internal.h"
#include "checkpoint_internal.h"

static bool put_gain(qa_source_save_io *w, qa_mixer_gain v) {
    return qa_ac_double(w, v.left) && qa_ac_double(w, v.right);
}
static qa_mixer_gain get_gain(qa_source_save_io *r) {
    qa_mixer_gain v; v.left = qa_ac_getdouble(r); v.right = qa_ac_getdouble(r); return v;
}
static bool put_index(qa_source_save_io *w, size_t index) { return qa_ac_u64(w, index == SIZE_MAX ? UINT64_MAX : index); }
static size_t get_index(qa_source_save_io *r, size_t count) {
    uint64_t value = qa_ac_get64(r);
    if (value == UINT64_MAX) return SIZE_MAX;
    if (value >= count) { qa_ac_bad(r, "Saved audio index leaves its owner table"); return SIZE_MAX; }
    return (size_t)value;
}
static bool put_prepared(qa_source_save_io *w, const qa_audio_checkpoint_refs *refs, const qa_mixer_prepared *p) {
    if (!qa_ac_u32(w, p != NULL) || !p) return !w->failed;
    qa_buffer sample = {0};
    if (!qa_audio_sample_checkpoint(p->sample, &sample, w->error)) { w->failed = true; return false; }
    bool ok = qa_ac_blob(w, (qa_bytes){sample.data, sample.size}); qa_buffer_free(&sample);
    ok = ok && qa_ac_u32(w, p->layout.q3) && qa_ac_u32(w, p->doppler_sums != NULL) && qa_ac_u32(w, p->asset != NULL);
    if (ok && p->asset) {
        const qa_resource *resource = qa_audio_asset_resource(p->asset);
        const qa_sha256_digest *digest = qa_resource_digest(resource);
        const char *name = qa_audio_asset_name(p->asset);
        if (!digest || !name) {
            qa_error_set(w->error, QA_ERROR_ARGUMENT, 0, "Audio asset has no persistent source identity"); w->failed = true; return false;
        }
        ok = qa_ac_u32(w, qa_audio_asset_family(p->asset)) &&
            qa_ac_blob(w, (qa_bytes){(const uint8_t *)name, strlen(name)}) && qa_ac_write(w, digest->bytes, sizeof(digest->bytes));
        qa_buffer descriptor = {0};
        if (ok && (!refs || !refs->asset_encode || !refs->asset_encode(refs->context, p->asset, &descriptor, w->error))) {
            if (!refs || !refs->asset_encode) qa_error_set(w->error, QA_ERROR_ARGUMENT, 0, "Audio asset identity encoder is absent");
            w->failed = true; ok = false;
        }
        if (ok && descriptor.size && !descriptor.data) {
            qa_error_set(w->error, QA_ERROR_ARGUMENT, 0, "Audio asset descriptor storage is absent"); w->failed = true; ok = false;
        }
        ok = ok && qa_ac_blob(w, (qa_bytes){descriptor.data, descriptor.size}); qa_buffer_free(&descriptor);
    }
    return ok;
}
static bool make_doppler(qa_mixer_prepared *p, qa_source_save_io *r) {
    if (p->layout.frames > SIZE_MAX - QA_MIXER_CHUNK_FRAMES) return qa_ac_bad(r, "Saved Doppler period overflows storage");
    size_t period = ((size_t)p->layout.frames + QA_MIXER_CHUNK_FRAMES - 1) / QA_MIXER_CHUNK_FRAMES * QA_MIXER_CHUNK_FRAMES;
    if (period >= SIZE_MAX / sizeof(double)) return qa_ac_bad(r, "Saved Doppler sums overflow storage");
    p->doppler_sums = malloc((period + 1) * sizeof(*p->doppler_sums));
    if (!p->doppler_sums) { qa_error_set(r->error, QA_ERROR_MEMORY, r->offset, "Restoring Doppler sample sums"); r->failed = true; return false; }
    p->doppler_period = period; p->doppler_sums[0] = 0;
    for (size_t i = 0; i < period; ++i) {
        uint64_t source; int16_t value = 0;
        if (i < p->layout.frames && qa_audio_source_index(&p->layout, i, &source) && source < p->sample->frame_count)
            value = p->sample->samples[source];
        p->doppler_sums[i + 1] = p->doppler_sums[i] + value;
    }
    return true;
}
static void get_prepared(qa_source_save_io *r, const qa_audio_checkpoint_refs *refs, qa_audio_mixer *m, size_t slot) {
    if (!qa_ac_bool(r) || r->failed) return;
    qa_bytes bytes;
    if (!qa_ac_getblob(r, &bytes)) return;
    qa_mixer_prepared *p = calloc(1, sizeof(*p));
    if (!p) { qa_error_set(r->error, QA_ERROR_MEMORY, r->offset, "Restoring prepared sound"); r->failed = true; return; }
    m->prepared[slot] = p; p->slot = slot;
    if (!qa_audio_sample_restore(bytes, &p->sample, r->error)) { r->failed = true; return; }
    bool q3 = qa_ac_bool(r), doppler = qa_ac_bool(r), asset = qa_ac_bool(r);
    if (r->failed) return;
    if (asset) {
        uint32_t family = qa_ac_get32(r); qa_bytes name, digest;
        if (!qa_ac_getblob(r, &name) || !qa_ac_read(r, 32, &digest)) return;
        if (family > QA_AUDIO_Q3 || name.size == SIZE_MAX || memchr(name.data, 0, name.size)) { qa_ac_bad(r, "Invalid saved sound asset identity"); return; }
        qa_bytes descriptor;
        if (!qa_ac_getblob(r, &descriptor)) return;
        qa_sha256_digest identity; memcpy(identity.bytes, digest.data, 32);
        bool resolved = refs && refs->asset_decode && refs->asset_decode(refs->context, descriptor, &p->asset, r->error);
        if (!resolved || !p->asset) {
            if (!refs || !refs->asset_decode) qa_error_set(r->error, QA_ERROR_ARGUMENT, r->offset, "Audio candidate asset resolver is absent");
            r->failed = true; return;
        }
        const char *actual_name = qa_audio_asset_name(p->asset);
        if (!actual_name || strlen(actual_name) != name.size || memcmp(actual_name, name.data, name.size)) {
            qa_ac_bad(r, "Saved audio asset name differs from its exact candidate"); return;
        }
        qa_audio_sample *actual = qa_audio_asset_sample(p->asset);
        const qa_sha256_digest *actual_digest = qa_resource_digest(qa_audio_asset_resource(p->asset));
        if (!actual || !actual_digest || memcmp(actual_digest->bytes, identity.bytes, 32) ||
            qa_audio_asset_family(p->asset) != (qa_audio_family)family ||
            actual->sample_rate != p->sample->sample_rate || actual->channels != p->sample->channels ||
            actual->frame_count != p->sample->frame_count || actual->loop_start != p->sample->loop_start ||
            actual->source_bytes_per_sample != p->sample->source_bytes_per_sample ||
            memcmp(actual->samples, p->sample->samples, (size_t)actual->frame_count * actual->channels * 2)) {
            qa_ac_bad(r, "Saved sound asset differs from candidate content"); return;
        }
        if (!qa_audio_sample_retain(actual)) { qa_ac_bad(r, "Saved sound sample reference is exhausted"); return; }
        qa_audio_sample_release(p->sample); p->sample = actual;
    }
    if (!p->sample->frame_count || p->sample->channels != 1) {
        qa_ac_bad(r, "Saved prepared sound is not nonempty mono PCM"); return;
    }
    if (!qa_audio_source_layout_compute(p->sample, m->options.sample_rate, q3 ? QA_AUDIO_Q3 : QA_AUDIO_Q1, &p->layout, r->error)) {
        r->failed = true; return;
    }
    if ((!q3 && !p->layout.frames) || p->layout.frames > INT64_MAX) {
        qa_ac_bad(r, "Saved prepared sound leaves the mixer frame range"); return;
    }
    if (doppler) (void)make_doppler(p, r);
}
static qa_mixer_prepared *get_prepared_ref(qa_source_save_io *r, qa_audio_mixer *m) {
    size_t slot = get_index(r, m->prepared_count);
    if (slot == SIZE_MAX || !m->prepared[slot]) { qa_ac_bad(r, "Saved audio holder has no prepared resource"); return NULL; }
    qa_mixer_prepared *p = m->prepared[slot]; ++p->references; return p;
}
static void get_sound_resource(qa_audio_play *play, qa_mixer_prepared *p) {
    if (!p) return;
    play->sample = p->sample; play->asset = p->asset; play->name = NULL;
}

bool qa_audio_mixer_checkpoint(const qa_audio_mixer *m, const qa_audio_checkpoint_refs *refs,
    qa_buffer *out, qa_error *error) {
    if (!out || !qa_audio_mixer_callbacks_idle(m) ||
        m->event_head != SIZE_MAX || m->event_tail != SIZE_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Mixer checkpoint requires drained callbacks and notifications"); return false;
    }
    qa_source_save_io w;
    if (!qa_source_save_writer(&w, NULL, error)) return false;
    qa_ac_write(&w, "QAMX", 4); qa_ac_u32(&w, m->options.sample_rate);
    qa_ac_u32(&w, m->options.output_channels); qa_ac_u32(&w, m->options.observer != NULL);
    qa_ac_u32(&w, m->transmission_checked ? 2 : m->transmission ? 1 : 0);
    qa_ac_put_listener(&w, refs, &m->listener);
    qa_ac_u64(&w, m->next_voice); qa_ac_u64(&w, m->schedule_order);
    qa_ac_u64(&w, (uint64_t)m->paint_time); qa_ac_u64(&w, (uint64_t)m->sound_time); qa_ac_u64(&w, (uint64_t)m->raw_end);
    qa_ac_double(&w, m->source_begin_offset); qa_ac_float(&w, m->effects_gain);
    qa_ac_u32(&w, m->doppler_enabled); qa_ac_u32(&w, m->enabled); qa_ac_u32(&w, m->random_state);
    qa_ac_u64(&w, m->prepared_count); qa_ac_u64(&w, m->voice_count); qa_ac_u64(&w, m->loop_count);
    qa_ac_u64(&w, m->loop_mix_count); qa_ac_u64(&w, m->position_count); qa_ac_u64(&w, m->transmission_count);
    qa_ac_u64(&w, m->event_count); put_index(&w, m->free_head); put_index(&w, m->event_free); qa_ac_u64(&w, m->event_free_count);
    for (size_t i = 0; !w.failed && i < m->prepared_count; ++i) put_prepared(&w, refs, m->prepared[i]);
    for (size_t i = 0; !w.failed && i < m->voice_count; ++i) {
        const qa_mixer_voice *v = &m->voices[i]; qa_ac_u32(&w, v->state); put_index(&w, v->next_free);
        if (v->state == QA_MIXER_FREE) continue;
        put_index(&w, v->prepared->slot); qa_ac_u32(&w, v->role); qa_ac_u32(&w, v->notification); qa_ac_put_play(&w, refs, &v->sound);
        qa_ac_u64(&w, v->id); qa_ac_u64(&w, v->channel); qa_ac_ref(&w, refs, QA_AUDIO_REFERENCE_KEY, v->key);
        qa_ac_u64(&w, v->order); qa_ac_u64(&w, v->loop_start); qa_ac_u64(&w, (uint64_t)v->start);
        qa_ac_u32(&w, (uint32_t)v->allocated_at); qa_ac_double(&w, v->volume); qa_ac_double(&w, v->attenuation);
        qa_ac_double(&w, v->distance_offset); qa_ac_double(&w, v->stereo_scale); qa_ac_u32(&w, v->unattenuated_mono);
        put_gain(&w, v->gain); put_index(&w, v->start_event); put_index(&w, v->stop_event);
    }
    for (size_t i = 0; !w.failed && i < m->loop_count; ++i) {
        const qa_mixer_loop *v = &m->loops[i]; put_index(&w, v->prepared->slot); qa_ac_put_play(&w, refs, &v->request.sound);
        qa_ac_vec(&w, v->request.velocity); qa_ac_u32(&w, (uint32_t)v->request.frame_number); qa_ac_u32(&w, v->request.persistent);
        qa_ac_u32(&w, v->active); qa_ac_u32(&w, v->doppler); qa_ac_u32(&w, v->merged); put_gain(&w, v->gain);
        qa_ac_float(&w, v->doppler_scale); qa_ac_float(&w, v->old_doppler_scale);
    }
    for (size_t i = 0; !w.failed && i < m->loop_mix_count; ++i) {
        const qa_mixer_loop_mix *v = &m->loop_mixes[i]; put_index(&w, v->prepared->slot); put_gain(&w, v->gain);
        qa_ac_u32(&w, v->family); qa_ac_u32(&w, v->doppler); qa_ac_float(&w, v->doppler_scale); qa_ac_float(&w, v->old_doppler_scale);
    }
    for (size_t i = 0; !w.failed && i < m->position_count; ++i) {
        qa_ac_ref(&w, refs, QA_AUDIO_REFERENCE_ACTOR, m->positions[i].actor);
        qa_ac_ref(&w, refs, QA_AUDIO_REFERENCE_OWNER, m->positions[i].owner);
        qa_ac_vec(&w, m->positions[i].origin); qa_ac_u32(&w, m->positions[i].scoped);
    }
    for (size_t i = 0; !w.failed && i < m->transmission_count; ++i) {
        qa_ac_vec(&w, m->transmissions[i].origin); qa_ac_float(&w, m->transmissions[i].gain);
    }
    for (size_t i = 0; !w.failed && i < m->event_count; ++i) put_index(&w, m->events[i].next);
    for (size_t i = 0; !w.failed && i < QA_MIXER_RAW_FRAMES * 2; ++i) qa_ac_u32(&w, (uint32_t)m->raw[i]);
    return qa_ac_finish(&w, out);
}

static void discard_mixer(qa_audio_mixer *m) {
    if (!m) return;
    for (size_t i = 0; i < m->prepared_count; ++i) {
        qa_mixer_prepared *p = m->prepared[i]; if (!p) continue;
        qa_audio_sample_release(p->sample); qa_audio_asset_release(p->asset); free(p->doppler_sums); free(p);
    }
    free(m->prepared); free(m->voices); free(m->loops); free(m->loop_mixes); free(m->positions);
    free(m->transmissions); free(m->events); free(m->diagnostic_message); free(m);
}
static bool allocate_table(qa_source_save_io *r, uint64_t count, size_t size, size_t minimum, void **out) {
    if (r->failed) return false;
    if (count > SIZE_MAX / size || count > (r->input.size - r->offset) / minimum)
        return qa_ac_bad(r, "Saved audio table exceeds its record extent");
    if (!count) return true;
    *out = calloc((size_t)count, size);
    if (!*out) { qa_error_set(r->error, QA_ERROR_MEMORY, r->offset, "Restoring audio state table"); r->failed = true; return false; }
    return true;
}
static bool validate_free_lists(qa_audio_mixer *m, qa_source_save_io *r) {
    uint8_t *voices = calloc(m->voice_count ? m->voice_count : 1, 1);
    uint8_t *events = calloc(m->event_count ? m->event_count : 1, 1);
    if (!voices || !events) {
        free(voices); free(events); qa_error_set(r->error, QA_ERROR_MEMORY, r->offset, "Checking restored audio slot ownership"); r->failed = true; return false;
    }
    size_t slot = m->free_head, free_events = 0;
    while (slot != SIZE_MAX) {
        if (slot >= m->voice_count || voices[slot] || m->voices[slot].state != QA_MIXER_FREE) { qa_ac_bad(r, "Saved voice free list is invalid"); break; }
        voices[slot] = 1; slot = m->voices[slot].next_free;
    }
    for (size_t i = 0; !r->failed && i < m->voice_count; ++i)
        if ((m->voices[i].state == QA_MIXER_FREE) != (voices[i] != 0)) qa_ac_bad(r, "Saved voice free list omits a slot");
    slot = m->event_free;
    while (!r->failed && slot != SIZE_MAX) {
        if (slot >= m->event_count || events[slot]) { qa_ac_bad(r, "Saved notification free list is invalid"); break; }
        events[slot] = 1; ++free_events; slot = m->events[slot].next;
    }
    if (!r->failed && free_events != m->event_free_count) qa_ac_bad(r, "Saved notification free count differs");
    for (size_t i = 0; !r->failed && i < m->voice_count; ++i) {
        qa_mixer_voice *v = &m->voices[i]; if (v->state == QA_MIXER_FREE) continue;
        bool observed = m->options.observer && v->role == QA_MIXER_EFFECT;
        bool started = v->state == QA_MIXER_STARTED;
        if ((v->role == QA_MIXER_EFFECT &&
                (v->notification != (started ? QA_MIXER_ANNOUNCED : QA_MIXER_UNANNOUNCED) ||
                 (observed && ((v->start_event != SIZE_MAX) != !started || v->stop_event == SIZE_MAX)))) ||
            (!observed && (v->start_event != SIZE_MAX || v->stop_event != SIZE_MAX)) ||
            (v->role != QA_MIXER_EFFECT && v->notification != QA_MIXER_UNANNOUNCED)) {
            qa_ac_bad(r, "Saved voice notification reservations differ from its phase"); break;
        }
        if (v->role == QA_MIXER_EFFECT) {
            if ((!m->options.allocate_voice_id && v->id > m->next_voice)) {
                qa_ac_bad(r, "Saved voice identifier exceeds its allocator continuation"); break;
            }
            for (size_t prior = 0; prior < i; ++prior)
                if (m->voices[prior].state != QA_MIXER_FREE && m->voices[prior].role == QA_MIXER_EFFECT &&
                    m->voices[prior].id == v->id) {
                    qa_ac_bad(r, "Saved effect voices repeat an identifier"); break;
                }
        }
        size_t reserved[] = {v->start_event, v->stop_event};
        for (size_t j = 0; j < 2; ++j) {
            slot = reserved[j]; if (slot == SIZE_MAX) continue;
            if (events[slot]) { qa_ac_bad(r, "Saved notification slot has multiple owners"); break; }
            events[slot] = 1;
        }
    }
    for (size_t i = 0; !r->failed && i < m->event_count; ++i)
        if (!events[i]) qa_ac_bad(r, "Saved notification slot has no owner");
    free(voices); free(events); return !r->failed;
}

bool qa_audio_mixer_restore(qa_bytes bytes, const qa_audio_mixer_options *options,
    const qa_audio_checkpoint_refs *refs, qa_audio_mixer **out, qa_error *error) {
    if (!options || !out || !bytes.data || bytes.size < 20 || memcmp(bytes.data, "QAMX", 4)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid mixer checkpoint arguments or header"); return false;
    }
    qa_source_save_io r;
    if (!qa_source_save_reader(&r, NULL, bytes, error)) return false;
    r.offset = 4;
    if (qa_ac_get32(&r) != options->sample_rate ||
        qa_ac_get32(&r) != options->output_channels || qa_ac_bool(&r) != (options->observer != NULL) ||
        qa_ac_get32(&r) != (refs && refs->geometry_checked ? 2u : refs && refs->geometry ? 1u : 0u))
        return qa_ac_bad(&r, "Saved mixer format or observer admission differs");
    qa_audio_mixer *m = NULL; qa_audio_mixer_options isolated = *options; isolated.initial_voices = 0;
    if (!qa_audio_mixer_create(&isolated, &m, error)) return false;
    qa_ac_get_listener(&r, refs, &m->listener, "Invalid saved audio listener"); m->next_voice = qa_ac_get64(&r); m->schedule_order = qa_ac_get64(&r);
    m->paint_time = qa_ac_geti64(&r); m->sound_time = qa_ac_geti64(&r); m->raw_end = qa_ac_geti64(&r);
    m->source_begin_offset = qa_ac_getdouble(&r); m->effects_gain = qa_ac_getfloat(&r);
    m->doppler_enabled = qa_ac_bool(&r); m->enabled = qa_ac_bool(&r); m->random_state = qa_ac_get32(&r);
    uint64_t counts[7]; for (size_t i = 0; i < 7; ++i) counts[i] = qa_ac_get64(&r);
    bool ok = allocate_table(&r, counts[0], sizeof(*m->prepared), 4, (void **)&m->prepared) &&
        allocate_table(&r, counts[1], sizeof(*m->voices), 12, (void **)&m->voices) &&
        allocate_table(&r, counts[2], sizeof(*m->loops), 48, (void **)&m->loops) &&
        allocate_table(&r, counts[3], sizeof(*m->loop_mixes), 32, (void **)&m->loop_mixes) &&
        allocate_table(&r, counts[4], sizeof(*m->positions), 24, (void **)&m->positions) &&
        allocate_table(&r, counts[5], sizeof(*m->transmissions), 16, (void **)&m->transmissions) &&
        allocate_table(&r, counts[6], sizeof(*m->events), 8, (void **)&m->events);
    if (!ok) { discard_mixer(m); return false; }
    m->prepared_count = m->prepared_capacity = (size_t)counts[0]; m->voice_count = m->voice_capacity = (size_t)counts[1];
    m->loop_count = m->loop_capacity = (size_t)counts[2]; m->loop_mix_count = m->loop_mix_capacity = (size_t)counts[3];
    m->position_count = m->position_capacity = (size_t)counts[4]; m->transmission_count = m->transmission_capacity = (size_t)counts[5];
    m->event_count = m->event_capacity = (size_t)counts[6];
    m->free_head = get_index(&r, m->voice_count); m->event_free = get_index(&r, m->event_count);
    uint64_t free_count = qa_ac_get64(&r);
    if (free_count > m->event_count || (double)m->effects_gain * 255 < INT32_MIN ||
        (double)m->effects_gain * 255 > INT32_MAX)
        qa_ac_bad(&r, "Invalid saved mixer gain or notification count");
    m->event_free_count = (size_t)free_count;
    for (size_t i = 0; !r.failed && i < m->prepared_count; ++i) get_prepared(&r, refs, m, i);
    for (size_t i = 0; !r.failed && i < m->voice_count; ++i) {
        qa_mixer_voice *v = &m->voices[i]; v->state = (qa_mixer_voice_state)qa_ac_get32(&r); v->next_free = get_index(&r, m->voice_count);
        if ((unsigned)v->state > QA_MIXER_STARTED) { qa_ac_bad(&r, "Invalid saved voice phase"); break; }
        if (v->state == QA_MIXER_FREE) continue;
        v->prepared = get_prepared_ref(&r, m); v->role = (qa_mixer_role)qa_ac_get32(&r);
        v->notification = (qa_mixer_notification)qa_ac_get32(&r); qa_ac_get_play(&r, refs, &v->sound); get_sound_resource(&v->sound, v->prepared);
        v->id = qa_ac_get64(&r); v->channel = qa_ac_get64(&r); v->key = qa_ac_getref(&r, refs, QA_AUDIO_REFERENCE_KEY);
        v->order = qa_ac_get64(&r); v->loop_start = qa_ac_get64(&r); v->start = qa_ac_geti64(&r); v->allocated_at = qa_ac_geti32(&r);
        v->volume = qa_ac_getdouble(&r); v->attenuation = qa_ac_getdouble(&r); v->distance_offset = qa_ac_getdouble(&r);
        v->stereo_scale = qa_ac_getdouble(&r); v->unattenuated_mono = qa_ac_bool(&r); v->gain = get_gain(&r);
        v->start_event = get_index(&r, m->event_count); v->stop_event = get_index(&r, m->event_count);
        if ((unsigned)v->role > QA_MIXER_AMBIENT || (unsigned)v->notification > QA_MIXER_FINISHED ||
            (v->role == QA_MIXER_EFFECT && !v->id) ||
            v->volume < 0 || v->attenuation < 0 || v->gain.left < 0 || v->gain.right < 0 ||
            (v->prepared && v->loop_start != QA_AUDIO_NO_LOOP && v->loop_start >= v->prepared->layout.frames))
            qa_ac_bad(&r, "Invalid saved voice continuation");
    }
    for (size_t i = 0; !r.failed && i < m->loop_count; ++i) {
        qa_mixer_loop *v = &m->loops[i]; v->prepared = get_prepared_ref(&r, m); qa_ac_get_play(&r, refs, &v->request.sound);
        get_sound_resource(&v->request.sound, v->prepared); v->request.velocity = qa_ac_getvec(&r);
        v->request.frame_number = qa_ac_geti32(&r); v->request.persistent = qa_ac_bool(&r);
        v->active = qa_ac_bool(&r); v->doppler = qa_ac_bool(&r); v->merged = qa_ac_bool(&r); v->gain = get_gain(&r);
        v->doppler_scale = qa_ac_getfloat(&r); v->old_doppler_scale = qa_ac_getfloat(&r);
        if ((v->doppler && v->doppler_scale > QA_MIXER_CHUNK_FRAMES && (!v->prepared || !v->prepared->doppler_sums)) ||
            v->gain.left < 0 || v->gain.right < 0 || v->doppler_scale < 0 || v->old_doppler_scale < 0)
            qa_ac_bad(&r, "Invalid saved loop continuation");
    }
    for (size_t i = 0; !r.failed && i < m->loop_mix_count; ++i) {
        qa_mixer_loop_mix *v = &m->loop_mixes[i]; v->prepared = get_prepared_ref(&r, m); v->gain = get_gain(&r);
        v->family = (qa_audio_family)qa_ac_get32(&r); v->doppler = qa_ac_bool(&r);
        v->doppler_scale = qa_ac_getfloat(&r); v->old_doppler_scale = qa_ac_getfloat(&r);
        if ((unsigned)v->family > QA_AUDIO_Q3 ||
            (v->doppler && v->doppler_scale > QA_MIXER_CHUNK_FRAMES && (!v->prepared || !v->prepared->doppler_sums)) ||
            v->gain.left < 0 || v->gain.right < 0 || v->doppler_scale < 0 || v->old_doppler_scale < 0)
            qa_ac_bad(&r, "Invalid saved loop mix continuation");
    }
    for (size_t i = 0; !r.failed && i < m->position_count; ++i) {
        m->positions[i].actor = qa_ac_getref(&r, refs, QA_AUDIO_REFERENCE_ACTOR);
        m->positions[i].owner = qa_ac_getref(&r, refs, QA_AUDIO_REFERENCE_OWNER);
        m->positions[i].origin = qa_ac_getvec(&r); m->positions[i].scoped = qa_ac_bool(&r);
        if (m->positions[i].actor == QA_AUDIO_NO_ACTOR) qa_ac_bad(&r, "Invalid saved actor position");
    }
    for (size_t i = 0; !r.failed && i < m->transmission_count; ++i) {
        m->transmissions[i].origin = qa_ac_getvec(&r); m->transmissions[i].gain = qa_ac_getfloat(&r);
        if (m->transmissions[i].gain < 0 || m->transmissions[i].gain > 1) qa_ac_bad(&r, "Invalid saved transmission gain");
    }
    for (size_t i = 0; !r.failed && i < m->event_count; ++i) m->events[i].next = get_index(&r, m->event_count);
    for (size_t i = 0; !r.failed && i < QA_MIXER_RAW_FRAMES * 2; ++i) m->raw[i] = qa_ac_geti32(&r);
    if (!r.failed && r.offset != bytes.size) qa_ac_bad(&r, "Mixer checkpoint has trailing fields");
    if (!r.failed) (void)validate_free_lists(m, &r);
    for (size_t i = 0; !r.failed && i < m->prepared_count; ++i)
        if (m->prepared[i] && !m->prepared[i]->references) qa_ac_bad(&r, "Saved prepared sound has no holder");
    if (r.failed) { discard_mixer(m); return false; }
    m->options = *options;
    m->transmission = refs ? refs->geometry : NULL; m->transmission_user = refs ? refs->geometry_context : NULL;
    m->transmission_checked = refs ? refs->geometry_checked : NULL;
    *out = m; return true;
}
