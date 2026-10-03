#include "remote_q3_modules_private.h"
#include "remote_q3_modules_save.h"
#include "qa/source_save.h"
#include "qa/q3_presentation_save.h"
#include "qa/audio_save.h"
#include "qa/audio_music_prepare.h"
#include "audio_identity_save.h"
#include <math.h>

typedef struct wrapper_saved {
    uint32_t kind, physical, receiver, seat;
    uint64_t identity, generation, epoch;
    remote_module_saved *roles;
    size_t count;
} wrapper_saved;

void frontend_remote_modules_saved_dispose(remote_module_saved *roles, size_t count)
{
    for (size_t i = 0; roles && i < count; ++i) {
        qa_buffer_free(&roles[i].scene); qa_buffer_free(&roles[i].media);
        qa_buffer_free(&roles[i].equipment); qa_buffer_free(&roles[i].music);
        free(roles[i].music_intro); free(roles[i].music_loop);
    }
    free(roles);
}
static bool blob(qa_source_save_io *io, qa_buffer *value)
{
    size_t size = value->size;
    if (!qa_source_save_count(io, &size, SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (size > io->input.size - io->offset) return false;
        value->data = size ? malloc(size) : NULL; value->size = size;
        if (size && !value->data) return frontend_fail(io->error, QA_ERROR_MEMORY, "Retaining role continuation");
    }
    return qa_source_save_bytes(io, value->data, size);
}
static bool fields(qa_source_save_io *io, wrapper_saved *saved)
{
    uint8_t signature[8] = {'Q','R','M','W',0,0,0,0};
    const uint8_t expected[8] = {'Q','R','M','W',0,0,0,0}; if (!qa_source_save_bytes(io, signature, 8) || memcmp(signature, expected, 8) ||
        !qa_source_save_u32(io, &saved->kind) || saved->kind > REMOTE_MODULE_INITIAL ||
        !qa_source_save_u64(io, &saved->identity) || !saved->identity ||
        !qa_source_save_u32(io, &saved->physical) || !qa_source_save_u32(io, &saved->receiver) ||
        !qa_source_save_u32(io, &saved->seat) || !qa_source_save_u64(io, &saved->generation) ||
        !qa_source_save_u64(io, &saved->epoch) || !saved->generation || !saved->epoch ||
        !qa_source_save_count(io, &saved->count, 2) || !saved->count) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        saved->roles = calloc(saved->count, sizeof(*saved->roles));
        if (!saved->roles) return frontend_fail(io->error, QA_ERROR_MEMORY, "Retaining saved role inventory");
    }
    for (size_t i = 0; i < saved->count; ++i) {
        remote_module_saved *role = &saved->roles[i];
        if (!qa_source_save_u32(io, &role->role) ||
            (role->role != QA_QVM_UI && role->role != QA_QVM_CGAME) ||
            (saved->kind == REMOTE_MODULE_INITIAL && role->role != QA_QVM_UI) ||
            !qa_source_save_string(io, &role->service_owner) || !role->service_owner ||
            !qa_source_save_u64(io, &role->keys) || !role->keys ||
            !blob(io, &role->scene) || !role->scene.size || !blob(io, &role->media) || !role->media.size ||
            !blob(io, &role->equipment) ||
            (role->role == QA_QVM_CGAME ? !role->equipment.size : role->equipment.size != 0) ||
            !qa_source_save_bool(io, &role->has_music) || !qa_source_save_bool(io, &role->music_attached) ||
            !qa_source_save_bool(io, &role->music_looping) || !qa_source_save_owned_text(io, &role->music_intro) ||
            !qa_source_save_owned_text(io, &role->music_loop) || !blob(io, &role->music) ||
            (role->music_attached && (!role->has_music || role->music.size)) ||
            (role->has_music && !role->music_attached && !role->music.size) ||
            (!role->has_music && (role->music.size || role->music_intro || role->music_loop || role->music_looping)) ||
            !qa_source_save_bool(io, &role->has_listener)) return false;
        for (size_t j = 0; j < i; ++j)
            if (role->role == saved->roles[j].role || role->service_owner == saved->roles[j].service_owner) return false;
        if (role->has_listener) {
            qa_audio_listener *listener = &role->listener;
            if (!qa_source_save_u32(io, &listener->seat) || listener->seat != saved->physical ||
                !qa_source_save_u64(io, &listener->actor) || !qa_source_save_vec3(io, &listener->origin)) return false;
            for (size_t axis = 0; axis < 3; ++axis) if (!qa_source_save_vec3(io, &listener->axis[axis])) return false;
            if (!qa_source_save_f32(io, &listener->gain) || !isfinite(listener->gain) || listener->gain < 0 ||
                !qa_source_save_bool(io, &listener->underwater)) return false;
            if (!qa_vec_finite(listener->origin)) return false;
            for (size_t axis = 0; axis < 3; ++axis) if (!qa_vec_finite(listener->axis[axis])) return false;
        }
    }
    return true;
}
static remote_module_lease *lease_at(frontend_remote_q3_modules *owner, size_t index)
{
    remote_module_lease *lease = owner->leases;
    while (lease && index--) lease = lease->next;
    return lease;
}
static bool copy_text(const char *text_value, char **out, qa_error *error)
{
    if (!text_value) return true;
    size_t size = strlen(text_value) + 1; *out = malloc(size);
    if (!*out) return frontend_fail(error, QA_ERROR_MEMORY, "Copying role music identity");
    memcpy(*out, text_value, size); return true;
}
bool frontend_remote_q3_modules_checkpoint(const frontend_remote_q3_modules *owner,
    const frontend_remote_q3_modules_save_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!owner || !refs || !refs->movies || !out || out->data || out->size ||
        !frontend_remote_q3_modules_capture_returned(owner, error)) return false;
    const qa_application_q3_remote_source *source = frontend_remote_modules_source(owner);
    wrapper_saved saved = {.kind = owner->kind, .receiver = source->receiver.receiver,
        .seat = source->receiver.seat, .generation = source->configuration_generation, .epoch = source->connection_epoch,
        .identity = owner->kind == REMOTE_MODULE_INITIAL ? owner->basis.initial.view.identity : owner->basis.decoded.view.identity,
        .physical = owner->kind == REMOTE_MODULE_INITIAL ? owner->basis.initial.view.physical_seat : owner->basis.decoded.view.physical_seat,
        .count = frontend_remote_q3_modules_role_count(owner)};
    saved.roles = calloc(saved.count, sizeof(*saved.roles));
    if (!saved.roles) return frontend_fail(error, QA_ERROR_MEMORY, "Capturing actual role inventory");
    bool ok = true;
    for (size_t i = 0; ok && i < saved.count; ++i) {
        frontend_remote_q3_module_topology topology; qa_q3_movie_checkpoint_refs movies;
        remote_module_saved *role = &saved.roles[i];
        remote_module_lease *lease = lease_at((frontend_remote_q3_modules *)owner, i);
        ok = frontend_remote_q3_modules_role_read(owner, i, &topology, error) && refs->movies(refs->context, &topology, &movies, error);
        if (!ok) break;
        if (lease->service_owner > UINT32_MAX) {
            ok = frontend_fail(error, QA_ERROR_FORMAT, "Remote module service namespace exceeds its actual string identity");
            break;
        }
        role->role = lease->role; role->service_owner = (qa_string_id)lease->service_owner;
        role->keys = frontend_key_profile_id(lease->keys);
        role->has_music = lease->music != NULL; role->music_attached = topology.music_attached;
        role->music_looping = lease->music_looping; role->has_listener = lease->has_listener; role->listener = lease->listener;
        if (role->has_listener && role->listener.actor != QA_AUDIO_NO_ACTOR &&
            !frontend_audio_id_read(owner->frontend, role->listener.actor, NULL, NULL)) { ok = false; break; }
        if (owner->restoring) {
            const remote_module_saved *previous = NULL;
            for (size_t j = 0; j < owner->saved_count; ++j)
                if (owner->saved[j].role == (uint32_t)lease->role &&
                    owner->saved[j].service_owner == lease->service_owner) { previous = owner->saved + j; break; }
            bool shared = false;
            if (!previous || !previous->media_restored) {
                ok = frontend_fail(error, QA_ERROR_FORMAT, "Restored role witness lacks its completed original media binding");
                break;
            }
            if (!qa_q3_presentation_media_binding_read(
                (qa_bytes){previous->media.data, previous->media.size}, &shared, error)) { ok = false; break; }
            if (shared != (lease->cinematics != NULL)) {
                ok = frontend_fail(error, QA_ERROR_FORMAT, "Restored role witness changed its original media binding");
                break;
            }
        }
        ok = copy_text(lease->music_intro, &role->music_intro, error) && copy_text(lease->music_loop, &role->music_loop, error) &&
            qa_q3_presentation_scene_checkpoint(lease->presentation, &role->scene, error) &&
            qa_q3_presentation_media_checkpoint(lease->presentation, &movies, &role->media, error) &&
            (!lease->equipment || frontend_equipment_source_checkpoint(lease->equipment, &role->equipment, error));
        if (ok && role->music_attached)
            ok = qa_audio_engine_bus_music(owner->frontend->audio, lease->service_owner) == lease->music &&
                qa_audio_engine_music_ready(owner->frontend->audio, lease->service_owner, saved.physical, 1);
        else if (ok && role->has_music) ok = qa_audio_music_checkpoint(lease->music, &role->music, error);
    }
    qa_source_save_io io = {0};
    if (ok) ok = qa_source_save_writer(&io, qa_application_session(owner->application), error) &&
        fields(&io, &saved) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); frontend_remote_modules_saved_dispose(saved.roles, saved.count);
    return ok;
}
static bool restore(qa_frontend *f, frontend_remote_q3 *row, frontend_remote_q3_initial *initial,
    qa_bytes wrapper, qa_bytes modules, frontend_remote_q3_modules **out, qa_error *error)
{
    wrapper_saved saved = {0}; qa_source_save_io io = {0};
    bool ok = f && out && !*out && qa_source_save_reader(&io, qa_application_session(f->application), wrapper, error) &&
        fields(&io, &saved) && qa_source_save_finish(&io, NULL);
    frontend_remote_q3_resources decoded = {0}; frontend_remote_q3_initial_view connecting = {0};
    if (ok) ok = row ? frontend_remote_q3_resources_import_read(row, &decoded, error) :
        frontend_remote_q3_initial_import_read(initial, &connecting, error);
    const qa_application_q3_remote_source *source = row ? &decoded.domain.source : &connecting.attempt.source;
    if (ok) ok = saved.kind == (uint32_t)(row ? REMOTE_MODULE_DECODED : REMOTE_MODULE_INITIAL) &&
        saved.identity == (row ? decoded.identity : connecting.identity) &&
        saved.physical == (row ? decoded.physical_seat : connecting.physical_seat) &&
        saved.receiver == source->receiver.receiver && saved.seat == source->receiver.seat &&
        saved.generation == source->configuration_generation && saved.epoch == source->connection_epoch;
    if (ok) ok = frontend_remote_modules_construct_restored(f, row, initial, &saved.roles, saved.count,
        wrapper, modules, out, error);
    frontend_remote_modules_saved_dispose(saved.roles, saved.count); qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Invalid actual module wrapper continuation");
    return ok;
}
bool frontend_remote_q3_modules_restore_decoded(frontend_remote_q3 *row, qa_bytes wrapper,
    qa_bytes modules, frontend_remote_q3_modules **out, qa_error *error)
{ return row && restore(row->frontend, row, NULL, wrapper, modules, out, error); }
bool frontend_remote_q3_modules_restore_initial(qa_frontend *f, frontend_remote_q3_initial *initial,
    qa_bytes wrapper, qa_bytes modules, frontend_remote_q3_modules **out, qa_error *error)
{ return initial && restore(f, NULL, initial, wrapper, modules, out, error); }

