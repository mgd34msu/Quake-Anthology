#include "visual_visibility.h"
#include "internal.h"
#include "native_q2_visibility.h"
#include "qa/bsp.h"
#include <math.h>

static bool cluster_visible(qa_collision_geometry *geometry, const application_q2_visibility_recipient *recipient,
    int32_t target, bool phs, bool *out, qa_error *error)
{
    *out = false;
    for (size_t i = 0; i < recipient->cluster_count; ++i) {
        bool visible;
        if (!qa_collision_cluster_visible(geometry, recipient->clusters[i], target, phs, &visible, error)) return false;
        if (visible) { *out = true; break; }
    }
    return true;
}

static bool headnode_visible(qa_collision_geometry *geometry, const application_q2_visibility_recipient *recipient,
    int32_t headnode, bool phs, bool *out, qa_error *error)
{
    const qa_bsp_view *map = qa_collision_bsp(geometry);
    size_t capacity = recipient->pending_capacity, count = 1, visits = 0;
    int32_t *pending = recipient->pending;
    if (!pending || !capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 visibility requires its view's headnode scratch");
    pending[0] = headnode; *out = false;
    bool ok = true;
    while (ok && count && !*out) {
        int32_t child = pending[--count];
        if (++visits > capacity) { ok = application_fail(error, QA_ERROR_FORMAT, "Q2 visibility headnode is cyclic"); break; }
        if (child < 0) {
            uint32_t index = (uint32_t)(-(int64_t)child - 1);
            qa_collision_leaf leaf;
            ok = qa_collision_leaf_at(geometry, index, &leaf, error) &&
                cluster_visible(geometry, recipient, (int32_t)leaf.cluster, phs, out, error);
        } else {
            qa_bsp_node node;
            ok = count <= capacity - 2 && qa_bsp_read_node(map, (uint32_t)child, &node, error);
            if (ok) { pending[count++] = node.children[1]; pending[count++] = node.children[0]; }
        }
    }
    return ok;
}

size_t application_q2_visibility_pending_capacity(const qa_application *app)
{
    size_t nodes = qa_bsp_record_count(qa_collision_bsp(app->geometry), QA_BSP_NODES);
    return nodes > (SIZE_MAX / sizeof(int32_t) - 1) / 2 ? 0 : nodes * 2 + 1;
}

bool application_q2_visibility_recipient_prepare(qa_application *app, qa_actor_id actor,
    uint32_t slot, qa_vec3 origin, application_q2_visibility_recipient *out, qa_error *error)
{
    application_q2_visibility_recipient value = {.actor = actor, .slot = slot, .origin = origin};
    if (!qa_collision_point_leaf(app->geometry, origin, &value.leaf, error)) return false;
    uint32_t fat_leaves[64];
    qa_leaf_list fat;
    qa_bounds bounds = {qa_vec_sub(origin, qa_v3(8, 8, 8)), qa_vec_add(origin, qa_v3(8, 8, 8))};
    if (!qa_collision_box_leaves(app->geometry, bounds, fat_leaves, 64, &fat, error)) return false;
    if (!fat.count) return application_fail(error, QA_ERROR_FORMAT, "Q2 Source fat PVS has no geometry leaf");
    for (size_t j = 0; j < fat.count; ++j) {
        qa_collision_leaf leaf;
        if (!qa_collision_leaf_at(app->geometry, fat_leaves[j], &leaf, error)) return false;
        value.clusters[value.cluster_count++] = (int32_t)leaf.cluster;
    }
    *out = value;
    return true;
}

bool application_q2_visibility_test(qa_application *app, struct application_native_q2 *engine,
    bool rr, bool builtin, const application_q2_visibility_recipient *recipient,
    const application_q2_visibility_entity *entity, const qa_q2_entity *state,
    bool novis, bool *out, qa_error *error)
{
    qa_collision_geometry *geometry = app->geometry;
    const qa_native_host_q2_entity *original = entity->original;
    bool sdk = original != NULL;
    uint32_t flags = entity->flags;
    qa_bounds bounds = entity->bounds;
    *out = false;
    if (flags & 1) return true;
    if (sdk && rr && (flags & 256)) {
        bool admitted;
        if (!application_native_q2_visibility_read(engine, original->binding.source_slot,
                original->binding.actor, recipient->slot, recipient->actor, &admitted, error)) return false;
        if (!admitted) return true;
    }
    if (state->number == recipient->slot || (rr && ((flags & 1024) || novis))) { *out = true; return true; }
    bool beam = (state->renderfx & 128) != 0, shadow = rr && (state->renderfx & 16384) != 0;
    bool phs = beam || (rr && (shadow || state->sound));
    bool area = false, visible = false;
    qa_world_leaf_membership membership = {0};
    if (sdk && !rr) {
        if (!qa_collision_areas_connected(geometry, (int32_t)recipient->leaf.area, original->areas[0], &area, error)) return false;
        if (!area && original->areas[1] &&
            !qa_collision_areas_connected(geometry, (int32_t)recipient->leaf.area, original->areas[1], &area, error)) return false;
        if (!area) return true;
        if (beam && original->cluster_count > 0) {
            if (!qa_collision_cluster_visible(geometry, (int32_t)recipient->leaf.cluster, original->clusters[0], true, &visible, error)) return false;
        } else if (original->cluster_count == -1) {
            if (!headnode_visible(geometry, recipient, original->headnode, false, &visible, error)) return false;
        } else for (int32_t i = 0; !visible && i < original->cluster_count; ++i)
            if (!cluster_visible(geometry, recipient, original->clusters[i], false, &visible, error)) return false;
    } else {
        if (!qa_world_link_membership(app->world, entity->actor, &bounds,
                QA_WORLD_LEAVES_BOX, &membership, error)) return false;
        for (size_t i = 0; i < membership.count; ++i) {
            const qa_collision_leaf *leaf = &membership.leaves[i];
            bool connected, seen;
            if (!qa_collision_areas_connected(geometry, (int32_t)recipient->leaf.area,
                (int32_t)leaf->area, &connected, error)) return false;
            area |= connected;
            if (builtin && beam && !rr) {
                seen = false;
                if (!i && !qa_collision_cluster_visible(geometry,
                    (int32_t)recipient->leaf.cluster, (int32_t)leaf->cluster,
                    true, &seen, error)) return false;
            } else if (!cluster_visible(geometry, recipient, (int32_t)leaf->cluster, phs, &seen, error)) return false;
            visible |= seen;
        }
        if (!area) return true;
    }
    if (!visible) return true;
    qa_vec3 origin = qa_v3(state->origin[0], state->origin[1], state->origin[2]);
    float distance = qa_vec_length(qa_vec_sub(recipient->origin, origin));
    if (rr && state->sound) {
        float attenuation = state->loop_attenuation == -1 ? 0 :
            state->loop_attenuation > 0 && state->loop_attenuation != 3 ? state->loop_attenuation * .0006f : .003f;
        if ((distance - 80) * attenuation <= 1) { *out = true; return true; }
        if (!state->modelindex) return true;
        if (!beam) {
            visible = false;
            for (size_t i = 0; !visible && i < membership.count; ++i)
                if (!cluster_visible(geometry, recipient, (int32_t)membership.leaves[i].cluster,
                    false, &visible, error)) return false;
        }
        *out = beam || visible; return true;
    }
    *out = state->modelindex || shadow || distance <= 400;
    return true;
}
