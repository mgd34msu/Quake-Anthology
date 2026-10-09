#include "internal.h"

#define WORLD_HEADER_BYTES 28u
#define WORLD_BODY_MIN_BYTES 38u
#define WORLD_BODY_MAX_BYTES 507u
#define WORLD_SPATIAL_BYTES 16u

enum {
    WORLD_COLLISION_SERIAL = 8192u,
    WORLD_ATTACHMENT_ORDER = 16384u,
    WORLD_LINK_COUNT = 32768u,
    WORLD_ATTACHMENT_FOLLOW = 65536u,
    WORLD_FLAGS = 131071u
};

static void write_ref(qa_net_writer *w, qa_saved_actor_id value)
{ qa_net_write_u64(w, value.generation); qa_net_write_u32(w, value.slot); }
static qa_saved_actor_id read_ref(qa_net_reader *r)
{ qa_saved_actor_id v; v.generation = qa_net_read_u64(r); v.slot = qa_net_read_u32(r); return v; }

static void write_reference(qa_net_writer *w, qa_saved_actor_id id,
    qa_actor_reference_kind kind, qa_actor_owner owner)
{
    qa_net_write_u8(w, (uint8_t)kind);
    if (kind == QA_ACTOR_REFERENCE_SOURCE && !id.generation) {
        qa_net_write_u32(w, owner); qa_net_write_u32(w, id.slot);
    } else if (kind == QA_ACTOR_REFERENCE_LIFETIME && !owner) write_ref(w, id);
    else qa_net_writer_fail(w, "Invalid saved actor reference kind");
}

static void read_reference(qa_net_reader *r, qa_saved_actor_id *id,
    qa_actor_reference_kind *kind, qa_actor_owner *owner)
{
    *kind = (qa_actor_reference_kind)qa_net_read_u8(r);
    if (*kind == QA_ACTOR_REFERENCE_SOURCE) {
        *owner = qa_net_read_u32(r); id->slot = qa_net_read_u32(r);
    } else if (*kind == QA_ACTOR_REFERENCE_LIFETIME) *id = read_ref(r);
    else qa_net_reader_fail(r, "Invalid saved actor reference kind");
}

/* Compare the stored float bits so signed zero and retained link snapshots
 * survive as well as the authoritative body. Missing fields use the baseline. */
static void write_floats(qa_net_writer *w, const float *values, const float *base, unsigned count)
{
    uint16_t mask = 0;
    for (unsigned i = 0; i < count; ++i)
        if (memcmp(values + i, base + i, sizeof(float))) mask |= (uint16_t)(1u << i);
    if (count > 8) qa_net_write_u16(w, mask); else qa_net_write_u8(w, (uint8_t)mask);
    for (unsigned i = 0; i < count; ++i)
        if (mask & (1u << i)) qa_net_write_f32(w, values[i]);
}

static void read_floats(qa_net_reader *r, float *values, const float *base, unsigned count)
{
    uint16_t mask = count > 8 ? qa_net_read_u16(r) : qa_net_read_u8(r);
    if (mask & ~((1u << count) - 1u)) qa_net_reader_fail(r, "Unknown saved world field mask");
    for (unsigned i = 0; i < count; ++i)
        values[i] = mask & (1u << i) ? qa_net_read_f32(r) : base[i];
}

static void body_fields(qa_body_state v, float fields[15])
{
    float value[15] = {v.origin.x, v.origin.y, v.origin.z, v.angles.x, v.angles.y, v.angles.z,
        v.velocity.x, v.velocity.y, v.velocity.z, v.bounds.mins.x, v.bounds.mins.y, v.bounds.mins.z,
        v.bounds.maxs.x, v.bounds.maxs.y, v.bounds.maxs.z};
    memcpy(fields, value, sizeof(value));
}

static void write_body(qa_net_writer *w, qa_body_state v, qa_body_state base)
{
    float fields[15], baseline[15];
    body_fields(v, fields); body_fields(base, baseline);
    write_floats(w, fields, baseline, 15);
}

static qa_body_state read_body(qa_net_reader *r, qa_body_state base)
{
    float fields[15], baseline[15];
    body_fields(base, baseline); read_floats(r, fields, baseline, 15);
    return (qa_body_state){.origin = {fields[0], fields[1], fields[2]},
        .angles = {fields[3], fields[4], fields[5]}, .velocity = {fields[6], fields[7], fields[8]},
        .bounds = {{fields[9], fields[10], fields[11]}, {fields[12], fields[13], fields[14]}}};
}

