/* Port of quake-typescript/src/network/q3/visibility.ts. */
#include "qa/network_q3.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

bool qa_q3_leaf_visibility(qa_world *world,qa_actor_id actor,const qa_bounds *bounds,
    qa_trace_scratch *scratch,qa_q3_visibility_entity *out,int32_t clusters[16],
    bool *has_leaves,qa_error *error)
{
    qa_collision_geometry *geometry=qa_world_geometry(world);
    if(!geometry || !out || !clusters) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q3 link visibility requires its actual leaf membership"); return false;
    }
    const qa_world_leaf_visibility_result *r;
    if(!qa_world_leaf_visibility(world,actor,bounds,QA_WORLD_LEAVES_BOX,scratch,&r,error)) return false;
    bool native=qa_collision_geometry_family(geometry)==QA_GAME_Q3;
    if(native?r->prefix_invalid:r->all_invalid) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q3 link visibility exceeds source indices"); return false;
    }
    const qa_leaf_area_pair *areas=native?&r->native_areas:&r->foreign_areas;
    size_t count=native?r->ordered_count:r->sorted_count;
    if(native && count>=16 && r->last_invalid) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q3 last linked cluster exceeds source range"); return false;
    }
    memcpy(clusters,native?r->ordered:r->sorted,count*sizeof(*clusters));
    out->area=areas->area;out->area2=areas->area2;out->last_cluster=count>=16?(native?r->last_cluster:r->maximum_cluster):0;
    out->cluster_count=count;out->clusters=clusters;*has_leaves=r->count!=0;return true;
}

typedef struct visibility_search {
    const qa_q3_player *player;
    const qa_q3_visibility_entity *entities;
    size_t entity_count;
    const qa_q3_visibility_world *world;
    qa_q3_visible_entities *result;
    bool visited[QA_Q3_ENTITIES];
    uint8_t area_bits[32];
    qa_error *error;
} visibility_search;
static void add(visibility_search *s, size_t number) {
    s->visited[number] = true;
    if (s->result->count == 256) { s->result->capacity_reached = true; return; }
    qa_q3_entity *out = &s->result->entities[s->result->count++];
    *out = *s->entities[number].state; out->number = (int32_t)number;
}
static bool visible_from(visibility_search *s, const float origin[3]) {
    int32_t area, cluster;
    size_t bytes;
    const qa_q3_visibility_world *world = s->world;
    if (!world->point(world->context, origin, &area, &cluster, s->error)
        || !world->area_bits(world->context, area, s->area_bits, &bytes, s->error)) return false;
    if (bytes > 32) { qa_error_set(s->error, QA_ERROR_FORMAT, bytes, "Q3 visibility area mask exceeds 32 bytes"); return false; }
    s->result->area_bytes = (uint8_t)bytes;
    for (size_t number = 0; number < s->entity_count; ++number) {
        const qa_q3_visibility_entity *entity = &s->entities[number];
        uint32_t flags = entity->flags;
        if (!entity->linked || !entity->state || (flags & QA_Q3_SVF_NOCLIENT)) continue;
        if ((flags & QA_Q3_SVF_SINGLECLIENT) && entity->single_client != s->player->clientNum) continue;
        if ((flags & QA_Q3_SVF_NOTSINGLECLIENT) && entity->single_client == s->player->clientNum) continue;
        if (flags & QA_Q3_SVF_CLIENTMASK) {
            if (s->player->clientNum >= 32) { qa_error_set(s->error, QA_ERROR_FORMAT, number, "Q3 client mask cannot address player 32 or higher"); return false; }
            if (!((uint32_t)entity->single_client & (UINT32_C(1) << s->player->clientNum))) continue;
        }
        if (s->visited[number]) continue;
        if (number == QA_Q3_ENTITY_NONE) { qa_error_set(s->error, QA_ERROR_FORMAT, number, "Q3 entity sentinel cannot be transmitted"); return false; }
        if (flags & QA_Q3_SVF_BROADCAST) { add(s, number); continue; }
        if (!entity->cluster_count || !entity->clusters
            || (!world->areas_connected(world->context, area, entity->area)
                && !world->areas_connected(world->context, area, entity->area2))) continue;
        size_t i; int32_t last = 0;
        for (i = 0; i < entity->cluster_count; ++i) {
            last = entity->clusters[i];
            if (world->cluster_visible(world->context, cluster, last)) break;
        }
        if (i == entity->cluster_count) {
            if (!entity->last_cluster) continue;
            int64_t candidate = last;
            for (; candidate <= entity->last_cluster; ++candidate)
                if (world->cluster_visible(world->context, cluster, (int32_t)candidate)) break;
            /* Retain the donor's equality test for overflow-cluster visibility. */
            if (candidate == entity->last_cluster) continue;
        }
        add(s, number);
        if (flags & QA_Q3_SVF_PORTAL) {
            const qa_q3_entity *state = entity->state;
            if (state->generic1) {
                float x = state->origin[0] - origin[0], y = state->origin[1] - origin[1], z = state->origin[2] - origin[2];
                float distance = (float)state->generic1 * (float)state->generic1;
                if (x * x + y * y + z * z > distance) continue;
            }
            if (!visible_from(s, state->origin2)) return false;
        }
    }
    return true;
}
static int entity_order(const void *a, const void *b) {
    const qa_q3_entity *left = a, *right = b;
    return left->number < right->number ? -1 : left->number > right->number ? 1 : 0;
}
bool qa_q3_select_snapshot_entities(const qa_q3_player *player, const qa_q3_visibility_entity *entities, size_t count,
                                     const qa_q3_visibility_world *world, bool dead, qa_q3_visible_entities *out, qa_error *error) {
    if (!player || !out || !world || !world->point || !world->area_bits || !world->areas_connected || !world->cluster_visible
        || (count && !entities) || count > QA_Q3_ENTITIES || player->clientNum < 0 || player->clientNum >= QA_Q3_ENTITIES) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 visibility source"); return false;
    }
    memset(out, 0, sizeof(*out));
    visibility_search search = {.player = player, .entities = entities, .entity_count = count, .world = world, .result = out, .error = error};
    search.visited[player->clientNum] = true;
    float origin[3] = {player->origin[0], player->origin[1], player->origin[2] + (float)player->viewheight};
    if (!dead && !visible_from(&search, origin)) return false;
    if (out->count > 1) qsort(out->entities, out->count, sizeof(out->entities[0]), entity_order);
    for (size_t i = 0; i < 32; ++i) out->area_mask[i] = (uint8_t)(search.area_bits[i] ^ 255);
    return true;
}
