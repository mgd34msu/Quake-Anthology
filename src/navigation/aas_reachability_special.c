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
    qa_bounds *bounds, bool *found, qa_error *error) {
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
        if (!model_bounds(work, entity, false, &bounds, &found, error)) return false;
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
    if (!model_bounds(work, entity, true, bounds, found, error)) return false;
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
bool aas_reach_special(aas_reach_build *work, qa_error *error) {
    qa_entity_syntax syntax = work->geometry->family == QA_BSP_Q1 ? QA_ENTITY_Q1 :
        work->geometry->family == QA_BSP_Q3 ? QA_ENTITY_Q3 :
        work->geometry->format == QA_BSP_IBSP44 ? QA_ENTITY_Q2_RERELEASE : QA_ENTITY_Q2_CLASSIC;
    if (!qa_entities_parse(work->geometry->lumps[QA_BSP_ENTITIES].bytes, syntax, &work->entities, error)) return false;
    return jump_pad(work, error) && teleport(work, error);
}