static void write_bounds(qa_net_writer *w, qa_bounds v)
{
    float fields[6] = {v.mins.x, v.mins.y, v.mins.z, v.maxs.x, v.maxs.y, v.maxs.z};
    const float base[6] = {0};
    write_floats(w, fields, base, 6);
}

static qa_bounds read_bounds(qa_net_reader *r)
{
    float fields[6]; const float base[6] = {0};
    read_floats(r, fields, base, 6);
    return (qa_bounds){{fields[0], fields[1], fields[2]}, {fields[3], fields[4], fields[5]}};
}

static uint8_t collision_flags(qa_actor_collision v)
{
    return (uint8_t)((v.inline_model ? 1u : 0) | (v.monster ? 2u : 0) |
        (v.dead_monster ? 4u : 0) | (v.q1_corpse ? 8u : 0) | (v.has_q3_owner ? 16u : 0));
}

static void collision_fields(qa_actor_collision v, uint64_t fields[9])
{
    uint64_t value[9] = {(uint32_t)v.family, (uint32_t)v.shape, v.model, v.contents.lo,
        v.contents.hi, (uint32_t)v.q1_opaque_token, (uint32_t)v.role,
        (uint32_t)v.q3_entity_number, (uint32_t)v.q3_owner_number};
    memcpy(fields, value, sizeof(value));
}

static void write_collision(qa_net_writer *w, qa_actor_collision v, qa_actor_collision base)
{
    uint64_t fields[9], baseline[9];
    collision_fields(v, fields); collision_fields(base, baseline);
    uint8_t flags = collision_flags(v);
    uint16_t mask = flags != collision_flags(base) ? 512u : 0;
    for (unsigned i = 0; i < 9; ++i)
        if (fields[i] != baseline[i]) mask |= (uint16_t)(1u << i);
    qa_net_write_u16(w, mask);
    for (unsigned i = 0; i < 9; ++i) {
        if (!(mask & (1u << i))) continue;
        if (i == 3 || i == 4) qa_net_write_u64(w, fields[i]);
        else if (i == 5) qa_net_write_i32(w, v.q1_opaque_token);
        else qa_net_write_u32(w, (uint32_t)fields[i]);
    }
    if (mask & 512u) qa_net_write_u8(w, flags);
}

static qa_actor_collision read_collision(qa_net_reader *r, qa_actor_collision base)
{
    uint64_t fields[9]; collision_fields(base, fields);
    int32_t opaque_token = base.q1_opaque_token;
    uint16_t mask = qa_net_read_u16(r);
    for (unsigned i = 0; i < 9; ++i) {
        if (!(mask & (1u << i))) continue;
        if (i == 3 || i == 4) fields[i] = qa_net_read_u64(r);
        else if (i == 5) opaque_token = qa_net_read_i32(r);
        else fields[i] = qa_net_read_u32(r);
    }
    uint8_t flags = mask & 512u ? qa_net_read_u8(r) : collision_flags(base);
    if (flags & ~31u) qa_net_reader_fail(r, "Unknown saved collision flags");
    return (qa_actor_collision){.family = (qa_collision_family)fields[0], .shape = (qa_shape_kind)fields[1],
        .model = (uint32_t)fields[2], .contents = {fields[3], fields[4]}, .q1_opaque_token = opaque_token,
        .role = (qa_collision_role)fields[6],
        .q3_entity_number = (int32_t)fields[7], .q3_owner_number = (int32_t)fields[8],
        .inline_model = (flags & 1u) != 0, .monster = (flags & 2u) != 0,
        .dead_monster = (flags & 4u) != 0, .q1_corpse = (flags & 8u) != 0, .has_q3_owner = (flags & 16u) != 0};
}

