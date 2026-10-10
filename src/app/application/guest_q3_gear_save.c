#include "guest_q3_gear_private.h"
#include "qa/q3_host_save.h"

static bool blob(qa_source_save_io *io, qa_buffer *bytes)
{
    size_t size = bytes->size;
    if (!qa_source_save_count(io, &size, io->direction == QA_SOURCE_SAVE_READ ? io->input.size : SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        bytes->size = size; bytes->data = size ? malloc(size) : NULL;
        if (size && !bytes->data) return q3gear_fail(io->error, QA_ERROR_MEMORY, "Decoding separate QVM gear owner bytes");
    }
    return qa_source_save_bytes(io, bytes->data, size);
}

static bool header(qa_source_save_io *io, application_q3_gear *gear)
{
    uint8_t magic[4] = {'Q','A','G','E'};
    const char *profile = gear->definition->id, *path = gear->path;
    const char *owner = qa_strings_cstr(qa_session_strings(gear->options.host.session), gear->options.host.owner);
    uint64_t service_owner = gear->options.host.service_owner;
    bool okay = qa_source_save_bytes(io, magic, 4) && !memcmp(magic, "QAGE", 4) &&
        qa_source_save_text(io, &profile) && profile && !strcmp(profile, gear->definition->id) &&
        qa_source_save_text(io, &path) && path && !strcmp(path, gear->path) &&
        qa_source_save_text(io, &owner) && owner &&
        !strcmp(owner, qa_strings_cstr(qa_session_strings(gear->options.host.session), gear->options.host.owner)) &&
        qa_source_save_u64(io, &service_owner) && service_owner == gear->options.host.service_owner;
    return okay;
}

static bool binding_fields(qa_source_save_io *io, q3gear_binding *row)
{
    return qa_source_save_actor(io, &row->actor) && row->actor.registry &&
        qa_source_save_u32(io, &row->pointer) && row->pointer &&
        qa_source_save_vec3(io, &row->origin) && isfinite(row->origin.x) && isfinite(row->origin.y) && isfinite(row->origin.z) &&
        qa_source_save_bool(io, &row->player) && qa_source_save_bool(io,&row->connected) &&
        qa_source_save_bool(io,&row->begun) && (!row->begun || row->connected) &&
        (row->player || (!row->connected && !row->begun));
}

static bool tether_fields(qa_source_save_io *io, q3gear_tether *row)
{
    return qa_source_save_actor(io, &row->owner) && row->owner.registry &&
        qa_source_save_actor(io, &row->actor) && qa_source_save_u32(io, &row->hook) &&
        qa_source_save_bool(io, &row->tracked) && row->tracked && qa_source_save_bool(io, &row->pulling) &&
        (!!row->actor.registry == !!row->hook) && (!row->pulling || row->hook);
}

static bool live_rows(application_q3_gear *gear, qa_error *error)
{
    for (uint32_t i = 0; i < gear->capacity; ++i) {
        q3gear_binding *row = &gear->bindings[i];
        if (!row->actor.registry) continue;
        uint32_t slot, actual;
        if (row->retired || !qa_actors_get(qa_session_actors(gear->options.host.session), row->actor) ||
            row->actor.slot != i || !q3gear_slot(gear, row->pointer, &slot, error) ||
            row->player != (slot < 64) || (row->player && !gear->userinfo[slot]) ||
            !qa_q3_host_actor_slot(gear->host, row->actor, &actual, error) || actual != slot)
            return q3gear_fail(error, QA_ERROR_FORMAT, "Separate QVM gear borrowed binding lost its physical source identity");
    }
    for (uint32_t i = 0; i < gear->capacity; ++i) {
        q3gear_tether *row = &gear->tethers[i];
        if (!row->tracked) continue;
        if (row->orphaned) return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear capture has an unfinished tether retirement");
        uint32_t hook, client, slot, actual;
        if (row->owner.slot != i || !q3gear_hook(gear, row->owner, &hook, &client, error) || hook != row->hook)
            return q3gear_fail(error, QA_ERROR_FORMAT, "Separate QVM gear saved owner differs from its real hook");
        int32_t flags;
        if (!q3gear_word(gear, client + 12, &flags, error) ||
            row->pulling != (hook && ((uint32_t)flags & gear->definition->pulling_flag) != 0))
            return q3gear_fail(error, QA_ERROR_FORMAT, "Separate QVM gear saved pull flag differs from source state");
        if (!row->actor.registry) continue;
        const qa_actor_record *record = qa_actors_get(qa_session_actors(gear->options.host.session), row->actor);
        if (!record || record->owner != gear->options.host.owner || !record->has_source ||
            !q3gear_slot(gear, hook, &slot, error) || record->source_slot != slot ||
            !qa_q3_host_actor_slot(gear->host, row->actor, &actual, error) || actual != slot)
            return q3gear_fail(error, QA_ERROR_FORMAT, "Separate QVM gear tether lost its owned physical source identity");
    }
    return true;
}

bool application_q3_gear_checkpoint(application_q3_gear *gear, qa_buffer *out, qa_error *error)
{
    if (!out || !gear || !gear->initialized || gear->restoring || !application_q3_gear_idle(gear))
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear capture requires its completed source owner");
    qa_buffer parts[4] = {{0}}; qa_qvm_saved_function descriptors[4];
    size_t functions = q3gear_descriptors(gear, descriptors), bindings = 0, tethers = 0;
    for (uint32_t i = 0; i < gear->capacity; ++i) {
        bindings += gear->bindings[i].actor.registry != 0; tethers += gear->tethers[i].tracked;
    }
    bool okay = live_rows(gear, error) && qa_qvm_checkpoint_functions(gear->vm, descriptors, functions, error) &&
        qa_q3_host_checkpoint_services(gear->host, &parts[0], error) &&
        qa_cvars_save_capture(gear->cvars, &parts[1], error) &&
        qa_console_save_capture(gear->console, gear->options.host.session, &parts[2], error) &&
        qa_qvm_checkpoint(gear->vm, &parts[3], error);
    qa_source_save_io io = {0};
    if (okay) okay = qa_source_save_writer(&io, gear->options.host.session, error) && header(&io, gear) &&
        qa_source_save_i32(&io, &gear->milliseconds) && qa_source_save_i32(&io, &gear->frame) &&
        qa_source_save_count(&io, &functions, 4);
    for (size_t i = 0; i < functions && okay; ++i) okay = qa_source_save_u64(&io, &descriptors[i].binding);
    if (okay) okay = qa_source_save_count(&io, &bindings, SIZE_MAX);
    for (uint32_t i = 0; i < gear->capacity && okay; ++i)
        if (gear->bindings[i].actor.registry) okay = binding_fields(&io, &gear->bindings[i]);
    if (okay) okay = qa_source_save_count(&io, &tethers, SIZE_MAX);
    for (uint32_t i = 0; i < gear->capacity && okay; ++i)
        if (gear->tethers[i].tracked) okay = tether_fields(&io, &gear->tethers[i]);
    for (size_t i = 0; i < 1088 && okay; ++i) {
        const char *text = i < 1024 ? gear->configstrings[i] : gear->userinfo[i - 1024];
        okay = qa_source_save_text(&io, &text);
    }
    qa_buffer entities = gear->entities;
    if (okay) okay = blob(&io, &entities);
    for (size_t i = 0; i < 4 && okay; ++i) okay = blob(&io, &parts[i]);
    okay = okay && qa_source_save_finish(&io, out); qa_source_save_dispose(&io);
    for (size_t i = 0; i < 4; ++i) qa_buffer_free(&parts[i]);
    if (!okay && error && error->code == QA_OK) q3gear_fail(error, QA_ERROR_FORMAT, "Separate QVM gear capture lost its complete owner inventory");
    return okay;
}

bool application_q3_gear_restore(application_q3_gear *gear, qa_bytes bytes,
    const qa_console_save_resolvers *resolvers, qa_error *error)
{
    if (!gear || !gear->restoring || gear->initialized || gear->capacity || !application_q3_gear_idle(gear))
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear restore requires its fresh isolated owner");
    qa_source_save_io io = {0}; qa_buffer parts[4] = {{0}}, entities = {0}, services = {0};
    qa_qvm_saved_function constructed[4]; qa_qvm_binding saved[4] = {0};
    size_t expected = q3gear_descriptors(gear, constructed), functions = 0, bindings = 0, tethers = 0;
    int32_t milliseconds = 0, frame = 0; char *text[1088] = {0};
    bool okay = qa_source_save_reader(&io, gear->options.host.session, bytes, error) && header(&io, gear) &&
        qa_source_save_i32(&io, &milliseconds) && milliseconds >= 0 &&
        qa_source_save_i32(&io, &frame) && frame >= 0 &&
        qa_source_save_count(&io, &functions, 4) && functions == expected;
    for (size_t i = 0; i < functions && okay; ++i) okay = qa_source_save_u64(&io, &saved[i]);
    if (okay) okay = qa_source_save_count(&io, &bindings, bytes.size/30);
    for (size_t i = 0; i < bindings && okay; ++i) {
        q3gear_binding row = {0};
        okay = binding_fields(&io, &row) && row.actor.slot < gear->capacity &&
            qa_actors_get(qa_session_actors(gear->options.host.session), row.actor) &&
            !gear->bindings[row.actor.slot].actor.registry;
        for (uint32_t j = 0; j < gear->capacity && okay; ++j)
            if (gear->bindings[j].actor.registry && gear->bindings[j].pointer == row.pointer) okay = false;
        if (okay) gear->bindings[row.actor.slot] = row;
    }
    if (okay) okay = qa_source_save_count(&io, &tethers, bytes.size/32);
    for (size_t i = 0; i < tethers && okay; ++i) {
        q3gear_tether row = {0};
        okay = tether_fields(&io, &row) && row.owner.slot < gear->capacity &&
            qa_actor_id_equal(gear->bindings[row.owner.slot].actor, row.owner) && gear->bindings[row.owner.slot].player &&
            !gear->tethers[row.owner.slot].tracked;
        const qa_actor_record *record = row.actor.registry ? qa_actors_get(qa_session_actors(gear->options.host.session), row.actor) : NULL;
        if (okay && row.actor.registry) okay = record && record->owner == gear->options.host.owner && record->has_source;
        for (uint32_t j = 0; j < gear->capacity && okay && row.actor.registry; ++j)
            if ((gear->tethers[j].actor.registry && (qa_actor_id_equal(gear->tethers[j].actor, row.actor) || gear->tethers[j].hook == row.hook)) ||
                (gear->bindings[j].actor.registry && gear->bindings[j].pointer == row.hook)) okay = false;
        if (okay) gear->tethers[row.owner.slot] = row;
    }
    for (size_t i = 0; i < 1088 && okay; ++i) {
        const char *value = NULL;
        okay = qa_source_save_text(&io, &value) && (!value || q3gear_replace_text(&text[i], value, error));
    }
    if (okay) okay = blob(&io, &entities) && entities.size == gear->entities.size &&
        (!entities.size || !memcmp(entities.data, gear->entities.data, entities.size));
    for (size_t i = 0; i < 4 && okay; ++i) okay = blob(&io, &parts[i]);
    okay = okay && qa_source_save_finish(&io, NULL); qa_source_save_dispose(&io);
    if (okay) okay = qa_q3_host_checkpoint_services(gear->host, &services, error) &&
        services.size == parts[0].size && (!services.size || !memcmp(services.data, parts[0].data, services.size));
    qa_cvars_restore *cvars = NULL;
    if (okay) okay = qa_cvars_save_prepare(gear->cvars, (qa_bytes){parts[1].data, parts[1].size}, &cvars, error) &&
        qa_cvars_save_validate(cvars, error);
    if (okay) okay = qa_qvm_restore_candidate_bindings(gear->vm, (qa_bytes){parts[3].data, parts[3].size}, constructed, saved, functions, error);
    if (okay) {
        gear->same_team = saved[0]; gear->damage = saved[1]; gear->pull = saved[2]; if (functions == 4) gear->mover = saved[3];
        /* Binding qualification above validated the executor before portable
         * host admission can inspect files. */
        qa_bytes host;
        okay = qa_qvm_checkpoint_host((qa_bytes){parts[3].data,parts[3].size}, &host, error) &&
            qa_q3_host_checkpoint_portable_state(host, error);
    }
    if (okay) {
        okay = qa_cvars_save_commit(cvars, error); if (okay) cvars = NULL;
    }
    if (okay) okay = qa_console_save_restore(gear->console, gear->options.host.session, resolvers,
        (qa_bytes){parts[2].data, parts[2].size}, error);
    if (okay) {
        for (size_t i = 0; i < 1088; ++i) {
            if (i < 1024) gear->configstrings[i] = text[i]; else gear->userinfo[i - 1024] = text[i];
            text[i] = NULL;
        }
        gear->milliseconds = milliseconds; gear->frame = frame;
        okay = qa_qvm_restore_candidate(gear->vm, (qa_bytes){parts[3].data, parts[3].size}, error) && live_rows(gear, error);
        int32_t source_time, source_frame;
        if (okay) okay = q3gear_word(gear, gear->definition->globals.time, &source_time, error) && source_time == milliseconds &&
            q3gear_word(gear, gear->definition->globals.frame, &source_frame, error) && source_frame == frame;
        if (okay) gear->initialized = true;
    }
    qa_cvars_save_abort(cvars); qa_buffer_free(&services); qa_buffer_free(&entities);
    for (size_t i = 0; i < 4; ++i) qa_buffer_free(&parts[i]);
    for (size_t i = 0; i < 1088; ++i) free(text[i]);
    if (!okay && error && error->code == QA_OK) q3gear_fail(error, QA_ERROR_FORMAT, "Separate QVM gear continuation differs from its actual source owner");
    return okay;
}

bool application_q3_gear_finish_restore(application_q3_gear *gear, qa_error *error)
{
    if (!gear || !gear->restoring || !gear->initialized || !application_q3_gear_idle(gear))
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear publication lacks its complete imported continuation");
    if (!qa_q3_host_finish_restore(gear->host, error)) return false;
    gear->restoring = false; return true;
}
