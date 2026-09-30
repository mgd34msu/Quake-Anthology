#include "internal.h"

#define WORLD_HEADER_BYTES 32u
#define WORLD_BODY_BYTES 439u
#define WORLD_SPATIAL_BYTES 16u

static void write_ref(qa_net_writer *w, qa_saved_actor_id value)
{ qa_net_write_u64(w, value.generation); qa_net_write_u32(w, value.slot); }
static qa_saved_actor_id read_ref(qa_net_reader *r)
{ qa_saved_actor_id v; v.generation = qa_net_read_u64(r); v.slot = qa_net_read_u32(r); return v; }
static void write_vector(qa_net_writer *w, qa_vec3 v)
{ qa_net_write_f32(w, v.x); qa_net_write_f32(w, v.y); qa_net_write_f32(w, v.z); }
static qa_vec3 read_vector(qa_net_reader *r)
{ qa_vec3 v; v.x = qa_net_read_f32(r); v.y = qa_net_read_f32(r); v.z = qa_net_read_f32(r); return v; }
static void write_bounds(qa_net_writer *w, qa_bounds v)
{ write_vector(w, v.mins); write_vector(w, v.maxs); }
static qa_bounds read_bounds(qa_net_reader *r)
{ qa_bounds v; v.mins = read_vector(r); v.maxs = read_vector(r); return v; }
static void write_body(qa_net_writer *w, qa_body_state v)
{ write_vector(w, v.origin); write_vector(w, v.angles); write_vector(w, v.velocity); write_bounds(w, v.bounds); }
static qa_body_state read_body(qa_net_reader *r)
{
    qa_body_state v = {0};
    v.origin = read_vector(r); v.angles = read_vector(r); v.velocity = read_vector(r); v.bounds = read_bounds(r);
    return v;
}
static void write_collision(qa_net_writer *w, qa_actor_collision v)
{
    qa_net_write_u32(w, v.family); qa_net_write_u32(w, v.shape); qa_net_write_u32(w, v.model);
    qa_net_write_i32(w, v.contents); qa_net_write_u32(w, v.role);
    qa_net_write_i32(w, v.q3_entity_number); qa_net_write_i32(w, v.q3_owner_number);
    qa_net_write_u8(w, (v.inline_model ? 1 : 0) | (v.monster ? 2 : 0) |
                       (v.dead_monster ? 4 : 0) | (v.q1_corpse ? 8 : 0) | (v.has_q3_owner ? 16 : 0));
}
static qa_actor_collision read_collision(qa_net_reader *r)
{
    qa_actor_collision v = {0};
    v.family = (qa_collision_family)qa_net_read_u32(r); v.shape = (qa_shape_kind)qa_net_read_u32(r);
    v.model = qa_net_read_u32(r); v.contents = qa_net_read_i32(r); v.role = (qa_collision_role)qa_net_read_u32(r);
    v.q3_entity_number = qa_net_read_i32(r); v.q3_owner_number = qa_net_read_i32(r);
    uint8_t flags = qa_net_read_u8(r);
    if (flags & ~31u) qa_net_reader_fail(r, "Unknown saved collision flags");
    v.inline_model = (flags & 1) != 0; v.monster = (flags & 2) != 0;
    v.dead_monster = (flags & 4) != 0; v.q1_corpse = (flags & 8) != 0; v.has_q3_owner = (flags & 16) != 0;
    return v;
}

bool qa_save_world_encode(const qa_world_checkpoint *value, qa_buffer *out, qa_error *error)
{
    if (!value || !out || (value->body_count && !value->bodies) ||
        (value->spatial_count && !value->spatial) || value->body_count > UINT32_MAX ||
        value->spatial_count > value->body_count || value->body_count > (SIZE_MAX - WORLD_HEADER_BYTES) / WORLD_BODY_BYTES)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid world checkpoint encoding");
    size_t size = WORLD_HEADER_BYTES + value->body_count * WORLD_BODY_BYTES;
    if (value->spatial_count > (SIZE_MAX - size) / WORLD_SPATIAL_BYTES)
        return persistence_fail(error, QA_ERROR_MEMORY, "World checkpoint codec size overflow");
    size += value->spatial_count * WORLD_SPATIAL_BYTES;
    qa_buffer buffer = {malloc(size), size};
    if (!buffer.data) return persistence_fail(error, QA_ERROR_MEMORY, "Allocating world checkpoint codec");
    qa_net_writer w;
    qa_net_writer_init(&w, buffer.data, size, error);
    qa_net_write_data(&w, "QAWD", 4); qa_net_write_u32(&w, 1);
    qa_net_write_u32(&w, (uint32_t)value->body_count); qa_net_write_u32(&w, (uint32_t)value->spatial_count);
    qa_net_write_u64(&w, value->attachment_order); qa_net_write_u64(&w, value->body_serial);
    for (size_t i = 0; i < value->body_count; ++i) {
        const qa_world_body_checkpoint *v = value->bodies + i;
        write_ref(&w, v->actor); write_ref(&w, v->ground); write_ref(&w, v->stored_ground);
        write_ref(&w, v->linked_ground); write_ref(&w, v->collision_owner); write_ref(&w, v->stored_collision_owner);
        write_ref(&w, v->retained_collision_owner); write_ref(&w, v->anchor);
        write_body(&w, v->state); write_body(&w, v->stored_state); write_body(&w, v->link.state);
        qa_net_write_u64(&w, v->link.link_count); write_bounds(&w, v->link.absolute_bounds);
        write_collision(&w, v->collision); write_collision(&w, v->stored_collision); write_collision(&w, v->retained_collision);
        qa_net_write_u32(&w, v->attachment.follow); write_vector(&w, v->attachment.offset);
        qa_net_write_u64(&w, v->storage_serial); qa_net_write_u64(&w, v->collision_serial);
        qa_net_write_u64(&w, v->attachment_order);
        uint32_t flags = (v->external_body ? 1u : 0) | (v->external_collision ? 2u : 0) |
            (v->has_collision ? 4u : 0) | (v->effective_collision ? 8u : 0) | (v->attached ? 16u : 0) |
            (v->has_ground ? 32u : 0) | (v->has_stored_ground ? 64u : 0) | (v->has_linked_ground ? 128u : 0) |
            (v->has_collision_owner ? 256u : 0) | (v->has_stored_collision_owner ? 512u : 0) |
            (v->has_retained_collision_owner ? 1024u : 0) | (v->has_anchor ? 2048u : 0) | (v->link.linked ? 4096u : 0);
        qa_net_write_u32(&w, flags);
    }
    for (size_t i = 0; i < value->spatial_count; ++i) {
        write_ref(&w, value->spatial[i].actor); qa_net_write_u32(&w, value->spatial[i].sector);
    }
    if (w.failed || qa_net_writer_size(&w) != size) { qa_buffer_free(&buffer); return false; }
    *out = buffer;
    return true;
}

