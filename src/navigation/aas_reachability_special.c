#include "aas_reachability_internal.h"
#include "qa/text.h"

enum { LAND_EVENTS = 1 | 4 | 8 | 16 | 32 | 128 | 256, HAZARD_EVENTS = 8 | 16 | 32 };

static bool text_is(qa_bytes value, const char *text) {
    return qa_text_bytes_equal(value, (qa_bytes){(const uint8_t *)text, strlen(text)}, false);
}
static bool entity_is(const aas_reach_build *work, size_t entity, const char *key, const char *value) {
    qa_bytes text;
    return qa_entity_value(&work->entities, entity, key, &text) && text_is(text, value);
}
static bool integer(const aas_reach_build *work, size_t entity, const char *key,
    int32_t *value, qa_error *error) {
    qa_bytes text = {0};
    (void)qa_entity_value(&work->entities, entity, key, &text);
    return qa_bsp_value_integer(text, value, error);
}
static bool number(const aas_reach_build *work, size_t entity, const char *key,
    float *value, qa_error *error) {
    qa_bytes text = {0};
    (void)qa_entity_value(&work->entities, entity, key, &text);
    return qa_bsp_value_float(text, value, error);
}
static bool vector(const aas_reach_build *work, size_t entity, const char *key,
    qa_vec3 *value, bool *found, qa_error *error) {
    qa_bytes text = {0};
    *found = qa_entity_value(&work->entities, entity, key, &text);
    return qa_bsp_value_vector(text, value, error);
}
static size_t target_find(const aas_reach_build *work, qa_bytes target, const char *classname) {
    for (size_t i = 0; i < work->entities.count; ++i) {
        qa_bytes name;
        if ((!classname || entity_is(work, i, "classname", classname)) &&
            qa_entity_value(&work->entities, i, "targetname", &name) &&
            qa_text_bytes_equal(name, target, false)) return i;
    }
    return SIZE_MAX;
}
static bool team_flags(const aas_reach_build *work, size_t entity, uint32_t *flags, qa_error *error) {
    int32_t not_team;
    if (!integer(work, entity, "bot_notteam", &not_team, error)) return false;
    *flags = not_team == 1 ? UINT32_C(0x01000000) :
        not_team == 2 ? UINT32_C(0x02000000) : 0;
    return true;
}
static bool model_bounds(const aas_reach_build *work, size_t entity, bool default_world,
    qa_bounds *bounds, int32_t *model_number, bool *found, qa_error *error) {
    *found = false;
    qa_bytes name = {0};
    bool has_model = qa_entity_value(&work->entities, entity, "model", &name) && name.size;
    if (!has_model && !default_world) return true;
    int32_t index = 0;
    if (has_model) {
        qa_bytes suffix = name;
        ++suffix.data; --suffix.size;
        if (!qa_bsp_value_integer(suffix, &index, error)) return false;
    }
    if (index < 0 || (size_t)index >= qa_bsp_record_count(work->geometry, QA_BSP_MODELS)) return true;
    qa_bsp_model model;
    if (!qa_bsp_read_model(work->geometry, (size_t)index, &model, error)) return false;
    /* Inline BSP bounds are already in map space. The original bot import
     * and TypeScript adapter return zero model-origin translation. */
    *bounds = (qa_bounds){model.bounds.min, model.bounds.max};
    if (model_number) *model_number = index;
    *found = true;
    return true;
}
static bool link_bounds(aas_reach_build *work, qa_bounds bounds, qa_error *error) {
    const qa_aas_view *source = work->source;
    qa_bounds player = work->navigation->graph->view.profile.has_crouched_shape ?
        work->navigation->graph->view.profile.crouched_shape.bounds :
        work->navigation->graph->view.profile.shape.bounds;
    for (size_t i = 0; i < source->count[QA_AAS_BOXES]; ++i)
        if (source->boxes[i].presence == 4) { player = source->boxes[i].bounds; break; }
    qa_bounds expanded = {qa_vec_sub(bounds.mins, player.maxs), qa_vec_sub(bounds.maxs, player.mins)};
    return qa_aas_bbox_areas(source, work->query, expanded, work->entity_areas,
        source->count[QA_AAS_AREAS], &work->entity_area_count, error);
}
static qa_nav_prediction_query prediction(qa_vec3 origin, qa_vec3 velocity) {
    return (qa_nav_prediction_query){.origin = origin, .velocity = velocity,
        .presence = 2, .maximum_frames = 30, .frame_ms = 100,
        .stop_events = LAND_EVENTS, .stop_area = QA_NAV_NO_INDEX};
}
static bool teleport(aas_reach_build *work, qa_error *error) {
    for (size_t entity = 0; entity < work->entities.count; ++entity) {
        bool relay = entity_is(work, entity, "classname", "trigger_multiple");
        if (!relay && !entity_is(work, entity, "classname", "trigger_teleport")) continue;
        qa_bounds bounds; bool found;
        if (!model_bounds(work, entity, false, &bounds, NULL, &found, error)) return false;
        if (!found) continue;
        qa_bytes target;
        if (!qa_entity_value(&work->entities, entity, "target", &target)) continue;
        if (relay) {
            size_t intermediary = target_find(work, target, "target_teleporter");
            if (intermediary == SIZE_MAX ||
                !qa_entity_value(&work->entities, intermediary, "target", &target)) continue;
        }
        size_t destination = target_find(work, target, NULL);
        if (destination == SIZE_MAX) continue;
        qa_vec3 origin;
        if (!vector(work, destination, "origin", &origin, &found, error)) return false;
        if (!found) continue;
        uint32_t area;
        if (!qa_aas_point_area(work->source, origin, &area, error)) return false;
        if (!(work->settings[area].contents & (AAS_REACH_TELEPORT | AAS_REACH_JUMPPAD))) {
            qa_vec3 end = origin; end.z -= 64;
            qa_aas_presence_trace trace;
            if (!qa_aas_trace_presence(work->source, work->query, origin, end, 4, &trace, error)) return false;
            if (trace.start_solid) continue;
            qa_bytes text = {0}; float angle;
            (void)qa_entity_value(&work->entities, destination, "angle", &text);
            if (!qa_bsp_value_float(text, &angle, error)) return false;
            qa_vec3 velocity = {0};
            if (angle != 0) {
                qa_angle_vectors(qa_v3(0, angle, 0), &velocity, NULL, NULL, QA_ANGLE_NATIVE_MATH);
                velocity = qa_vec_scale(velocity, 400);
            }
            qa_nav_prediction_query query = prediction(origin, velocity);
            if (!aas_reach_predict(work, &query, error)) return false;
            origin = work->prediction.end;
            if (!qa_aas_point_area(work->source, origin, &area, error)) return false;
        }
        uint32_t flags;
        if (!team_flags(work, entity, &flags, error) || !link_bounds(work, bounds, error)) return false;
        qa_aas_reach reach = {.area = (int32_t)area,
            .start = qa_vec_scale(qa_vec_add(bounds.mins, bounds.maxs), .5f), .end = origin,
            .travel_type = (int32_t)((uint32_t)AAS_REACH_TELEPORT_TYPE | flags)};
        if (!aas_reach_time(work->movement.teleport, &reach.travel_time, error)) return false;
        for (size_t i = 0; i < work->entity_area_count; ++i) {
            uint32_t from = work->entity_areas[i];
            if ((work->settings[from].contents & AAS_REACH_TELEPORT) &&
                !aas_reach_add(work, from, reach, error)) return false;
        }
    }
    return true;
}
static bool jump_pad_info(aas_reach_build *work, size_t entity, qa_bounds *bounds,
    qa_vec3 *start, qa_vec3 *velocity, bool *found, qa_error *error) {
    if (!model_bounds(work, entity, true, bounds, NULL, found, error)) return false;
    if (!*found) return true;
    *found = false;
    qa_vec3 center = qa_vec_scale(qa_vec_add(bounds->mins, bounds->maxs), .5f);
    qa_vec3 above = center; above.z += 64;
    qa_aas_presence_trace trace;
    if (!qa_aas_trace_presence(work->source, work->query, above, center, 4, &trace, error)) return false;
    *start = trace.start_solid ? center : trace.end; start->z += .125f;
    qa_bytes target;
    if (!qa_entity_value(&work->entities, entity, "target", &target)) return true;
    size_t destination = target_find(work, target, NULL);
    if (destination == SIZE_MAX) return true;
    qa_vec3 origin; bool has_origin;
    if (!vector(work, destination, "origin", &origin, &has_origin, error)) return false;
    (void)has_origin;
    float time = (float)sqrt((origin.z - center.z) / (.5 * work->movement.gravity));
    if (!(time > 0) || !isfinite(time)) return true;
    qa_vec3 delta = qa_vec_sub(origin, center);
    float distance = qa_vec_normalize_length(&delta), speed = (distance / time) * 1.1f;
    *velocity = qa_vec_scale(delta, speed); velocity->z = time * work->movement.gravity;
    *found = true;
    return true;
}
static bool jump_pad_link(aas_reach_build *work, uint32_t from, uint32_t to,
    qa_vec3 start, qa_vec3 end, qa_vec3 velocity, qa_vec3 command,
    uint32_t flags, bool controlled, qa_error *error) {
    qa_aas_reach reach = {.area = (int32_t)to, .start = start, .end = end,
        .travel_type = (int32_t)((uint32_t)AAS_REACH_JUMPPAD_TYPE | flags)};
    command.z = 0;
    if (!aas_reach_integer(velocity.z, &reach.face, error) ||
        !aas_reach_integer(qa_vec_length(command), &reach.edge, error) ||
        !aas_reach_time(controlled ? work->movement.air_jump_pad : work->movement.jump_pad,
            &reach.travel_time, error)) return false;
    return aas_reach_add(work, from, reach, error);
}
static bool in_source_pad(const aas_reach_build *work, uint32_t area) {
    for (size_t i = 0; i < work->entity_area_count; ++i)
        if (work->entity_areas[i] == area && (work->settings[area].contents & AAS_REACH_JUMPPAD)) return true;
    return false;
}
static bool jump_pad(aas_reach_build *work, qa_error *error) {
    for (size_t entity = 0; entity < work->entities.count; ++entity) {
        if (!entity_is(work, entity, "classname", "trigger_push")) continue;
        qa_vec3 start, velocity; qa_bounds bounds; bool found;
        if (!jump_pad_info(work, entity, &bounds, &start, &velocity, &found, error)) return false;
        if (!found) continue;
        if (!link_bounds(work, bounds, error)) return false;
        bool pad = false;
        for (size_t i = 0; i < work->entity_area_count; ++i)
            if (work->settings[work->entity_areas[i]].contents & AAS_REACH_JUMPPAD) { pad = true; break; }
        if (!pad) continue;
        uint32_t flags;
        if (!team_flags(work, entity, &flags, error)) return false;
        if (velocity.x != 0 || velocity.y != 0) {
            uint32_t area = 0; unsigned frame;
            for (frame = 0; frame < 20; ++frame) {
                qa_nav_prediction_query query = prediction(start, velocity);
                if (!aas_reach_predict(work, &query, error)) return false;
                if (!qa_aas_point_area(work->source, work->prediction.end, &area, error)) return false;
                if (!in_source_pad(work, area)) break;
                start = work->prediction.end; velocity = work->prediction.velocity;
            }
            if (area != 0 && frame < 20) {
                for (size_t i = 0; i < work->entity_area_count; ++i) {
                    uint32_t from = work->entity_areas[i];
                    if ((work->settings[from].contents & AAS_REACH_JUMPPAD) && !aas_reach_exists(work, from, area) &&
                        !jump_pad_link(work, from, area, start, work->prediction.end, velocity, velocity,
                            flags, false, error)) return false;
                }
            }
        }
        if (fabsf(velocity.x) > 100 || fabsf(velocity.y) > 100) continue;
        for (uint32_t area = 1; area < work->source->count[QA_AAS_AREAS]; ++area) {
            bool excluded = false;
            for (size_t i = 0; i < work->entity_area_count; ++i) {
                uint32_t from = work->entity_areas[i];
                if (aas_reach_exists(work, from, area) ||
                    (from == area && (work->settings[from].contents & AAS_REACH_JUMPPAD))) { excluded = true; break; }
            }
            if (excluded) continue;
            const qa_aas_area *destination = work->source->areas + area;
            for (int32_t i = 0; i < destination->face_count; ++i) {
                int32_t face_number = work->source->face_index[destination->first_face + i];
                const qa_aas_face *face = work->source->faces + nav_aas_oriented_index(face_number);
                if (!(face->flags & AAS_REACH_FACE_GROUND)) continue;
                qa_vec3 center = aas_reach_face_center(work->source, face);
                float speed;
                if (center.z < start.z || !aas_reach_horizontal_jump_speed(work, velocity.z, start, center, &speed) ||
                    speed >= 150) continue;
                qa_vec3 direction = qa_vec_sub(center, start); direction.z = 0;
                qa_vec3 command = qa_vec_scale(qa_vec_normalize(direction), speed);
                qa_nav_prediction_query query = prediction(start, velocity);
                query.command_move = command; query.command_frames = 30;
                query.stop_events = ((uint32_t)LAND_EVENTS & ~UINT32_C(1)) | UINT32_C(1024);
                query.stop_area = area;
                if (!aas_reach_predict(work, &query, error)) return false;
                const qa_nav_prediction_result *move = &work->prediction;
                if (aas_reach_prediction_frames(work) >= 30 || (move->stop_event & HAZARD_EVENTS) ||
                    !(move->stop_event & (1024 | 128 | 256))) continue;
                uint32_t arrival;
                if (!qa_aas_point_area(work->source, move->end, &arrival, error)) return false;
                bool returned = false;
                for (size_t j = 0; j < work->entity_area_count; ++j)
                    if (work->entity_areas[j] == arrival) { returned = true; break; }
                if (returned) continue;
                for (size_t j = 0; j < work->entity_area_count; ++j) {
                    uint32_t from = work->entity_areas[j];
                    if ((work->settings[from].contents & AAS_REACH_JUMPPAD) && !aas_reach_exists(work, from, area) &&
                        !jump_pad_link(work, from, arrival, start, center, velocity, command, flags, true, error)) return false;
                }
            }
        }
    }
    return true;
}
static void perimeter(qa_bounds bounds, qa_vec3 middle, qa_vec3 points[8]) {
    points[0] = qa_v3(bounds.mins.x, middle.y, 0);
    points[1] = qa_v3(middle.x, bounds.maxs.y, 0);
    points[2] = qa_v3(bounds.maxs.x, middle.y, 0);
    points[3] = qa_v3(middle.x, bounds.mins.y, 0);
    points[4] = qa_v3(bounds.mins.x, bounds.maxs.y, 0);
    points[5] = qa_v3(bounds.maxs.x, bounds.maxs.y, 0);
    points[6] = qa_v3(bounds.maxs.x, bounds.mins.y, 0);
    points[7] = qa_v3(bounds.mins.x, bounds.mins.y, 0);
}
static bool elevator(aas_reach_build *work, qa_error *error) {
    for (size_t entity = 0; entity < work->entities.count; ++entity) {
        if (!entity_is(work, entity, "classname", "func_plat")) continue;
        qa_bounds bounds; int32_t model; bool found;
        if (!model_bounds(work, entity, false, &bounds, &model, &found, error)) return false;
        if (!found || model <= 0) continue;
        qa_vec3 origin; float lip, height, speed; uint32_t flags;
        if (!vector(work, entity, "origin", &origin, &found, error) ||
            !number(work, entity, "lip", &lip, error) ||
            !number(work, entity, "height", &height, error) ||
            !number(work, entity, "speed", &speed, error) ||
            !team_flags(work, entity, &flags, error)) return false;
        if (lip == 0) lip = 8;
        if (height == 0) height = (bounds.maxs.z - bounds.mins.z) - lip;
        if (speed == 0) speed = 200;
        qa_vec3 bottom_position = origin; bottom_position.z -= height;
        qa_vec3 bottom = qa_vec_ma(bottom_position, .5f, qa_vec_add(bounds.mins, bounds.maxs));
        bottom.z = bounds.maxs.z - (origin.z - bottom_position.z) + 2;
        qa_vec3 top = qa_vec_ma(bottom_position, .5f, qa_vec_add(bounds.mins, bounds.maxs));
        top.z = bounds.maxs.z + 2;
        bounds.mins = qa_vec_sub(bounds.mins, qa_v3(1, 1, 1));
        bounds.maxs = qa_vec_add(bounds.maxs, qa_v3(1, 1, 1));
        qa_vec3 middle = qa_vec_scale(qa_vec_add(bounds.mins, bounds.maxs), .5f), sides[8];
        perimeter(bounds, middle, sides);
        for (unsigned i = 0; i < 9; ++i) {
            qa_vec3 start_origin; uint32_t from;
            if (i < 8) {
                start_origin = qa_v3(origin.x + sides[i].x, origin.y + sides[i].y, bottom.z + 16);
                if (!qa_aas_point_area(work->source, start_origin, &from, error)) return false;
                unsigned probe;
                for (probe = 0; probe < 16; ++probe) {
                    if (from && (aas_reach_grounded(work, from) || aas_reach_swimming(work, from))) break;
                    start_origin.z += 4;
                    if (!qa_aas_point_area(work->source, start_origin, &from, error)) return false;
                }
                if (probe == 16) continue;
            } else {
                start_origin = top; start_origin.z += 24;
                if (!qa_aas_point_area(work->source, start_origin, &from, error)) return false;
                if (!from) continue;
                start_origin = bottom; start_origin.z += 24;
            }
            bool linked = false;
            for (unsigned distance = 0; distance < 3 && !linked; ++distance) {
                /* The source expands cumulatively across bottom samples. */
                bounds.mins = qa_vec_sub(bounds.mins, qa_v3(4, 4, 4));
                bounds.maxs = qa_vec_add(bounds.maxs, qa_v3(4, 4, 4));
                qa_vec3 top_sides[8]; perimeter(bounds, middle, top_sides);
                for (unsigned j = 0; j < 8; ++j) {
                    qa_vec3 end_origin = qa_v3(origin.x + top_sides[j].x,
                        origin.y + top_sides[j].y, top.z + 16);
                    uint32_t to;
                    if (!qa_aas_point_area(work->source, end_origin, &to, error)) return false;
                    unsigned probe;
                    for (probe = 0; probe < 16; ++probe) {
                        if (to && (aas_reach_grounded(work, to) || aas_reach_swimming(work, to))) {
                            qa_vec3 start = top, end = end_origin;
                            start.z += 32; end.z += 1;
                            qa_aas_presence_trace trace;
                            if (!qa_aas_trace_presence(work->source, work->query,
                                start, end, 4, &trace, error)) return false;
                            if (trace.fraction >= 1) break;
                        }
                        end_origin.z += 4;
                        if (!qa_aas_point_area(work->source, end_origin, &to, error)) return false;
                    }
                    if (probe == 16 || to == from || !aas_reach_grounded(work, to) ||
                        aas_reach_exists(work, from, to)) continue;
                    qa_vec3 direction = qa_vec_normalize(qa_vec_sub(start_origin, bottom));
                    qa_vec3 start = qa_v3(start_origin.x + 24 * direction.x,
                        start_origin.y + 24 * direction.y, start_origin.z);
                    if (qa_bounds_contains(qa_bounds_translate(bounds, origin), start)) continue;
                    qa_aas_reach reach = {.area = (int32_t)to, .face = model, .start = start,
                        .end = end_origin, .travel_type = (int32_t)((uint32_t)AAS_REACH_ELEVATOR | flags)};
                    if (!aas_reach_integer(height, &reach.edge, error) ||
                        !aas_reach_time(work->movement.start_elevator + height * 100 / speed,
                            &reach.travel_time, error) || !aas_reach_add(work, from, reach, error)) return false;
                    linked = true;
                }
            }
        }
    }
    return true;
}
typedef struct face_reach {
    uint32_t area;
    qa_vec3 start, end;
} face_reach;
static bool inside_face(const qa_aas_view *world, int32_t number, qa_vec3 point, float epsilon) {
    const qa_aas_face *face = world->faces + nav_aas_oriented_index(number);
    qa_vec3 normal = world->planes[face->plane].normal;
    for (int32_t i = 0; i < face->edge_count; ++i) {
        int32_t edge_number = world->edge_index[face->first_edge + i];
        const qa_aas_edge *edge = world->edges + nav_aas_oriented_index(edge_number);
        qa_vec3 first = world->vertices[edge->vertices[edge_number < 0 ? 1 : 0]],
            second = world->vertices[edge->vertices[edge_number < 0 ? 0 : 1]];
        if (qa_vec_dot(qa_vec_sub(point, first), qa_vec_cross(qa_vec_sub(second, first), normal)) < -epsilon)
            return false;
    }
    return true;
}
static size_t face_reachabilities(const aas_reach_build *work, const qa_vec3 points[4],
    const qa_aas_plane *plane, bool towards_face, face_reach *out) {
    const qa_aas_view *world = work->source;
    size_t count = 0;
    for (uint32_t area_number = 1; area_number < world->count[QA_AAS_AREAS]; ++area_number) {
        const qa_aas_area *area = world->areas + area_number;
        aas_reach_closest_edges closest = {.distance = 999999};
        const qa_aas_plane *best_plane = NULL; int32_t best_face = 0;
        for (int32_t j = 0; j < area->face_count; ++j) {
            int32_t number = world->face_index[area->first_face + j];
            const qa_aas_face *face = world->faces + nav_aas_oriented_index(number);
            if (!(face->flags & AAS_REACH_FACE_GROUND)) continue;
            const qa_aas_plane *ground = world->planes + face->plane;
            for (int32_t k = 0; k < face->edge_count; ++k) {
                const qa_aas_edge *edge = world->edges +
                    nav_aas_oriented_index(world->edge_index[face->first_edge + k]);
                for (unsigned l = 0; l < 4; ++l) {
                    float previous = closest.distance;
                    aas_reach_closest_edge_points(world->vertices[edge->vertices[0]],
                        world->vertices[edge->vertices[1]], points[l], points[(l + 1) % 4],
                        ground, plane, &closest);
                    if (closest.distance < previous) { best_face = number; best_plane = ground; }
                }
            }
        }
        if (!closest.found || closest.distance > 192) continue;
        qa_vec3 start = qa_vec_scale(qa_vec_add(closest.start[0], closest.start[1]), .5f),
            end = qa_vec_scale(qa_vec_add(closest.end[0], closest.end[1]), .5f);
        if (!towards_face) { qa_vec3 swap = start; start = end; end = swap; }
        qa_vec3 horizontal = qa_vec_sub(end, start); horizontal.z = 0;
        float distance = qa_vec_length(horizontal), speed;
        if (distance > 2 * aas_reach_max_jump_distance(work) || end.z - 32 > start.z ||
            end.z < start.z - 128 || (distance > 32 &&
            !aas_reach_horizontal_jump_speed(work, 0, start, end, &speed))) continue;
        ++start.z; ++end.z;
        qa_vec3 test = towards_face ? end : start; test.z = 0;
        test.z = (best_plane->distance - qa_vec_dot(best_plane->normal, test)) / best_plane->normal.z;
        if (!inside_face(world, best_face, test, .1f) && end.z - 16 > start.z) continue;
        out[count++] = (face_reach){.area = area_number, .start = start, .end = end};
    }
    return count;
}
static void bobbing_face(qa_bounds bounds, qa_vec3 middle, qa_vec3 point, qa_vec3 face[4]) {
    float top = (point.z + (bounds.maxs.z - middle.z)) + 24;
    face[0] = qa_v3(point.x + (bounds.maxs.x - middle.x), point.y + (bounds.maxs.y - middle.y), top);
    face[1] = qa_v3(point.x + (bounds.maxs.x - middle.x), point.y + (bounds.mins.y - middle.y), top);
    face[2] = qa_v3(point.x + (bounds.mins.x - middle.x), point.y + (bounds.mins.y - middle.y), top);
    face[3] = qa_v3(point.x + (bounds.mins.x - middle.x), point.y + (bounds.maxs.y - middle.y), top);
}
static bool bobbing(aas_reach_build *work, qa_error *error) {
    face_reach *candidates = NULL;
    for (size_t entity = 0; entity < work->entities.count; ++entity) {
        if (!entity_is(work, entity, "classname", "func_bobbing")) continue;
        qa_bounds bounds; int32_t model, spawnflags; float height; bool found;
        if (!model_bounds(work, entity, false, &bounds, &model, &found, error)) return false;
        if (!found || model <= 0) continue;
        qa_vec3 origin;
        if (!vector(work, entity, "origin", &origin, &found, error) ||
            !integer(work, entity, "spawnflags", &spawnflags, error) ||
            !number(work, entity, "height", &height, error)) return false;
        if (height == 0) height = 32;
        bounds = qa_bounds_translate(bounds, origin);
        qa_vec3 middle = qa_vec_scale(qa_vec_add(bounds.mins, bounds.maxs), .5f),
            axis = (spawnflags & 1) ? qa_v3(1, 0, 0) :
                (spawnflags & 2) ? qa_v3(0, 1, 0) : qa_v3(0, 0, 1);
        qa_vec3 move[2] = {qa_vec_ma(middle, -height, axis), qa_vec_ma(middle, height, axis)},
            faces[2][4], tops[2];
        qa_aas_plane planes[2];
        bool clear = true;
        for (unsigned i = 0; i < 2; ++i) {
            bobbing_face(bounds, middle, move[i], faces[i]);
            planes[i] = (qa_aas_plane){.normal = {0, 0, 1}, .distance = faces[i][0].z, .type = 2};
            tops[i] = move[i]; tops[i].z += (bounds.maxs.z - middle.z) + 24;
            uint32_t area;
            if (!qa_aas_point_area(work->source, tops[i], &area, error)) return false;
            if (!area) clear = false;
        }
        if (!clear) continue;
        if (!candidates) {
            candidates = qa_arena_alloc(&work->storage,
                work->source->count[QA_AAS_AREAS] * 2 * sizeof(*candidates), _Alignof(face_reach), error);
            if (!candidates) return false;
        }
        face_reach *starts = candidates, *ends = candidates + work->source->count[QA_AAS_AREAS];
        uint32_t flags;
        if (!team_flags(work, entity, &flags, error)) return false;
        for (unsigned direction = 0; direction < 2; ++direction) {
            size_t start_count = face_reachabilities(work, faces[direction], planes + direction, true, starts),
                end_count = face_reachabilities(work, faces[1 - direction], planes + 1 - direction, false, ends);
            int32_t start_coordinate, end_coordinate;
            if (!aas_reach_integer(qa_vec_dot(move[direction], axis), &start_coordinate, error) ||
                !aas_reach_integer(qa_vec_dot(move[1 - direction], axis), &end_coordinate, error)) return false;
            for (size_t i = start_count; i > 0; --i) {
                face_reach *from = starts + i - 1;
                for (size_t j = end_count; j > 0; --j) {
                    const face_reach *to = ends + j - 1;
                    qa_vec3 outward = qa_vec_sub(from->start, tops[direction]); outward.z = 0;
                    outward = qa_vec_normalize(outward);
                    qa_vec3 start = qa_vec_ma(from->start, 1, outward),
                        end = qa_vec_ma(from->start, 16, outward);
                    ++start.z; ++end.z;
                    qa_aas_crossing crossings[10]; size_t count;
                    if (!qa_aas_trace_areas(work->source, work->query, start, end, crossings, 10, &count, error)) return false;
                    if (!count) continue;
                    from->start = count > 1 ? crossings[1].point : end;
                    uint32_t start_area, end_area;
                    if (!qa_aas_point_area(work->source, from->start, &start_area, error) ||
                        !qa_aas_point_area(work->source, to->end, &end_area, error)) return false;
                    if (!start_area || !end_area) continue;
                    qa_aas_reach reach = {.area = (int32_t)to->area, .start = from->start, .end = to->end,
                        .face = qa_i32_from_u32(((uint32_t)spawnflags << 16) | (uint32_t)model),
                        .edge = qa_i32_from_u32(((uint32_t)start_coordinate << 16) | ((uint32_t)end_coordinate & 0xffffu)),
                        .travel_type = (int32_t)((uint32_t)AAS_REACH_BOBBING | flags)};
                    if (!aas_reach_time(work->movement.func_bob, &reach.travel_time, error) ||
                        !aas_reach_add(work, from->area, reach, error)) return false;
                }
            }
            if (!(spawnflags & 3)) break;
        }
    }
    return true;
}
bool aas_reach_special_prepare(aas_reach_build *work, qa_error *error) {
    qa_entity_syntax syntax = work->geometry->family == QA_BSP_Q1 ? QA_ENTITY_Q1 :
        work->geometry->family == QA_BSP_Q3 ? QA_ENTITY_Q3 :
        work->geometry->format == QA_BSP_IBSP44 ? QA_ENTITY_Q2_RERELEASE : QA_ENTITY_Q2_CLASSIC;
    if (!qa_entities_parse(work->geometry->lumps[QA_BSP_ENTITIES].bytes, syntax, &work->entities, error)) return false;
    static const char *const items[] = {"item_armor_body", "item_armor_combat", "item_health_mega",
        "weapon_grenadelauncher", "weapon_rocketlauncher", "weapon_lightning", "weapon_plasmagun",
        "weapon_railgun", "weapon_bfg", "item_quad", "item_regen", "item_invulnerability"};
    const qa_bounds bounds = {{-15, -15, -15}, {15, 15, 15}};
    for (size_t entity = 0; entity < work->entities.count; ++entity) {
        qa_bytes classname;
        if (!qa_entity_value(&work->entities, entity, "classname", &classname)) continue;
        bool important = false;
        for (size_t i = 0; i < sizeof(items) / sizeof(*items); ++i)
            if (text_is(classname, items[i])) { important = true; break; }
        if (!important) continue;
        qa_vec3 origin; bool found; int32_t spawnflags;
        if (!vector(work, entity, "origin", &origin, &found, error)) return false;
        if (!found) continue;
        if (!integer(work, entity, "spawnflags", &spawnflags, error)) return false;
        if (!(spawnflags & 1)) {
            qa_vec3 end = origin; end.z -= 100;
            qa_trace_result drop;
            if (!aas_reach_world_trace(work, origin, end, &bounds, &drop, error)) return false;
            if (!drop.start_solid) origin = drop.end;
        }
        uint32_t area; qa_vec3 point;
        if (!qa_navigation_best_reachable(work->navigation, work->workspace, origin, bounds,
            &point, &area, &found, error)) return false;
        work->settings[found ? area : 0].flags |= AAS_REACH_WEAPON_JUMP_AREA;
    }
    for (size_t i = 1; i < work->source->count[QA_AAS_AREAS]; ++i)
        if (work->settings[i].contents & AAS_REACH_JUMPPAD)
            work->settings[i].flags |= AAS_REACH_WEAPON_JUMP_AREA;
    return true;
}
bool aas_reach_special(aas_reach_build *work, qa_error *error) {
    return jump_pad(work, error) && teleport(work, error) &&
        elevator(work, error) && bobbing(work, error);
}