bool qa_save_world_encode(const qa_world_checkpoint *value, qa_buffer *out, qa_error *error)
{
    if (!value || !out || (value->body_count && !value->bodies) ||
        (value->spatial_count && !value->spatial) || value->body_count > UINT32_MAX ||
        value->spatial_count > value->body_count || value->body_count > (SIZE_MAX - WORLD_HEADER_BYTES) / WORLD_BODY_MAX_BYTES)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid world checkpoint encoding");
    size_t capacity = WORLD_HEADER_BYTES + value->body_count * WORLD_BODY_MAX_BYTES;
    if (value->spatial_count > (SIZE_MAX - capacity) / WORLD_SPATIAL_BYTES)
        return persistence_fail(error, QA_ERROR_MEMORY, "World checkpoint codec size overflow");
    capacity += value->spatial_count * WORLD_SPATIAL_BYTES;
    qa_buffer buffer = {malloc(capacity), 0};
    if (!buffer.data) return persistence_fail(error, QA_ERROR_MEMORY, "Allocating world checkpoint codec");
    qa_net_writer w;
    qa_net_writer_init(&w, buffer.data, capacity, error);
    qa_net_write_data(&w, "QAWD", 4);
    qa_net_write_u32(&w, (uint32_t)value->body_count); qa_net_write_u32(&w, (uint32_t)value->spatial_count);
    qa_net_write_u64(&w, value->attachment_order); qa_net_write_u64(&w, value->body_serial);
    for (size_t i = 0; i < value->body_count; ++i) {
        const qa_world_body_checkpoint *v = value->bodies + i;
        uint32_t flags = (v->external_body ? 1u : 0) | (v->external_collision ? 2u : 0) |
            (v->has_collision ? 4u : 0) | (v->effective_collision ? 8u : 0) | (v->attached ? 16u : 0) |
            (v->has_ground ? 32u : 0) | (v->has_stored_ground ? 64u : 0) | (v->has_linked_ground ? 128u : 0) |
            (v->has_collision_owner ? 256u : 0) | (v->has_stored_collision_owner ? 512u : 0) |
            (v->has_retained_collision_owner ? 1024u : 0) | (v->has_anchor ? 2048u : 0) | (v->link.linked ? 4096u : 0) |
            (v->collision_serial ? WORLD_COLLISION_SERIAL : 0) | (v->attachment_order ? WORLD_ATTACHMENT_ORDER : 0) |
            (v->link.link_count ? WORLD_LINK_COUNT : 0) | (v->attachment.follow ? WORLD_ATTACHMENT_FOLLOW : 0);
        write_ref(&w, v->actor); qa_net_write_u32(&w, flags);
        if (v->has_ground) write_reference(&w, v->ground, v->ground_kind, v->ground_owner);
        if (v->has_stored_ground) write_reference(&w, v->stored_ground, v->stored_ground_kind, v->stored_ground_owner);
        if (v->has_linked_ground) write_reference(&w, v->linked_ground, v->linked_ground_kind, v->linked_ground_owner);
        if (v->has_collision_owner) write_reference(&w, v->collision_owner, v->collision_owner_kind, v->collision_owner_owner);
        if (v->has_stored_collision_owner) write_reference(&w, v->stored_collision_owner, v->stored_collision_owner_kind, v->stored_collision_owner_owner);
        if (v->has_retained_collision_owner) write_reference(&w, v->retained_collision_owner, v->retained_collision_owner_kind, v->retained_collision_owner_owner);
        if (v->has_anchor) write_ref(&w, v->anchor);
        write_body(&w, v->state, (qa_body_state){0});
        write_body(&w, v->stored_state, v->state); write_body(&w, v->link.state, v->state);
        if (v->link.link_count) qa_net_write_u64(&w, v->link.link_count);
        write_bounds(&w, v->link.absolute_bounds);
        write_collision(&w, v->collision, (qa_actor_collision){0});
        write_collision(&w, v->stored_collision, v->collision); write_collision(&w, v->retained_collision, v->collision);
        if (v->attachment.follow) qa_net_write_u32(&w, v->attachment.follow);
        float offset[3] = {v->attachment.offset.x, v->attachment.offset.y, v->attachment.offset.z};
        const float base[3] = {0}; write_floats(&w, offset, base, 3);
        qa_net_write_u64(&w, v->storage_serial);
        if (v->collision_serial) qa_net_write_u64(&w, v->collision_serial);
        if (v->attachment_order) qa_net_write_u64(&w, v->attachment_order);
    }
    for (size_t i = 0; i < value->spatial_count; ++i) {
        write_ref(&w, value->spatial[i].actor); qa_net_write_u32(&w, value->spatial[i].sector);
    }
    if (w.failed) { qa_buffer_free(&buffer); return false; }
    buffer.size = qa_net_writer_size(&w);
    *out = buffer;
    return true;
}