bool qa_save_world_decode(qa_bytes bytes, qa_world_checkpoint *out, qa_error *error)
{
    if (!out || !bytes.data || bytes.size < WORLD_HEADER_BYTES || memcmp(bytes.data, "QAWD", 4))
        return persistence_fail(error, QA_ERROR_FORMAT, "Invalid world checkpoint signature");
    qa_net_reader r;
    qa_net_reader_init(&r, bytes, error); r.bit = 32;
    uint32_t version = qa_net_read_u32(&r);
    qa_world_checkpoint value = {0};
    value.body_count = qa_net_read_u32(&r); value.spatial_count = qa_net_read_u32(&r);
    value.attachment_order = qa_net_read_u64(&r); value.body_serial = qa_net_read_u64(&r);
    size_t remaining = qa_net_reader_remaining(&r);
    if (version != 1 || value.spatial_count > value.body_count ||
        value.body_count > remaining / WORLD_BODY_BYTES)
        return persistence_fail(error, QA_ERROR_FORMAT, "Invalid world checkpoint body extent");
    remaining -= value.body_count * WORLD_BODY_BYTES;
    if (value.spatial_count > remaining / WORLD_SPATIAL_BYTES || remaining != value.spatial_count * WORLD_SPATIAL_BYTES ||
        value.body_count > SIZE_MAX / sizeof(*value.bodies) || value.spatial_count > SIZE_MAX / sizeof(*value.spatial))
        return persistence_fail(error, QA_ERROR_FORMAT, "Invalid world checkpoint spatial extent");
    value.bodies = value.body_count ? calloc(value.body_count, sizeof(*value.bodies)) : NULL;
    value.spatial = value.spatial_count ? calloc(value.spatial_count, sizeof(*value.spatial)) : NULL;
    if ((value.body_count && !value.bodies) || (value.spatial_count && !value.spatial)) {
        qa_world_checkpoint_free(&value);
        return persistence_fail(error, QA_ERROR_MEMORY, "Allocating decoded world checkpoint");
    }
    for (size_t i = 0; i < value.body_count && !r.failed; ++i) {
        qa_world_body_checkpoint *v = value.bodies + i;
        v->actor = read_ref(&r); v->ground = read_ref(&r); v->stored_ground = read_ref(&r);
        v->linked_ground = read_ref(&r); v->collision_owner = read_ref(&r); v->stored_collision_owner = read_ref(&r);
        v->retained_collision_owner = read_ref(&r); v->anchor = read_ref(&r);
        v->state = read_body(&r); v->stored_state = read_body(&r); v->link.state = read_body(&r);
        v->link.link_count = qa_net_read_u64(&r); v->link.absolute_bounds = read_bounds(&r);
        v->collision = read_collision(&r); v->stored_collision = read_collision(&r); v->retained_collision = read_collision(&r);
        v->attachment.follow = (qa_body_follow)qa_net_read_u32(&r); v->attachment.offset = read_vector(&r);
        v->storage_serial = qa_net_read_u64(&r); v->collision_serial = qa_net_read_u64(&r);
        v->attachment_order = qa_net_read_u64(&r);
        uint32_t flags = qa_net_read_u32(&r);
        if (flags & ~8191u) qa_net_reader_fail(&r, "Unknown saved world body flags");
        v->external_body = (flags & 1u) != 0; v->external_collision = (flags & 2u) != 0;
        v->has_collision = (flags & 4u) != 0; v->effective_collision = (flags & 8u) != 0;
        v->attached = (flags & 16u) != 0; v->has_ground = (flags & 32u) != 0;
        v->has_stored_ground = (flags & 64u) != 0; v->has_linked_ground = (flags & 128u) != 0;
        v->has_collision_owner = (flags & 256u) != 0; v->has_stored_collision_owner = (flags & 512u) != 0;
        v->has_retained_collision_owner = (flags & 1024u) != 0; v->has_anchor = (flags & 2048u) != 0;
        v->link.linked = (flags & 4096u) != 0;
    }
    for (size_t i = 0; i < value.spatial_count && !r.failed; ++i) {
        value.spatial[i].actor = read_ref(&r); value.spatial[i].sector = qa_net_read_u32(&r);
    }
    if (!qa_net_reader_finish(&r)) { qa_world_checkpoint_free(&value); return false; }
    *out = value;
    return true;
}
