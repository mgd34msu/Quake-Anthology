#include "collision/world_internal.h"
#include <stdlib.h>
#include <string.h>

static bool checkpoint_fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }

static bool save_ref(qa_world *world, qa_actor_id actor, qa_saved_actor_id *saved,
                      bool *present, qa_error *error)
{
    *present = actor.registry != 0;
    return !*present || qa_actors_save_reference(world->actors, actor, saved, error);
}

void qa_world_checkpoint_free(qa_world_checkpoint *value)
{
    if (!value) return;
    free(value->bodies); free(value->spatial);
    *value = (qa_world_checkpoint){0};
}

bool qa_world_checkpoint_capture(qa_world *world, qa_world_checkpoint *out, qa_error *error)
{
    if (!qa_world_idle(world) || world->geometry_admission || !out)
        return checkpoint_fail(error, QA_ERROR_ARGUMENT, "World checkpoint requires an idle owner without admissions");
    uint64_t revision = qa_actors_revision(world->actors), serial = world->body_serial;
    qa_world_checkpoint value = {.body_serial = serial, .attachment_order = world->attachment_order};
    for (uint32_t slot = 0; slot < world->capacity; ++slot) {
        qa_world_body *body = qa_world_raw_body(world, slot);
        if (body && body->present) ++value.body_count;
    }
    for (uint32_t sector = 0; sector < QA_SPATIAL_SECTORS; ++sector)
        for (qa_spatial_member *member = world->sectors[sector].head; member; member = member->next)
            ++value.spatial_count;
    if (value.body_count > SIZE_MAX / sizeof(*value.bodies) ||
        value.spatial_count > SIZE_MAX / sizeof(*value.spatial))
        return checkpoint_fail(error, QA_ERROR_MEMORY, "World checkpoint size overflow");
    value.bodies = value.body_count ? calloc(value.body_count, sizeof(*value.bodies)) : NULL;
    value.spatial = value.spatial_count ? calloc(value.spatial_count, sizeof(*value.spatial)) : NULL;
    if ((value.body_count && !value.bodies) || (value.spatial_count && !value.spatial)) {
        qa_world_checkpoint_free(&value);
        return checkpoint_fail(error, QA_ERROR_MEMORY, "Allocating world checkpoint");
    }
    size_t index = 0;
    bool ok = true;
    for (uint32_t slot = 0; ok && slot < world->capacity; ++slot) {
        qa_world_body *body = qa_world_raw_body(world, slot);
        if (!body || !body->present) continue;
        if (index == value.body_count) { ok = false; break; }
        qa_world_body_checkpoint *record = value.bodies + index++;
        record->storage_serial = body->storage_serial;
        record->collision_serial = body->collision_serial;
        record->external_body = body->external;
        record->external_collision = body->collision_binding.read != NULL;
        record->has_collision = body->has_collision;
        record->attached = body->attached;
        record->attachment_order = body->attachment_order;
        record->stored_state = body->state;
        record->stored_collision = body->collision;
        record->attachment = body->attachment;
        record->link = (qa_body_link_state){body->link_count, body->linked,
                                         body->link.state, body->link.absolute_bounds};
        ok = qa_actors_save_reference(world->actors, body->actor, &record->actor, error) &&
             qa_world_body_read(world, body->actor, &record->state, error);
        qa_error collision_error = {0};
        if (ok) record->effective_collision = qa_world_get_collision(world, body->actor, &record->collision, &collision_error);
        if (collision_error.code != QA_OK) { if (error) *error = collision_error; ok = false; }
        if (ok && body->member) record->retained_collision = body->member->actor.collision;
        if (ok) ok = save_ref(world, record->state.ground, &record->ground, &record->has_ground, error) &&
            save_ref(world, record->stored_state.ground, &record->stored_ground, &record->has_stored_ground, error) &&
            save_ref(world, record->link.state.ground, &record->linked_ground, &record->has_linked_ground, error) &&
            save_ref(world, record->collision.owner, &record->collision_owner, &record->has_collision_owner, error) &&
            save_ref(world, record->stored_collision.owner, &record->stored_collision_owner,
                     &record->has_stored_collision_owner, error) &&
            save_ref(world, record->retained_collision.owner, &record->retained_collision_owner,
                     &record->has_retained_collision_owner, error) &&
            save_ref(world, record->attached ? record->attachment.anchor : (qa_actor_id){0},
                     &record->anchor, &record->has_anchor, error);
        record->state.ground = record->stored_state.ground = record->link.state.ground = (qa_actor_id){0};
        record->collision.owner = record->stored_collision.owner = record->retained_collision.owner = (qa_actor_id){0};
        record->attachment.anchor = (qa_actor_id){0};
        if (qa_actors_revision(world->actors) != revision || world->body_serial != serial ||
            qa_world_find_body(world, body->actor) != body || body->storage_serial != record->storage_serial ||
            body->collision_serial != record->collision_serial || body->link_count != record->link.link_count)
            ok = false;
    }
    index = 0;
    for (uint32_t sector = 0; ok && sector < QA_SPATIAL_SECTORS; ++sector)
        for (qa_spatial_member *member = world->sectors[sector].head; member; member = member->next) {
            if (index == value.spatial_count) { ok = false; break; }
            value.spatial[index].sector = sector;
            ok = qa_actors_save_reference(world->actors, member->actor.body.actor, &value.spatial[index].actor, error);
            ++index;
            if (!ok) break;
        }
    if (!ok || index != value.spatial_count || qa_actors_revision(world->actors) != revision ||
        world->body_serial != serial || world->attachment_order != value.attachment_order) {
        qa_world_checkpoint_free(&value);
        if (!error || error->code == QA_OK)
            checkpoint_fail(error, QA_ERROR_ARGUMENT, "World changed during checkpoint capture");
        return false;
    }
    *out = value;
    return true;
}