bool qa_save_world_decode(qa_bytes bytes, qa_world_checkpoint *out, qa_error *error)
{
    if (!out || !bytes.data || bytes.size < WORLD_HEADER_BYTES || memcmp(bytes.data, "QAWD", 4))
        return persistence_fail(error, QA_ERROR_FORMAT, "Invalid world checkpoint signature");
    qa_net_reader r;
    qa_net_reader_init(&r, bytes, error); r.bit = 32;
    qa_world_checkpoint value = {0};
    value.body_count = qa_net_read_u32(&r); value.spatial_count = qa_net_read_u32(&r);
    value.attachment_order = qa_net_read_u64(&r); value.body_serial = qa_net_read_u64(&r);
    size_t remaining = qa_net_reader_remaining(&r);
    if (value.spatial_count > value.body_count || value.body_count > remaining / WORLD_BODY_MIN_BYTES)
        return persistence_fail(error, QA_ERROR_FORMAT, "Invalid world checkpoint body extent");
    remaining -= value.body_count * WORLD_BODY_MIN_BYTES;
    if (value.spatial_count > remaining / WORLD_SPATIAL_BYTES ||
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
        v->actor = read_ref(&r); uint32_t flags = qa_net_read_u32(&r);
        if (flags & ~(uint32_t)WORLD_FLAGS) qa_net_reader_fail(&r, "Unknown saved world body flags");
        v->external_body = (flags & 1u) != 0; v->external_collision = (flags & 2u) != 0;
        v->has_collision = (flags & 4u) != 0; v->effective_collision = (flags & 8u) != 0;
        v->attached = (flags & 16u) != 0; v->has_ground = (flags & 32u) != 0;
        v->has_stored_ground = (flags & 64u) != 0; v->has_linked_ground = (flags & 128u) != 0;
        v->has_collision_owner = (flags & 256u) != 0; v->has_stored_collision_owner = (flags & 512u) != 0;
        v->has_retained_collision_owner = (flags & 1024u) != 0; v->has_anchor = (flags & 2048u) != 0;
        v->link.linked = (flags & 4096u) != 0;
        if (v->has_ground) read_reference(&r, &v->ground, &v->ground_kind, &v->ground_owner);
        if (v->has_stored_ground) read_reference(&r, &v->stored_ground, &v->stored_ground_kind, &v->stored_ground_owner);
        if (v->has_linked_ground) read_reference(&r, &v->linked_ground, &v->linked_ground_kind, &v->linked_ground_owner);
        if (v->has_collision_owner) read_reference(&r, &v->collision_owner, &v->collision_owner_kind, &v->collision_owner_owner);
        if (v->has_stored_collision_owner) read_reference(&r, &v->stored_collision_owner, &v->stored_collision_owner_kind, &v->stored_collision_owner_owner);
        if (v->has_retained_collision_owner) read_reference(&r, &v->retained_collision_owner, &v->retained_collision_owner_kind, &v->retained_collision_owner_owner);
        if (v->has_anchor) v->anchor = read_ref(&r);
        v->state = read_body(&r, (qa_body_state){0});
        v->stored_state = read_body(&r, v->state); v->link.state = read_body(&r, v->state);
        if (flags & WORLD_LINK_COUNT) v->link.link_count = qa_net_read_u64(&r);
        v->link.absolute_bounds = read_bounds(&r);
        v->collision = read_collision(&r, (qa_actor_collision){0});
        v->stored_collision = read_collision(&r, v->collision); v->retained_collision = read_collision(&r, v->collision);
        if (flags & WORLD_ATTACHMENT_FOLLOW) v->attachment.follow = (qa_body_follow)qa_net_read_u32(&r);
        float offset[3]; const float base[3] = {0}; read_floats(&r, offset, base, 3);
        v->attachment.offset = (qa_vec3){offset[0], offset[1], offset[2]};
        v->storage_serial = qa_net_read_u64(&r);
        if (flags & WORLD_COLLISION_SERIAL) v->collision_serial = qa_net_read_u64(&r);
        if (flags & WORLD_ATTACHMENT_ORDER) v->attachment_order = qa_net_read_u64(&r);
    }
    for (size_t i = 0; i < value.spatial_count && !r.failed; ++i) {
        value.spatial[i].actor = read_ref(&r); value.spatial[i].sector = qa_net_read_u32(&r);
    }
    if (!qa_net_reader_finish(&r)) { qa_world_checkpoint_free(&value); return false; }
    *out = value;
    return true;
}