bool frontend_remote_q3_modules_restore_continuation(frontend_remote_q3_modules *owner,
    const frontend_remote_q3_modules_save_refs *refs, qa_error *error)
{
    if (!owner || !owner->restoring || !refs || !refs->movies ||
        !frontend_remote_q3_modules_capture_returned(owner, error)) return false;
    if (!frontend_remote_modules_restore_renderer_parameters(owner, error)) return false;
    for (size_t i = 0; i < owner->saved_count; ++i) {
        remote_module_saved *saved = &owner->saved[i]; remote_module_lease *lease = owner->leases;
        while (lease && (lease->role != (qa_qvm_role)saved->role || lease->service_owner != saved->service_owner)) lease = lease->next;
        if (!lease || !saved->prepared) return false;
        if (!saved->music_restored) {
            if (saved->has_listener && saved->listener.actor != QA_AUDIO_NO_ACTOR &&
                !frontend_audio_id_read(owner->frontend, saved->listener.actor, NULL, NULL))
                return frontend_fail(error, QA_ERROR_FORMAT, "Saved module listener has no actual restored audio actor identity");
            if (saved->has_music) {
                if (saved->music_attached) {
                    uint32_t physical = owner->kind == REMOTE_MODULE_INITIAL ?
                        owner->basis.initial.view.physical_seat : owner->basis.decoded.view.physical_seat;
                    qa_audio_music *player = qa_audio_engine_bus_music(owner->frontend->audio, lease->service_owner);
                    if (!player || !qa_audio_engine_music_ready(owner->frontend->audio,
                        lease->service_owner, physical, 1) || !qa_audio_music_retain(player, error)) return false;
                    lease->music = player;
                } else if (!qa_audio_music_restore((qa_bytes){saved->music.data,saved->music.size}, &lease->music, error)) return false;
                lease->music_attached = saved->music_attached;
            }
            lease->music_intro = saved->music_intro; saved->music_intro = NULL;
            lease->music_loop = saved->music_loop; saved->music_loop = NULL;
            lease->music_looping = saved->music_looping; lease->has_listener = saved->has_listener; lease->listener = saved->listener;
            saved->music_restored = true;
        }
        size_t index = 0; for (remote_module_lease *p = owner->leases; p != lease; p = p->next) ++index;
        frontend_remote_q3_module_topology topology; qa_q3_movie_checkpoint_refs movies;
        if (!frontend_remote_q3_modules_role_read(owner, index, &topology, error) ||
            !refs->movies(refs->context, &topology, &movies, error)) return false;
        if (!saved->scene_restored) {
            if (!qa_q3_presentation_scene_restore(lease->presentation, (qa_bytes){saved->scene.data,saved->scene.size}, error)) return false;
            saved->scene_restored = true;
        }
        if (!saved->media_restored) {
            if (!qa_q3_presentation_media_restore(lease->presentation, &movies, lease->service_owner,
                (double)owner->frontend->wall_time_ns / 1000000.0, (qa_bytes){saved->media.data,saved->media.size}, error)) return false;
            lease->legacy_cinematics = !lease->cinematics;
            saved->media_restored = true;
        }
        if (!saved->music_origin_restored) {
            if (!frontend_remote_modules_music_restore_origin(lease, error)) return false;
            saved->music_origin_restored = true;
        }
        if (!saved->equipment_restored) {
            if (lease->equipment) {
                bool initialized, succeeded;
                if (!qa_application_native_q3_client_modules_initialization_read(owner->modules, lease->role,
                    &initialized, &succeeded, error)) return false;
                qa_application_q3_client_context client = frontend_remote_modules_source(owner)->receiver;
                client.service_owner = lease->service_owner; client.frontend_lifetime = lease;
                client.initialized = initialized && succeeded;
                if (!lease->network.source_actor(lease->network.context,
                    (uint32_t)owner->basis.decoded.view.domain.initial.client_number,
                    &client.source_actor, &initialized, error)) return false;
                if (!initialized) client.source_actor = (qa_actor_id){0};
                if (!frontend_equipment_source_restore(lease->equipment, &client,
                    (qa_bytes){saved->equipment.data,saved->equipment.size}, error)) return false;
            }
            saved->equipment_restored = true;
        }
    }
    return true;
}
bool frontend_remote_q3_modules_finish_restore(frontend_remote_q3_modules *owner,
    const frontend_remote_q3_modules_save_refs *refs, qa_error *error)
{
    if (!owner || !owner->restoring || !frontend_remote_q3_modules_restore_continuation(owner, refs, error)) return false;
    if (!owner->lower_finished) {
        if (!qa_application_native_q3_client_modules_finish_restore(owner->modules, error)) return false;
        owner->lower_finished = true;
    }
    qa_buffer witness = {0};
    bool ok = frontend_remote_q3_modules_checkpoint(owner, refs, &witness, error) &&
        witness.size == owner->saved_bytes.size && !memcmp(witness.data, owner->saved_bytes.data, witness.size);
    qa_buffer_free(&witness);
    if (!ok) return frontend_fail(error, QA_ERROR_FORMAT, "Restored module wrapper differs from its saved continuation");
    owner->restoring = false;
    frontend_remote_modules_saved_dispose(owner->saved, owner->saved_count); owner->saved = NULL; owner->saved_count = 0;
    qa_buffer_free(&owner->saved_bytes); return true;
}