static bool restore_ref(qa_world *world, bool present, qa_saved_actor_id saved,
                         qa_actor_id *out, qa_error *error)
{
    *out = (qa_actor_id){0};
    return !present || qa_actors_reference_saved(world->actors, saved, true, out, error);
}

static bool collision_equal(qa_actor_collision a,qa_actor_collision b)
{
    return a.family==b.family && a.shape==b.shape && a.inline_model==b.inline_model &&
        a.model==b.model && a.contents==b.contents && qa_actor_id_equal(a.owner,b.owner) &&
        a.role==b.role && a.monster==b.monster && a.dead_monster==b.dead_monster &&
        a.q1_corpse==b.q1_corpse && a.has_q3_owner==b.has_q3_owner &&
        a.q3_entity_number==b.q3_entity_number && a.q3_owner_number==b.q3_owner_number;
}

typedef struct checkpoint_slot {
    const qa_world_body_checkpoint *record;
    bool seen;
} checkpoint_slot;

bool qa_world_checkpoint_restore(qa_world *world, const qa_world_checkpoint *value, qa_error *error)
{
    if (!qa_world_idle(world) || world->geometry_admission || !value ||
        (value->body_count && !value->bodies) || (value->spatial_count && !value->spatial) ||
        value->body_count > world->capacity || value->spatial_count > value->body_count)
        return checkpoint_fail(error, QA_ERROR_ARGUMENT, "Invalid candidate world checkpoint");
    qa_spatial_member **members = value->spatial_count ? calloc(value->spatial_count, sizeof(*members)) : NULL;
    checkpoint_slot *slots = calloc(world->capacity, sizeof(*slots));
    if (!slots || (value->spatial_count && !members)) {
        free(slots); free(members);
        return checkpoint_fail(error, QA_ERROR_MEMORY, "Allocating candidate spatial checkpoint");
    }
    bool ok = true;
    for (size_t i = 0; ok && i < value->body_count; ++i) {
        const qa_world_body_checkpoint *record = value->bodies + i;
        const qa_actor_record *actor = qa_actors_resolve_saved(world->actors, record->actor);
        if (!actor || slots[actor->id.slot].seen || record->storage_serial > value->body_serial ||
            !record->storage_serial || record->attachment_order > value->attachment_order ||
            (record->attached && (!record->has_anchor || !record->attachment_order))) {
            ok = checkpoint_fail(error, QA_ERROR_FORMAT, "Invalid saved world actor/storage/attachment identity"); break;
        }
        slots[actor->id.slot] = (checkpoint_slot){record, true};
        qa_world_body *body = qa_world_find_body(world, actor->id);
        if (!body && record->external_body) {
            ok = checkpoint_fail(error, QA_ERROR_FORMAT, "Saved external body binding was not rebuilt"); break;
        }
        qa_body_state state = record->state;
        qa_body_state stored_state = record->stored_state;
        qa_body_link_state link = record->link;
        qa_actor_collision collision = record->collision;
        qa_actor_collision stored_collision = record->stored_collision;
        qa_body_attachment attachment = record->attachment;
        ok = restore_ref(world, record->has_ground, record->ground, &state.ground, error) &&
            restore_ref(world, record->has_stored_ground, record->stored_ground, &stored_state.ground, error) &&
            restore_ref(world, record->has_linked_ground, record->linked_ground, &link.state.ground, error) &&
            restore_ref(world, record->has_collision_owner, record->collision_owner, &collision.owner, error) &&
            restore_ref(world, record->has_stored_collision_owner, record->stored_collision_owner,
                         &stored_collision.owner, error) &&
            restore_ref(world, record->has_anchor, record->anchor, &attachment.anchor, error);
        if (!ok) break;
        if ((record->has_collision && !qa_world_collision_validate(world,&stored_collision,error)) ||
            (record->effective_collision && !qa_world_collision_validate(world,&collision,error))) {
            ok=false; break;
        }
        if (record->attached && (attachment.follow < QA_BODY_FOLLOW_TRANSLATION ||
            attachment.follow > QA_BODY_FOLLOW_BOUNDS_MIN || !qa_vec_finite(attachment.offset) ||
            !qa_actors_get(world->actors, attachment.anchor) || qa_actor_id_equal(actor->id, attachment.anchor))) {
            ok = checkpoint_fail(error, QA_ERROR_FORMAT, "Invalid saved body attachment"); break;
        }
        for (size_t j = 0; ok && j < i; ++j)
            if (value->bodies[j].storage_serial == record->storage_serial ||
                (record->attached && value->bodies[j].attached &&
                 value->bodies[j].attachment_order == record->attachment_order))
                ok = checkpoint_fail(error, QA_ERROR_FORMAT, "Duplicate saved body or attachment ordering identity");
        if (!ok) break;
        if (!qa_vec_finite(stored_state.origin) || !qa_vec_finite(stored_state.angles) ||
            !qa_vec_finite(stored_state.velocity) || !qa_bounds_valid(stored_state.bounds) ||
            !qa_vec_finite(state.origin) || !qa_vec_finite(state.angles) || !qa_vec_finite(state.velocity) ||
            !qa_bounds_valid(state.bounds) || (link.linked && (!link.link_count ||
                !qa_vec_finite(link.state.origin) || !qa_vec_finite(link.state.angles) ||
                !qa_vec_finite(link.state.velocity) || !qa_bounds_valid(link.state.bounds) ||
                !qa_bounds_valid(link.absolute_bounds)))) {
            ok = checkpoint_fail(error, QA_ERROR_FORMAT, "Invalid saved body/link fields"); break;
        }
        if (!body) {
            ok = qa_world_body_create(world, actor->id, &state, error);
            if (!ok) break;
            body = qa_world_find_body(world, actor->id);
        }
        if (body->external != record->external_body ||
            (body->collision_binding.read != NULL) != record->external_collision) {
            ok = checkpoint_fail(error, QA_ERROR_FORMAT, "Candidate body/collision owner differs from snapshot"); break;
        }
        if (record->external_body && !qa_world_body_write(world, actor->id, &state, error)) { ok = false; break; }
        body->state = stored_state;
        if (!body->external) body->state = state;
        body->storage_serial = record->storage_serial;
        body->collision_serial = record->collision_serial;
        body->has_collision = record->has_collision;
        body->collision = stored_collision;
        body->attached = record->attached;
        body->attachment = attachment;
        body->attachment_order = record->attachment_order;
        body->linked = link.linked;
        body->link_count = link.link_count;
        body->link = (qa_linked_body){actor->id, link.state, link.absolute_bounds, link.link_count};
        if (record->effective_collision && !record->external_collision) {
            body->has_collision = true;
            body->collision = collision;
        }
    }
    for (uint32_t slot = 0; ok && slot < world->capacity; ++slot) {
        qa_world_body *body = qa_world_raw_body(world, slot);
        if (body && body->present && !slots[slot].seen)
            ok = checkpoint_fail(error, QA_ERROR_FORMAT, "Candidate has a body omitted by the saved world");
        slots[slot].seen = false;
    }
    for (size_t i = 0; ok && i < value->body_count; ++i) {
        const qa_actor_record *actor = qa_actors_resolve_saved(world->actors, value->bodies[i].actor);
        qa_world_body *body = qa_world_find_body(world, actor->id);
        qa_world_body *cursor = body;
        size_t depth = 0;
        while (cursor && cursor->attached) {
            if (++depth > value->body_count) {
                ok = checkpoint_fail(error, QA_ERROR_FORMAT, "Cycle in restored body attachments"); break;
            }
            cursor = qa_world_find_body(world, cursor->attachment.anchor);
            if (!cursor) {
                ok = checkpoint_fail(error, QA_ERROR_FORMAT, "Restored attachment anchor has no body"); break;
            }
        }
        qa_actor_collision effective;
        qa_error collision_error = {0};
        bool present = ok && qa_world_get_collision(world, actor->id, &effective, &collision_error);
        if (ok && (collision_error.code != QA_OK || present != value->bodies[i].effective_collision)) {
            if (collision_error.code != QA_OK && error) *error = collision_error;
            else checkpoint_fail(error, QA_ERROR_FORMAT, "Restored collision ownership differs from snapshot");
            ok = false;
        }
        if(ok && present) {
            qa_actor_collision expected=value->bodies[i].collision;
            if(!restore_ref(world,value->bodies[i].has_collision_owner,value->bodies[i].collision_owner,
                            &expected.owner,error) || !collision_equal(effective,expected)) {
                if(!error || error->code==QA_OK)
                    checkpoint_fail(error,QA_ERROR_FORMAT,"Restored source collision fields differ from checkpoint");
                ok=false;
            }
        }
    }
    for (size_t i = 0; ok && i < value->spatial_count; ++i) {
        const qa_world_spatial_checkpoint *saved = value->spatial + i;
        const qa_actor_record *actor = qa_actors_resolve_saved(world->actors, saved->actor);
        if (!actor || saved->sector >= QA_SPATIAL_SECTORS || slots[actor->id.slot].seen ||
            (i && saved->sector < value->spatial[i - 1].sector)) {
            ok = checkpoint_fail(error, QA_ERROR_FORMAT, "Invalid spatial checkpoint order"); break;
        }
        slots[actor->id.slot].seen = true;
        qa_world_body *body = qa_world_find_body(world, actor->id);
        const qa_world_body_checkpoint *record = slots[actor->id.slot].record;
        if (!body || !body->linked || !record || record->actor.slot != saved->actor.slot ||
            record->actor.generation != saved->actor.generation) {
            ok = checkpoint_fail(error, QA_ERROR_FORMAT, "Saved spatial membership has no linked body"); break;
        }
        qa_actor_collision retained = record->retained_collision;
        if (!restore_ref(world, record->has_retained_collision_owner, record->retained_collision_owner,
                         &retained.owner, error)) { ok = false; break; }
        qa_actor_collision empty = {.family = qa_collision_geometry_family(world->geometry)};
        bool empty_link = collision_equal(retained, empty);
        if(!empty_link && !qa_world_collision_validate(world,&retained,error)) { ok=false; break; }
        members[i] = calloc(1, sizeof(*members[i]));
        if (!members[i]) { ok = checkpoint_fail(error, QA_ERROR_MEMORY, "Allocating restored spatial member"); break; }
        members[i]->actor = (qa_spatial_actor){body->link, retained};
        members[i]->sector = saved->sector;
        uint32_t sector = 0;
        while (world->sectors[sector].axis >= 0) {
            const qa_spatial_sector *split = world->sectors + sector;
            unsigned axis = (unsigned)split->axis;
            if (qa_vec_component(body->link.absolute_bounds.mins, axis) > split->distance) sector = split->front;
            else if (qa_vec_component(body->link.absolute_bounds.maxs, axis) < split->distance) sector = split->back;
            else break;
        }
        if (sector != saved->sector)
            ok = checkpoint_fail(error, QA_ERROR_FORMAT, "Saved spatial sector differs from restored geometry");
    }
    if (ok) {
        qa_spatial_dispose(world);
        for (uint32_t slot = 0; slot < world->capacity; ++slot) {
            qa_world_body *body = qa_world_raw_body(world, slot);
            if (body) body->member = NULL;
        }
        for (size_t i = 0; i < value->spatial_count; ++i) {
            qa_spatial_member *member = members[i];
            qa_spatial_sector *sector = world->sectors + member->sector;
            member->previous = sector->tail;
            if (sector->tail) sector->tail->next = member; else sector->head = member;
            sector->tail = member;
            qa_world_find_body(world, member->actor.body.actor)->member = member;
            members[i] = NULL;
        }
        world->attachment_order = value->attachment_order;
        world->body_serial = value->body_serial;
    }
    for (size_t i = 0; i < value->spatial_count; ++i) free(members ? members[i] : NULL);
    free(members); free(slots);
    return ok;
}
