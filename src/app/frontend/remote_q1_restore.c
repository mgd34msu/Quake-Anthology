#include "remote_q1_restore.h"
#include "remote_q1_private.h"
#include "remote_q1_effects.h"
#include "remote_q1_prediction.h"
#include "remote_q1_skins.h"
#include "save_private.h"
#include "qa/scene_resource_save.h"
#include "qa/scene_save.h"
#include "qa/material_library_save.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct saved_q1 {
    frontend_remote_q1_domain domain;
    uint64_t domain_catalog, catalog, mounts, map_pool, map_resource;
    bool loaded;
} saved_q1;
static bool opening(qa_source_save_io *io, qa_vfs *files, qa_vfs_acquisition *a)
{
    if (!qa_source_save_u64(io, &a->mount) || !qa_source_save_u64(io, &a->resource_id) ||
        !frontend_save_text(io, &a->path) || !frontend_save_text(io, &a->lookup_path) ||
        !frontend_save_text(io, &a->link_source) || !frontend_save_text(io, &a->link_target)) return false;
    if (!a->mount) return !a->resource_id && !a->path && !a->lookup_path && !a->link_source && !a->link_target &&
        !a->opening_present && !a->opening.order && !a->opening.prefix;
    return qa_vfs_acquisition_opening_codec(io, files, a) && a->opening_present;
}
static bool floats(qa_source_save_io *io, float *values, size_t count)
{ for (size_t i = 0; i < count; ++i) if (!qa_source_save_f32(io, values + i)) return false; return true; }
static bool entity(qa_source_save_io *io, qa_q1_entity *v)
{
    return qa_source_save_u32(io, &v->number) && qa_source_save_u32(io, &v->model) &&
        qa_source_save_u32(io, &v->frame) && qa_source_save_u32(io, &v->colormap) &&
        qa_source_save_u32(io, &v->skin) && qa_source_save_u32(io, &v->effects) &&
        floats(io, v->origin, 3) && floats(io, v->angles, 3) && qa_source_save_u8(io, &v->alpha) &&
        qa_source_save_u8(io, &v->scale) && qa_source_save_bool(io, &v->step) &&
        qa_source_save_f32(io, &v->lerp_finish) && qa_source_save_u32(io, &v->qw_flags);
}
static bool entities(qa_source_save_io *io, remote_q1_entities *v)
{
    size_t count = v->count;
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? (io->input.size - io->offset) / 59 : SIZE_MAX / sizeof(*v->rows);
    if (!qa_source_save_count(io, &count, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        v->rows = count ? calloc(count, sizeof(*v->rows)) : NULL; v->count = v->capacity = count;
        if (count && !v->rows) return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!entity(io, v->rows + i)) return false;
        for (size_t j = 0; j < i; ++j) if (v->rows[j].number == v->rows[i].number) return false;
    }
    return true;
}
static bool data(qa_source_save_io *io, qa_q1_clientdata *v)
{
    return qa_source_save_f32(io, &v->viewheight) && qa_source_save_f32(io, &v->idealpitch) &&
        floats(io, v->punch, 3) && floats(io, v->velocity, 3) && qa_source_save_u32(io, &v->items) &&
        qa_source_save_bool(io, &v->onground) && qa_source_save_bool(io, &v->inwater) &&
        qa_source_save_u32(io, &v->weapon_frame) && qa_source_save_u32(io, &v->armor) &&
        qa_source_save_u32(io, &v->weapon_model) && qa_source_save_i32(io, &v->health) &&
        qa_source_save_u32(io, &v->ammo) && qa_source_save_u32(io, &v->shells) &&
        qa_source_save_u32(io, &v->nails) && qa_source_save_u32(io, &v->rockets) &&
        qa_source_save_u32(io, &v->cells) && qa_source_save_u32(io, &v->weapon) && qa_source_save_u8(io, &v->weapon_alpha);
}
static bool protocol(qa_source_save_io *io, qa_net_protocol_id *v)
{
    uint32_t kind = v->kind;
    if (!qa_source_save_u32(io, &kind) || !qa_source_save_u32(io, &v->revision) || !qa_source_save_u32(io, &v->flags)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) v->kind = kind;
    return qa_q1_profile_valid(*v, io->error);
}
static bool domain(qa_source_save_io *io, frontend_remote_q1_domain *v)
{
    qa_command_context *c = &v->command_context; uint32_t dialect = c->dialect, origin = c->origin;
    if (!qa_source_save_u64(io, &v->client.owner) || !qa_source_save_u64(io, &v->client.generation) ||
        !qa_source_save_u32(io, &v->client.slot) || !qa_source_save_u64(io, &v->seat.owner) ||
        !qa_source_save_u32(io, &v->seat.index) || !qa_source_save_u64(io, &v->epoch) ||
        !qa_source_save_u64(io, &v->configuration_generation) || !qa_source_save_u32(io, &v->physical_seat) ||
        !protocol(io, &v->protocol) || !qa_source_save_u32(io, &v->product) ||
        !qa_source_save_u32(io, &v->actor_owner) || !qa_source_save_u32(io, &v->actor_definition) ||
        !qa_source_save_u64(io, &c->session) || !qa_source_save_u64(io, &c->owner) ||
        !qa_source_save_u64(io, &c->client) || !qa_source_save_u32(io, &c->seat) ||
        !qa_source_save_u64(io, &c->registry) || !qa_source_save_u64(io, &c->generation) ||
        !qa_source_save_u32(io, &dialect) || dialect > QA_CONSOLE_Q3 || !qa_source_save_u32(io, &origin) ||
        !qa_source_save_bool(io, &c->direct) || !qa_source_save_bool(io, &c->console_text) ||
        !qa_source_save_actor(io, &c->actor)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) { c->dialect = dialect; c->origin = origin; }
    return !c->script;
}
static bool names(qa_source_save_io *io, char ***rows, size_t *count)
{
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? (io->input.size - io->offset) / 1 : SIZE_MAX / sizeof(**rows);
    if (!qa_source_save_count(io, count, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        *rows = *count ? calloc(*count, sizeof(**rows)) : NULL; if (*count && !*rows) return false;
    }
    for (size_t i = 0; i < *count; ++i) if (!frontend_save_text(io, *rows + i) || !(*rows)[i]) return false;
    return true;
}
static bool player(qa_source_save_io *io, qa_qw_player *v)
{
    uint16_t forward = (uint16_t)v->command.forward, side = (uint16_t)v->command.side, up = (uint16_t)v->command.up;
    if (!qa_source_save_u8(io, &v->slot) || !qa_source_save_u8(io, &v->msec) || !qa_source_save_u16(io, &v->flags) ||
        !floats(io, v->origin, 3) || !floats(io, v->velocity, 3) || !qa_source_save_u32(io, &v->frame) ||
        !qa_source_save_u32(io, &v->model) || !qa_source_save_u32(io, &v->skin) || !qa_source_save_u32(io, &v->effects) ||
        !qa_source_save_u32(io, &v->weapon_frame) || !floats(io, v->command.angles, 3) ||
        !qa_source_save_u16(io, &forward) || !qa_source_save_u16(io, &side) || !qa_source_save_u16(io, &up) ||
        !qa_source_save_u8(io, &v->command.msec) || !qa_source_save_u8(io, &v->command.buttons) ||
        !qa_source_save_u8(io, &v->command.impulse)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        v->command.forward = forward <= INT16_MAX ? (int16_t)forward : (int16_t)((int32_t)forward - 65536);
        v->command.side = side <= INT16_MAX ? (int16_t)side : (int16_t)((int32_t)side - 65536);
        v->command.up = up <= INT16_MAX ? (int16_t)up : (int16_t)((int32_t)up - 65536);
    }
    return true;
}
static bool pending(qa_source_save_io *io, frontend_remote_q1 *row)
{
    size_t count = row->qw_pending_count;
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? (io->input.size - io->offset) / 4 : SIZE_MAX / sizeof(*row->qw_pending);
    if (!qa_source_save_count(io, &count, maximum) || !qa_source_save_count(io, &row->qw_pending_cursor, count)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        row->qw_pending = count ? calloc(count, sizeof(*row->qw_pending)) : NULL;
        row->qw_pending_count = row->qw_pending_capacity = count;
        if (count && !row->qw_pending) return false;
    }
    for (size_t i = 0; i < count; ++i) {
        remote_q1_pending *p = row->qw_pending + i; qa_nq_message *m = &p->message;
        uint32_t op = m->op;
        if (!qa_source_save_u32(io, &op)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) m->op = op;
        switch (m->op) {
        case QA_NQ_NAME: case QA_NQ_LIGHTSTYLE:
            if (!qa_source_save_u8(io, &m->data.indexed_text.index) || !frontend_save_text(io, &p->text) || !p->text) return false;
            m->data.indexed_text.text = p->text; break;
        case QA_NQ_PRINT: case QA_NQ_CENTERPRINT: case QA_NQ_FINALE:
            if (!frontend_save_text(io, &p->text) || !p->text) return false;
            m->data.text = p->text; break;
        case QA_NQ_COLORS: case QA_NQ_FRAGS:
            if (!qa_source_save_u8(io, &m->data.indexed.index) || !qa_source_save_i32(io, &m->data.indexed.value)) return false;
            break;
        case QA_NQ_SETVIEW: case QA_NQ_PAUSE: if (!qa_source_save_u32(io, &m->data.value)) return false; break;
        case QA_NQ_INTERMISSION: break;
        case QA_NQ_CDTRACK:
            if (!qa_source_save_u8(io, &m->data.cd.track) || !qa_source_save_u8(io, &m->data.cd.loop)) return false;
            break;
        case QA_NQ_SETANGLE: if (!floats(io, m->data.angles, 3)) return false; break;
        case QA_NQ_BASELINE: case QA_NQ_STATIC: if (!entity(io, &m->data.entity)) return false; break;
        case QA_NQ_SOUND: case QA_NQ_STATICSOUND: {
            qa_q1_sound *s = &m->data.sound;
            if (!qa_source_save_u32(io, &s->entity) || !qa_source_save_u32(io, &s->channel) ||
                !qa_source_save_u32(io, &s->index) || !qa_source_save_u8(io, &s->volume) ||
                !qa_source_save_f32(io, &s->attenuation) || !floats(io, s->origin, 3)) return false;
            break;
        }
        case QA_NQ_STOPSOUND:
            if (!qa_source_save_u16(io, &m->data.stop_sound.entity) || !qa_source_save_u8(io, &m->data.stop_sound.channel)) return false;
            break;
        case QA_NQ_DAMAGE:
            if (!qa_source_save_u8(io, &m->data.damage.armor) || !qa_source_save_u8(io, &m->data.damage.blood) ||
                !floats(io, m->data.damage.origin, 3)) return false;
            break;
        case QA_NQ_TEMPENTITY: {
            qa_q1_temp *t = &m->data.temporary; uint32_t kind = t->kind;
            if (!qa_source_save_u32(io, &kind) || kind > QA_Q1_TEMP_COLORS || !qa_source_save_u8(io, &t->type) ||
                !qa_source_save_u8(io, &t->count) || !qa_source_save_u8(io, &t->color_start) ||
                !qa_source_save_u8(io, &t->color_length) || !qa_source_save_u16(io, &t->entity) ||
                !floats(io, t->origin, 3) || !floats(io, t->end, 3)) return false;
            if (io->direction == QA_SOURCE_SAVE_READ) t->kind = kind;
            break;
        }
        default: return false;
        }
    }
    return true;
}
static bool fields(qa_source_save_io *io, frontend_remote_q1 *row,
    const frontend_remote_q1_restore_refs *refs, saved_q1 *saved)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[4] = {'Q','1','R','C'}, kick = (uint8_t)row->qw_kick; if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "Q1RC", 4) || !domain(io, &saved->domain) || !protocol(io, &row->protocol) ||
        qa_q1_is_qw(row->protocol) != qa_q1_is_qw(saved->domain.protocol) || !qa_source_save_u64(io, &saved->domain_catalog) ||
        !qa_source_save_bool(io, &row->bound) || !qa_source_save_bool(io, &saved->loaded) ||
        !qa_source_save_bool(io, &row->retired) || !qa_source_save_bool(io, &row->has_data) ||
        !qa_source_save_bool(io, &row->intermission) || !qa_source_save_bool(io, &row->published) ||
        !qa_source_save_u64(io, &saved->catalog) || !qa_source_save_u64(io, &saved->mounts) ||
        !qa_source_save_u32(io, &row->content.product)) return false;
    if (reading && (saved->catalog || saved->mounts)) {
        if (!saved->catalog || !saved->mounts ||
            !qa_application_content_retain_catalog(refs->content, saved->catalog, &row->content.catalog, io->error) ||
            !qa_application_content_claim_view(refs->content, saved->mounts, &row->content.mounts, io->error)) return false;
    }
    if (!qa_source_save_u64(io, &saved->map_pool) ||
        !qa_source_save_u64(io, &saved->map_resource) || !opening(io, row->content.mounts, &row->map_opening) ||
        !qa_source_save_u64(io, &row->revision) || !row->revision || !qa_source_save_u64(io, &row->map_generation) ||
        !qa_source_save_u64(io, &row->received_ns) || !qa_source_save_u64(io, &row->frame_number) ||
        !qa_source_save_u64(io, &row->next_event) || !row->next_event || !qa_source_save_f64(io, &row->seconds) ||
        !qa_source_save_f64(io, &row->previous_seconds) || !qa_source_save_f64(io, &row->fraction) ||
        !isfinite(row->fraction) || row->fraction < 0 || row->fraction > 1 ||
        !qa_source_save_u32(io, &row->max_clients) || row->max_clients > UINT8_MAX ||
        !qa_source_save_u8(io, &row->pending_impulse) ||
        !qa_source_save_u32(io, &row->view_entity) || !qa_source_save_vec3(io, &row->view_angles) || !data(io, &row->data) ||
        !entities(io, &row->current) || !entities(io, &row->previous) || !entities(io, &row->statics) ||
        !entities(io, &row->qw_entities) || !entities(io, &row->qw_nails) || !entities(io, &row->qw_batch_players) || !pending(io, row) ||
        !names(io, &row->models, &row->model_count) || !names(io, &row->sounds, &row->sound_count) ||
        !frontend_save_text(io, &row->skybox) || !qa_source_save_u8(io, &row->sky_found) || row->sky_found > 63 ||
        !qa_source_save_u64(io, &row->saved_world)) return false;
    if (reading) row->loaded = saved->loaded;
    if (reading) {
        row->sound_available = row->sound_count ? calloc(row->sound_count, sizeof(*row->sound_available)) : NULL;
        if (row->sound_count && !row->sound_available) return false;
    }
    for (size_t i = 0; i < row->sound_count; ++i)
        if (!row->sound_available || !qa_source_save_bool(io, row->sound_available + i)) return false;
    for (unsigned i = 0; i < 6; ++i) if (!qa_source_save_u64(io, row->saved_sky + i)) return false;
    for (size_t i = 0; i < 256; ++i) if (!frontend_save_text(io, row->styles + i) || !qa_source_save_i32(io, row->qw_stats + i)) return false;
    for (size_t i = 0; i < 256; ++i) {
        remote_q1_client *c = row->clients + i;
        if (!frontend_save_text(io, &c->name) || !frontend_save_text(io, &c->social) ||
            !frontend_save_text(io, &c->player_info) || !frontend_save_text(io, &c->userinfo) ||
            !qa_source_save_i32(io, &c->frags) || !qa_source_save_i32(io, &c->ping) || !qa_source_save_u8(io, &c->colors) ||
            !qa_source_save_bool(io, &c->present) || !qa_source_save_bool(io, &c->has_ping) ||
            !qa_source_save_bool(io, &c->has_social) || !qa_source_save_bool(io, &c->has_player_info)) return false;
    }
    for (size_t i = 0; i < 32; ++i)
        if (!qa_source_save_bool(io, row->qw_player_valid + i) || !player(io, row->qw_players + i) ||
            (row->qw_player_valid[i] && row->qw_players[i].slot != i)) return false;
    bool qw = row->qw_directory != NULL;
    if (!qa_source_save_bool(io, &qw) || !frontend_save_text(io, &row->qw_directory) || !frontend_save_text(io, &row->qw_level)) return false;
    if (qw) {
        qa_qw_movevars *m = &row->qw.movement;
        if (!row->qw_directory || !row->qw_level || !protocol(io, &row->qw.protocol) ||
            !qa_source_save_i32(io, &row->qw.server_count) || !qa_source_save_u8(io, &row->qw.player_slot) ||
            row->qw.player_slot >= 32 || !qa_source_save_bool(io, &row->qw.spectator) ||
            !qa_source_save_f32(io, &m->gravity) || !qa_source_save_f32(io, &m->stop_speed) ||
            !qa_source_save_f32(io, &m->max_speed) || !qa_source_save_f32(io, &m->spectator_max_speed) ||
            !qa_source_save_f32(io, &m->accelerate) || !qa_source_save_f32(io, &m->air_accelerate) ||
            !qa_source_save_f32(io, &m->water_accelerate) || !qa_source_save_f32(io, &m->friction) ||
            !qa_source_save_f32(io, &m->water_friction) || !qa_source_save_f32(io, &m->entity_gravity)) return false;
        row->qw.game_directory = row->qw_directory; row->qw.level = row->qw_level;
    } else if (row->qw_directory || row->qw_level) return false;
    if (!qa_source_save_bool(io, &row->qw_ready) || !qa_source_save_bool(io, &row->qw_frame) ||
        !qa_source_save_u8(io, &kick) || !qa_source_save_u8(io, &row->qw_pending_track) ||
        !qa_source_save_bool(io, &row->qw_has_pending_track) || !qa_source_save_bool(io, &row->qw_intermission) ||
        !qa_source_save_vec3(io, &row->qw_intermission_origin) || !qa_source_save_vec3(io, &row->qw_intermission_angles)) return false;
    if (reading) row->qw_kick = kick <= INT8_MAX ? (int8_t)kick : (int8_t)((int)kick - 256);
    size_t count = row->actor_count;
    if (!qa_source_save_count(io, &count, 65536)) return false;
    if (reading) {
        row->actors = count ? calloc(count, sizeof(*row->actors)) : NULL; row->actor_count = 0; row->actor_capacity = count;
        if (count && !row->actors) return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!qa_source_save_u32(io, &row->actors[i].number) || !qa_source_save_actor(io, &row->actors[i].id)) return false;
        for (size_t j = 0; j < i; ++j)
            if (row->actors[j].number == row->actors[i].number || qa_actor_id_equal(row->actors[j].id, row->actors[i].id)) return false;
        const qa_actor_record *record = qa_actors_get(row->options.domain.actors, row->actors[i].id);
        if (!record || record->owner != row->options.domain.actor_owner || !record->has_source ||
            record->source_slot != row->actors[i].number) return false;
        if (reading) ++row->actor_count;
    }
    count = frontend_remote_q1_model_count(row);
    size_t maximum = reading ? (io->input.size - io->offset) / 43 : SIZE_MAX;
    if (!qa_source_save_count(io, &count, maximum)) return false;
    remote_q1_model **link = &row->model_cache;
    for (size_t i = 0; i < count; ++i) {
        if (reading) { *link = calloc(1, sizeof(**link)); if (!*link) return false; }
        remote_q1_model *m = *link; uint64_t pool = 0, resource = 0;
        uint64_t parsed = m->saved_model, scene = m->saved_scene, world = m->saved_world;
        if (!reading) {
            parsed = scene = world = 0;
            if (!qa_application_content_resource_id(refs->content, m->resource, &pool, &resource)) return false;
            if (m->world) {
                if (!frontend_world_encode(refs->roots, m->world, &world, io->error)) return false;
            } else {
                if (!frontend_model_encode(refs->models, m->source, &parsed, io->error) || parsed == UINT64_MAX ||
                    !frontend_scene_root_encode(refs->roots, m->scene, &scene, io->error)) return false;
                ++parsed; /* Parsed inventory keys are zero-based; this field has an absent state. */
            }
        }
        if (!frontend_save_text(io, &m->path) || !qa_source_save_u64(io, &pool) || !qa_source_save_u64(io, &resource) ||
            !opening(io, row->content.mounts, &m->opening) || !qa_source_save_bool(io, &m->colors) || !qa_source_save_u8(io, &m->top) ||
            !qa_source_save_u8(io, &m->bottom) || !qa_source_save_u64(io, &parsed) || !qa_source_save_u64(io, &scene) ||
            !qa_source_save_u64(io, &world) || (!!world == !!parsed) || (!!parsed != !!scene)) return false;
        if (reading) {
            m->saved_model = parsed; m->saved_scene = scene; m->saved_world = world;
            m->resource = (qa_resource *)qa_application_content_resource(refs->content, pool, resource);
            if (!m->resource || qa_application_content_pool(refs->content, pool) != qa_vfs_resources(row->content.mounts)) return false;
            qa_resource_retain(m->resource);
        }
        link = &m->next;
    }
    if (!remote_q1_effects_fields(row, io, refs, io->error) || !remote_q1_prediction_fields(row, io, io->error) ||
        !remote_q1_camera_fields(row, io)) return false;
    bool has_skins=row->skins!=NULL;
    if (!qa_source_save_bool(io,&has_skins) || has_skins!=qa_q1_is_qw(row->options.domain.protocol)) return false;
    if (!has_skins) return true;
    qa_buffer bytes={0}; size_t child_count=0;
    if (!reading && !frontend_remote_q1_skins_checkpoint(row->skins,refs,&bytes,io->error)) return false;
    if (!reading) child_count=bytes.size;
    bool ok=qa_source_save_count(io,&child_count,reading?io->input.size-io->offset:SIZE_MAX) && child_count!=0 &&
        (!reading || child_count<=io->input.size-io->offset);
    if (ok && reading) {
        qa_bytes child={io->input.data+io->offset,child_count}; io->offset+=child_count;
        ok=row->options.skin_bindings && frontend_remote_q1_skins_restore(row,row->options.skin_bindings,refs,child,&row->skins,io->error);
    } else if (ok) ok=qa_source_save_bytes(io,bytes.data,child_count);
    qa_buffer_free(&bytes); return ok;
}
static bool retained(frontend_remote_q1 *row, const frontend_remote_q1_restore_refs *refs,
    const saved_q1 *saved, qa_error *error)
{
    const qa_product *product = qa_catalog_product(row->options.domain.catalog, row->options.domain.product);
    const qa_product *content_product = qa_catalog_product(row->content.catalog, row->content.product);
    if (!product || product->family != QA_GAME_Q1 || !saved->domain_catalog ||
        qa_application_content_catalog(refs->content, saved->domain_catalog) != row->options.domain.catalog ||
        !!saved->catalog != !!saved->mounts || !!saved->map_pool != !!saved->map_resource ||
        !!row->map != !!saved->map_resource || (saved->loaded && !row->saved_world) ||
        (saved->loaded && (!row->map || !row->content.mounts)) ||
        (row->published && (!saved->loaded || !row->has_data || !row->view_entity)) ||
        (row->content.mounts && (!content_product || content_product->family != QA_GAME_Q1 ||
            !qa_catalog_product_view_current(row->content.catalog, row->content.product, row->content.mounts))) ||
        (!row->map && (row->map_opening.mount || row->map_opening.resource_id || row->map_opening.path ||
            row->map_opening.lookup_path || row->map_opening.link_source || row->map_opening.link_target)) ||
        (!row->content.mounts && (row->model_cache || row->saved_world || row->sky_found)))
        return remote_q1_fail(error, QA_ERROR_FORMAT, "Q1 cold owner leaves its real catalog/content topology");
    if (row->map && (qa_application_content_pool(refs->content, saved->map_pool) != qa_vfs_resources(row->content.mounts) ||
        qa_resource_pool_find(qa_vfs_resources(row->content.mounts), qa_resource_id(row->map)) != row->map ||
        row->map_opening.resource_id != qa_resource_id(row->map) ||
        !qa_vfs_acquisition_retained(row->content.mounts, &row->map_opening, error))) return false;
    for (remote_q1_model *m = row->model_cache; m; m = m->next) {
        if (!m->path || !*m->path || !m->resource || m->top > 13 || m->bottom > 13 ||
            (row->importing && ((!!m->saved_model == !!m->saved_world) || (!!m->saved_model != !!m->saved_scene))) ||
            qa_resource_pool_find(qa_vfs_resources(row->content.mounts), qa_resource_id(m->resource)) != m->resource ||
            m->opening.resource_id != qa_resource_id(m->resource) || !m->opening.path ||
            !qa_vfs_acquisition_retained(row->content.mounts, &m->opening, error)) return false;
        char *normalized = qa_vfs_normalize_path(m->path, error);
        bool same_path = normalized && !strcmp(normalized, m->opening.path); free(normalized);
        if (!same_path) return false;
        for (remote_q1_model *other = m->next; other; other = other->next)
            if (!strcmp(m->path, other->path) && m->colors == other->colors &&
                (!m->colors || (m->top == other->top && m->bottom == other->bottom))) return false;
    }
    if (row->qw_ready && (!saved->loaded || !row->qw_directory || !qa_q1_is_qw(row->options.domain.protocol))) return false;
    for (unsigned i = 0; i < 6; ++i) if ((row->sky_found & (1u << i)) && !row->saved_sky[i]) return false;
    return true;
}
bool frontend_remote_q1_checkpoint(const frontend_remote_q1 *source,
    const frontend_remote_q1_restore_refs *refs, qa_buffer *out, qa_error *error)
{
    frontend_remote_q1 *row = (frontend_remote_q1 *)source;
    if (!row || !refs || !refs->content || !refs->models || !refs->roots || !refs->scene || !refs->owner ||
        !out || out->data || out->size || row->busy || row->importing || !row->frontend->capture ||
        (row->bound && !row->retired && !remote_q1_live(row, error))) return false;
    saved_q1 saved = {.domain = row->options.domain, .loaded = row->loaded,
        .domain_catalog = qa_application_content_catalog_id(refs->content, row->options.domain.catalog),
        .catalog = qa_application_content_catalog_id(refs->content, row->content.catalog),
        .mounts = qa_application_content_view_id(refs->content, row->content.mounts)};
    frontend_remote_q1 captured = *row; captured.saved_world = 0;
    bool ok = (!row->map || qa_application_content_resource_id(refs->content, row->map, &saved.map_pool, &saved.map_resource)) &&
        (!row->world || frontend_world_encode(refs->roots, row->world, &captured.saved_world, error));
    for (unsigned i = 0; ok && i < 6; ++i) {
        captured.saved_sky[i] = 0;
        if (row->sky_images[i]) ok = frontend_scene_image_encode(refs->scene, row->sky_images[i], captured.saved_sky + i, error);
    }
    qa_source_save_io io = {0}; ++row->busy;
    if (ok) ok = qa_source_save_writer(&io, qa_application_session(row->frontend->application), error) &&
        fields(&io, &captured, refs, &saved) && retained(&captured, refs, &saved, error) && qa_source_save_finish(&io, out);
    --row->busy; qa_source_save_dispose(&io); return ok;
}
bool frontend_remote_q1_restore_prepare(qa_frontend *f, const frontend_remote_q1_options *options,
    const frontend_remote_q1_restore_refs *refs, qa_bytes bytes, frontend_remote_q1 **out, qa_error *error)
{
    if (!f || !f->source_restoring || !options || !refs || !refs->content || !refs->owner || !out || *out ||
        f->application != options->domain.application || !options->current || !options->load_content ||
        !options->service || !options->disconnected || !options->domain.actors || !options->domain.actor_owner ||
        (qa_q1_is_qw(options->domain.protocol) && !options->skin_bindings)) return false;
    frontend_remote_q1 *row = calloc(1, sizeof(*row));
    if (!row) return remote_q1_fail(error, QA_ERROR_MEMORY, "Retaining Q1 isolated cold owner");
    row->frontend = f; row->options = *options; row->importing = true;
    frontend_remote_q1 **tail = &f->remote_q1; while (*tail) tail = &(*tail)->next;
    *tail = row; *out = row;
    qa_catalog_retain(options->domain.catalog);
    saved_q1 saved = {0}; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, qa_application_session(f->application), bytes, error) &&
        fields(&io, row, refs, &saved) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    saved.domain.application = options->domain.application; saved.domain.runtime = options->domain.runtime;
    saved.domain.catalog = qa_application_content_catalog(refs->content, saved.domain_catalog);
    saved.domain.console = options->domain.console; saved.domain.cvars = options->domain.cvars;
    saved.domain.actors = options->domain.actors;
    if (ok) ok = remote_q1_domain_equal(&saved.domain, &options->domain) &&
        options->domain.physical_seat < f->options.seats &&
        (row->bound ? options->domain.client.owner && options->domain.client.generation && options->domain.epoch :
            !options->domain.client.owner && !options->domain.epoch);
    if (ok && saved.map_resource) {
        row->map = (qa_resource *)qa_application_content_resource(refs->content, saved.map_pool, saved.map_resource);
        ok = row->map != NULL; if (ok) qa_resource_retain(row->map);
    }
    if (ok) ok = retained(row, refs, &saved, error);
    if (ok && row->content.mounts) {
        row->images = qa_scene_resources_create_detached(row->content.mounts, error);
        row->materials = row->images ? qa_material_library_create_detached(row->images, error) : NULL;
        ok = row->images && row->materials && qa_audio_bank_create(row->content.mounts, &row->sound_bank, error);
    }
    if (!ok && (!error || error->code == QA_OK)) remote_q1_fail(error, QA_ERROR_FORMAT, "Q1 cold receiver leaves its saved owner graph");
    return ok;
}
bool frontend_remote_q1_import_read(const frontend_remote_q1 *row, frontend_remote_q1_view *out, qa_error *error)
{
    if (!row || !row->importing || !row->frontend->source_restoring) return false;
    return frontend_remote_q1_metadata_read(row, out, error);
}
bool frontend_remote_q1_roots_attach_restored(frontend_remote_q1 *row,
    const frontend_remote_q1_restore_refs *refs, qa_error *error)
{
    if (!row || !row->importing || !row->frontend->source_restoring || row->busy ||
        !refs || !refs->roots || !refs->models || !refs->owner) return false;
    frontend_world_source world = {0}; frontend_scene_owner scope = {0};
    if (row->saved_world && (!frontend_world_inventory_world_at(refs->roots, row->saved_world - 1, &world, &scope) ||
        scope.kind != FRONTEND_SCENE_OWNER_REMOTE_Q1_MAP || scope.owner != refs->owner || scope.row != 1 ||
        world.resource != row->map || world.files != row->content.mounts || world.images != row->images ||
        world.materials != row->materials || (row->world ? world.world != row->world :
            !frontend_world_owner_ready(refs->roots, row->saved_world, FRONTEND_SCENE_OWNER_REMOTE_Q1_MAP, refs->owner, error)))) return false;
    size_t ordinal = 0;
    for (remote_q1_model *m = row->model_cache; m; m = m->next, ++ordinal) {
        if (m->saved_world) {
            frontend_world_source brush = {0}; frontend_scene_owner brush_scope = {0};
            if (!frontend_world_inventory_world_at(refs->roots, m->saved_world - 1, &brush, &brush_scope) ||
                brush_scope.kind != FRONTEND_SCENE_OWNER_REMOTE_Q1_MAP || brush_scope.owner != refs->owner ||
                brush_scope.row != ordinal + 2 || brush.resource != m->resource || brush.files != row->content.mounts ||
                brush.images != row->images || brush.materials != row->materials ||
                (m->world ? brush.world != m->world : !frontend_world_owner_ready(refs->roots, m->saved_world,
                    FRONTEND_SCENE_OWNER_REMOTE_Q1_MAP, refs->owner, error))) return false;
            continue;
        }
        frontend_model_source parsed = {0}; frontend_scene_root_view root = {0};
        if (!m->saved_model || !m->saved_scene || !frontend_model_source_at(refs->models, m->saved_model - 1, &parsed) ||
            !frontend_world_inventory_model_at(refs->roots, m->saved_scene - 1, &root) ||
            parsed.resource != m->resource || parsed.files != row->content.mounts || parsed.parent ||
            root.source.model != parsed.model || root.source.resource != m->resource ||
            root.source.files != row->content.mounts || root.source.parent ||
            root.owner.kind != FRONTEND_SCENE_OWNER_REMOTE_Q1 || root.owner.owner != refs->owner || root.owner.row != ordinal + 1 ||
            (m->scene ? root.scene != m->scene : !frontend_scene_root_owner_ready(refs->roots, m->saved_scene,
                FRONTEND_SCENE_OWNER_REMOTE_Q1, refs->owner, error))) return false;
        if (m->source_lease) {
            frontend_model_source held = {0};
            if (!frontend_model_lease_source(m->source_lease, &held) || held.model != parsed.model ||
                held.resource != m->resource || held.files != row->content.mounts || m->source != parsed.model) return false;
        } else {
            if (!frontend_model_retain(refs->models, parsed.model, &m->source_lease, error)) return false;
            m->source = parsed.model;
        }
    }
    if (row->saved_world && !row->world) {
        row->world = (qa_scene_world *)world.world; frontend_world_adopt(refs->roots, row->saved_world);
    }
    for (remote_q1_model *m = row->model_cache; m; m = m->next) {
        if (m->saved_world && !m->world) {
            if (!frontend_world_decode(refs->roots, m->saved_world, &m->world, error)) return false;
            frontend_world_adopt(refs->roots, m->saved_world);
        } else if (m->saved_scene && !m->scene) {
            if (!frontend_scene_root_decode(refs->roots, m->saved_scene, &m->scene, error)) return false;
            frontend_scene_root_adopt(refs->roots, m->saved_scene);
        }
    }
    return true;
}
bool frontend_remote_q1_restore_finish(frontend_remote_q1 *row,
    const frontend_remote_q1_restore_refs *refs, qa_error *error)
{
    if (!row || !row->importing || !refs || !refs->scene || row->busy ||
        (row->saved_world && !row->world)) return false;
    for (remote_q1_model *m = row->model_cache; m; m = m->next)
        if (m->saved_world ? !m->world : !m->scene || !m->source || !m->source_lease) return false;
    for (unsigned i = 0; i < 6; ++i) {
        const qa_scene_image *image = NULL;
        if ((row->saved_sky[i] && !frontend_scene_image_decode(refs->scene, row->saved_sky[i], &image, error)) ||
            (row->sky_images[i] && row->sky_images[i] != image)) return false;
        const qa_scene_resources *owners[] = {row->images}; size_t index;
        if (image && !qa_scene_image_owner_index(owners, 1, image, &index)) return false;
        if (image && !row->sky_images[i]) { qa_scene_image_retain(image); row->sky_images[i] = image; }
    }
    if (row->bound && !row->retired && (!row->options.current(row->options.context, &row->options.domain, error) ||
        qa_network_epoch(row->options.domain.runtime, row->options.domain.client) != row->options.domain.epoch)) return false;
    if (row->bound && !row->retired) {
        qa_network_q1_client_state actual;
        if (!qa_network_q1_client_state_read(row->options.domain.runtime, row->options.domain.client, &actual, error)) return false;
        qa_net_protocol_id reached = actual.received && !actual.next_service ? actual.before_protocol : actual.protocol;
        if (reached.kind != row->protocol.kind || reached.revision != row->protocol.revision || reached.flags != row->protocol.flags) return false;
    }
    if (!remote_q1_effects_restore_finish(row, refs, error)) return false;
    row->importing = false; return true;
}
