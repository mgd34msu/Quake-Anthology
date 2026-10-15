#include "aas_reachability_internal.h"

typedef struct step_candidate {
    float distance, length;
    int32_t edge;
    qa_vec3 start, end, normal;
    bool found;
} step_candidate;

static const qa_aas_face *area_face(const qa_aas_view *world, const qa_aas_area *area,
    int32_t ordinal, int32_t *number) {
    int32_t index = world->face_index[area->first_face + ordinal];
    if (number) *number = index;
    return world->faces + nav_aas_oriented_index(index);
}
static const qa_aas_edge *face_edge(const qa_aas_view *world, const qa_aas_face *face,
    int32_t ordinal, int32_t *number) {
    int32_t index = world->edge_index[face->first_edge + ordinal];
    if (number) *number = index;
    return world->edges + nav_aas_oriented_index(index);
}
static bool close_bounds(qa_bounds a, qa_bounds b, bool vertical) {
    return a.mins.x <= b.maxs.x + 10 && a.maxs.x >= b.mins.x - 10 &&
        a.mins.y <= b.maxs.y + 10 && a.maxs.y >= b.mins.y - 10 &&
        (!vertical || (a.mins.z <= b.maxs.z + 10 && a.maxs.z >= b.mins.z - 10));
}
qa_vec3 aas_reach_face_center(const qa_aas_view *world, const qa_aas_face *face) {
    qa_vec3 center = {0};
    for (int32_t i = 0; i < face->edge_count; ++i) {
        const qa_aas_edge *edge = face_edge(world, face, i, NULL);
        center = qa_vec_add(center, world->vertices[edge->vertices[0]]);
        center = qa_vec_add(center, world->vertices[edge->vertices[1]]);
    }
    return face->edge_count ? qa_vec_scale(center, .5f / (float)face->edge_count) : center;
}
static float face_area(const qa_aas_view *world, const qa_aas_face *face) {
    if (!face->edge_count) return 0;
    int32_t index;
    const qa_aas_edge *first = face_edge(world, face, 0, &index);
    qa_vec3 origin = world->vertices[first->vertices[index < 0 ? 1 : 0]];
    float total = 0;
    for (int32_t i = 1; i < face->edge_count - 1; ++i) {
        const qa_aas_edge *edge = face_edge(world, face, i, &index);
        qa_vec3 a = world->vertices[edge->vertices[index < 0 ? 1 : 0]];
        qa_vec3 b = world->vertices[edge->vertices[index < 0 ? 0 : 1]];
        total = (float)(total + .5 * qa_vec_length(qa_vec_cross(
            qa_vec_sub(a, origin), qa_vec_sub(b, origin))));
    }
    return total;
}
static float area_volume(const qa_aas_view *world, uint32_t area_number) {
    const qa_aas_area *area = world->areas + area_number;
    qa_vec3 corner = {0};
    for (int32_t i = 0; i < area->face_count; ++i) {
        const qa_aas_face *face = area_face(world, area, i, NULL);
        if (!face->edge_count) continue;
        const qa_aas_edge *edge = face_edge(world, face, 0, NULL);
        corner = world->vertices[edge->vertices[0]];
        break;
    }
    float volume = 0;
    for (int32_t i = 0; i < area->face_count; ++i) {
        const qa_aas_face *face = area_face(world, area, i, NULL);
        const qa_aas_plane *plane = world->planes + (face->plane ^ (face->back_area != (int32_t)area_number));
        float distance = -(qa_vec_dot(corner, plane->normal) - plane->distance);
        volume += distance * face_area(world, face);
    }
    return volume / 3;
}
static bool add(aas_reach_build *work, uint32_t from, uint32_t to, int32_t edge,
    qa_vec3 start, qa_vec3 end, int32_t type, float time, bool *found, qa_error *error) {
    qa_aas_reach reach = {.area = (int32_t)to, .edge = edge,
        .start = start, .end = end, .travel_type = type};
    if (!aas_reach_time(time, &reach.travel_time, error) ||
        !aas_reach_add(work, from, reach, error)) return false;
    *found = true;
    return true;
}
static bool swim(aas_reach_build *work, uint32_t from, uint32_t to, bool *found, qa_error *error) {
    if (!aas_reach_swimming(work, from) || !aas_reach_swimming(work, to) ||
        aas_reach_crouched(work, to)) return true;
    const qa_aas_view *world = work->source;
    const qa_aas_area *area1 = world->areas + from, *area2 = world->areas + to;
    if (!close_bounds(area1->bounds, area2->bounds, true)) return true;
    for (int32_t i = 0; i < area1->face_count; ++i) {
        int32_t signed_face;
        const qa_aas_face *face = area_face(world, area1, i, &signed_face);
        uint32_t number = (uint32_t)nav_aas_oriented_index(signed_face);
        if (!face->edge_count) continue;
        for (int32_t j = 0; j < area2->face_count; ++j) {
            if (number != nav_aas_oriented_index(world->face_index[area2->first_face + j])) continue;
            qa_vec3 start = aas_reach_face_center(world, face);
            uint32_t contents;
            if (!nav_contents(&work->navigation->services, &work->navigation->graph->view.profile,
                (qa_actor_id){0}, start, true, &contents, error)) return false;
            if (!(contents & (QA_NAV_WATER | QA_NAV_SLIME | QA_NAV_LAVA))) continue;
            qa_aas_reach reach = {.area = (int32_t)to, .face = (int32_t)number,
                .start = start, .end = qa_vec_ma(start, -2,
                    world->planes[face->plane ^ (signed_face < 0)].normal),
                .travel_type = AAS_REACH_SWIM, .travel_time = 1};
            if (area_volume(world, to) < 800) reach.travel_time += 200;
            if (!aas_reach_add(work, from, reach, error)) return false;
            *found = true;
            return true;
        }
    }
    return true;
}
static bool equal_floor(aas_reach_build *work, uint32_t from, uint32_t to,
    bool *found, qa_error *error) {
    if (!aas_reach_grounded(work, from) || !aas_reach_grounded(work, to)) return true;
    const qa_aas_view *world = work->source;
    const qa_aas_area *area1 = world->areas + from, *area2 = world->areas + to;
    if (!close_bounds(area1->bounds, area2->bounds, false) ||
        area2->bounds.mins.z > area1->bounds.maxs.z) return true;
    step_candidate best = {.distance = 99999};
    for (int32_t i = 0; i < area1->face_count; ++i) {
        const qa_aas_face *face1 = area_face(world, area1, i, NULL);
        if (!(face1->flags & AAS_REACH_FACE_GROUND)) continue;
        for (int32_t j = 0; j < area2->face_count; ++j) {
            const qa_aas_face *face2 = area_face(world, area2, j, NULL);
            if (!(face2->flags & AAS_REACH_FACE_GROUND)) continue;
            for (int32_t k = 0; k < face1->edge_count; ++k) {
                int32_t number;
                const qa_aas_edge *edge = face_edge(world, face1, k, &number);
                for (int32_t l = 0; l < face2->edge_count; ++l) {
                    if (nav_aas_oriented_index(number) !=
                        nav_aas_oriented_index(world->edge_index[face2->first_edge + l])) continue;
                    qa_vec3 v0 = world->vertices[edge->vertices[0]], v1 = world->vertices[edge->vertices[1]];
                    float length = qa_vec_length(qa_vec_sub(v1, v0));
                    qa_vec3 midpoint = qa_vec_scale(qa_vec_add(v0, v1), .5f);
                    qa_vec3 along = number < 0 ? qa_vec_sub(v1, v0) : qa_vec_sub(v0, v1);
                    qa_vec3 normal = qa_vec_normalize(qa_vec_cross(along, world->planes[face2->plane].normal));
                    qa_vec3 start = qa_vec_ma_wide(midpoint, .1, normal), end = qa_vec_ma(midpoint, 5, normal);
                    end.z += .125f;
                    if (start.z < best.distance || (start.z < best.distance + 1 && length > best.length))
                        best = (step_candidate){.distance = start.z, .length = length, .edge = number,
                            .start = start, .end = end, .found = true};
                }
            }
        }
    }
    if (!best.found) return true;
    float time = 1;
    if (!aas_reach_crouched(work, from) && aas_reach_crouched(work, to)) time += work->movement.start_crouch;
    return add(work, from, to, best.edge, best.start, best.end, AAS_REACH_WALK, time, found, error);
}

static void swap_float(float *a, float *b) { float value = *a; *a = *b; *b = value; }
static void swap_vector(qa_vec3 *a, qa_vec3 *b) { qa_vec3 value = *a; *a = *b; *b = value; }

static void step_candidates(const aas_reach_build *work, uint32_t from, uint32_t to,
    step_candidate *ground, step_candidate *water) {
    const qa_aas_view *world = work->source;
    const qa_aas_area *area1 = world->areas + from, *area2 = world->areas + to;
    const qa_vec3 up = {0, 0, 1};
    for (int32_t i = 0; i < area1->face_count; ++i) {
        int32_t signed_face;
        const qa_aas_face *face1 = area_face(world, area1, i, &signed_face);
        bool face_side = signed_face < 0;
        if (!(face1->flags & AAS_REACH_FACE_GROUND) &&
            (!aas_reach_swimming(work, from) ||
             world->planes[face1->plane ^ (!face_side)].normal.z < .7f)) continue;
        for (int32_t k = 0; k < face1->edge_count; ++k) {
            int32_t signed_edge;
            const qa_aas_edge *edge1 = face_edge(world, face1, k, &signed_edge);
            bool side = signed_edge < 0;
            if (!(face1->flags & AAS_REACH_FACE_GROUND)) side = side == face_side;
            qa_vec3 v1 = world->vertices[edge1->vertices[side ? 0 : 1]],
                v2 = world->vertices[edge1->vertices[side ? 1 : 0]];
            qa_vec3 normal = qa_vec_normalize(qa_vec_cross(qa_vec_sub(v2, v1), up));
            /* Original dist is reused for separation in the inner loop. */
            float distance = qa_vec_dot(normal, v1);
            for (int32_t j = 0; j < area2->face_count; ++j) {
                const qa_aas_face *face2 = area_face(world, area2, j, NULL);
                if (!(face2->flags & AAS_REACH_FACE_GROUND)) continue;
                for (int32_t l = 0; l < face2->edge_count; ++l) {
                    const qa_aas_edge *edge2 = face_edge(world, face2, l, NULL);
                    qa_vec3 v3 = world->vertices[edge2->vertices[0]], v4 = world->vertices[edge2->vertices[1]];
                    float diff1 = qa_vec_dot(normal, v3) - distance,
                        diff2 = qa_vec_dot(normal, v4) - distance;
                    if (diff1 < -.1f || diff1 > .1f || diff2 < -.1f || diff2 > .1f) continue;
                    qa_vec3 ort = qa_vec_cross(up, normal);
                    float square = qa_vec_dot(ort, ort);
                    if (square == 0) continue;
                    float y1 = v1.z, y2 = v2.z, y3 = v3.z, y4 = v4.z;
                    float x1 = qa_vec_dot(v1, ort) / square, x2 = qa_vec_dot(v2, ort) / square,
                        x3 = qa_vec_dot(v3, ort) / square, x4 = qa_vec_dot(v4, ort) / square;
                    if (x1 > x2) { swap_float(&x1, &x2); swap_float(&y1, &y2); swap_vector(&v1, &v2); }
                    if (x3 > x4) { swap_float(&x3, &x4); swap_float(&y3, &y4); swap_vector(&v3, &v4); }
                    if (x2 <= x3 || x4 <= x1) continue;
                    float dist1, dist2;
                    qa_vec3 p11, p12, p21, p22;
                    if (x1 - .5f < x3 && x4 < x2 + .5f && x3 - .5f < x1 && x2 < x4 + .5f) {
                        dist1 = y3 - y1; dist2 = y4 - y2;
                        p11 = v1; p21 = v2; p12 = v3; p22 = v4;
                    } else {
                        if (x1 > x3 - .1f && x1 < x3 + .1f) {
                            dist1 = y3 - y1; p11 = v1; p12 = v3;
                        } else if (x1 < x3) {
                            float y = y1 + (x3 - x1) * (y2 - y1) / (x2 - x1);
                            dist1 = y3 - y; p11 = v3; p11.z = y; p12 = v3;
                        } else {
                            float y = y3 + (x1 - x3) * (y4 - y3) / (x4 - x3);
                            dist1 = y - y1; p11 = v1; p12 = v1; p12.z = y;
                        }
                        if (x2 > x4 - .1f && x2 < x4 + .1f) {
                            dist2 = y4 - y2; p21 = v2; p22 = v4;
                        } else if (x2 < x4) {
                            float y = y3 + (x2 - x3) * (y4 - y3) / (x4 - x3);
                            dist2 = y - y2; p21 = v2; p22 = v2; p22.z = y;
                        } else {
                            float y = y1 + (x4 - x1) * (y2 - y1) / (x2 - x1);
                            dist2 = y4 - y; p21 = v4; p21.z = y; p22 = v4;
                        }
                    }
                    qa_vec3 start, end;
                    if (dist1 > dist2 - 1 && dist1 < dist2 + 1) {
                        distance = dist1; start = qa_vec_scale(qa_vec_add(p11, p21), .5f);
                        end = qa_vec_scale(qa_vec_add(p12, p22), .5f);
                    } else if (dist1 < dist2) { distance = dist1; start = p11; end = p12; }
                    else { distance = dist2; start = p21; end = p22; }
                    float length = qa_vec_length(qa_vec_sub(p22, p12));
                    step_candidate *best = face1->flags & AAS_REACH_FACE_GROUND ? ground : water;
                    float prior = best->found ? best->distance : 99999;
                    if (distance < prior || (distance < prior + 1 && length > best->length))
                        *best = (step_candidate){.distance = distance, .length = length,
                            .edge = (int32_t)nav_aas_oriented_index(signed_edge), .start = start,
                            .end = end, .normal = normal, .found = true};
                }
            }
        }
    }
}
float aas_reach_fall_delta(const aas_reach_build *work, float distance) {
    float time = (float)sqrt((double)fabsf(distance) * 2 / work->movement.gravity);
    float delta = time * work->movement.gravity;
    return (float)((delta * delta) * .0001);
}
static bool ledge_time(const aas_reach_build *work, uint32_t to, float distance,
    bool separate, uint16_t *time, qa_error *error) {
    const aas_reach_settings *s = &work->movement;
    if (!aas_reach_time(s->start_ledge + fabs((double)distance) * 50 / s->gravity,
        time, error)) return false;
    if (!aas_reach_swimming(work, to) && !(work->settings[to].contents & AAS_REACH_JUMPPAD)) {
        float delta = aas_reach_fall_delta(work, distance);
        if (delta > s->fall_delta5 && !aas_reach_time(*time + s->fall_damage5, time, error)) return false;
        if ((separate || delta <= s->fall_delta5) && delta > s->fall_delta10 &&
            !aas_reach_time(*time + s->fall_damage10, time, error)) return false;
    }
    return true;
}
static bool crosses_portal(aas_reach_build *work, qa_vec3 start, qa_vec3 end,
    bool *crossed, qa_error *error) {
    qa_aas_crossing crossings[10]; size_t count;
    if (!qa_aas_trace_areas(work->source, work->query, start, end, crossings, 10, &count, error)) return false;
    *crossed = false;
    for (size_t i = 0; i < count; ++i)
        if (work->settings[crossings[i].area].contents & 8) { *crossed = true; break; }
    return true;
}
static bool step(aas_reach_build *work, uint32_t from, uint32_t to, bool *found, qa_error *error) {
    if ((!aas_reach_grounded(work, from) && !aas_reach_swimming(work, from)) ||
        (!aas_reach_grounded(work, to) && !aas_reach_swimming(work, to))) return true;
    if (!close_bounds(work->source->areas[from].bounds, work->source->areas[to].bounds, false)) return true;
    step_candidate ground = {0}, water = {0};
    step_candidates(work, from, to, &ground, &water);
    const aas_reach_settings *s = &work->movement;
    if (ground.found && ground.distance >= 0 && ground.distance < s->max_step) {
        float time = !aas_reach_crouched(work, from) && aas_reach_crouched(work, to) ? s->start_crouch : 0;
        return add(work, from, to, ground.edge, qa_vec_ma_wide(ground.start, .1, ground.normal),
            qa_vec_ma(ground.end, 5, ground.normal), AAS_REACH_WALK, time, found, error);
    }
    if (water.found) {
        qa_vec3 point = qa_vec_ma(water.end, -2, water.normal); point.z -= s->max_water_jump;
        uint32_t area;
        if (!qa_aas_point_area(work->source, point, &area, error)) return false;
        if (aas_reach_swimming(work, area) && water.distance < s->max_water_jump + 24 &&
            !aas_reach_crouched(work, from) && !aas_reach_crouched(work, to))
            return add(work, from, to, water.edge, water.start, qa_vec_ma(water.end, 15, water.normal),
                AAS_REACH_WATERJUMP, s->water_jump, found, error);
    }
    if (ground.found && ground.distance > 0 && ground.distance < s->max_barrier &&
        (!water.found || ground.distance - water.distance < 16) &&
        !aas_reach_crouched(work, from) && !aas_reach_crouched(work, to))
        return add(work, from, to, ground.edge, qa_vec_ma_wide(ground.start, .1, ground.normal),
            qa_vec_ma(ground.end, 5, ground.normal), AAS_REACH_BARRIER, s->barrier_jump, found, error);
    if (!ground.found || ground.distance >= 0) return true;
    if (ground.distance > -s->max_step)
        return add(work, from, to, ground.edge, qa_vec_ma_wide(ground.start, .1, ground.normal),
            qa_vec_ma(ground.end, 5, ground.normal), AAS_REACH_WALK, 1, found, error);
    if (s->max_fall_height != 0 && !(fabsf(ground.distance) < s->max_fall_height)) return true;
    qa_vec3 ground_end = qa_vec_ma(ground.end, 2, ground.normal), start = ground_end, end = ground_end;
    start.z = ground.start.z; end.z += 4;
    qa_aas_presence_trace trace;
    if (!qa_aas_trace_presence(work->source, work->query, start, end, 2, &trace, error)) return false;
    qa_vec3 point = trace.end; point.z += 1; uint32_t area;
    if (trace.start_solid || trace.fraction < 1) return true;
    if (!qa_aas_point_area(work->source, point, &area, error)) return false;
    if (area != to) return true;
    bool portal;
    if (!crosses_portal(work, start, end, &portal, error)) return false;
    if (portal) return true;
    uint16_t time;
    if (!ledge_time(work, to, ground.distance, true, &time, error)) return false;
    return add(work, from, to, ground.edge, ground.start, ground_end, AAS_REACH_LEDGE,
        time, found, error);
}

static qa_vec3 edge_projection(qa_vec3 point, qa_vec3 origin, qa_vec3 direction,
    const qa_aas_plane *plane) {
    qa_vec3 flat;
    if (direction.x != 0) {
        float a = direction.y / direction.x, b = origin.y - a * origin.x;
        float x = (qa_vec_dot(point, direction) - (a * direction.x + b * direction.y)) / direction.x;
        flat = qa_v3(x, a * x + b, 0);
    } else flat = qa_v3(origin.x, point.y, 0);
    flat.z = (plane->distance - qa_vec_dot(plane->normal, flat)) / plane->normal.z;
    return flat;
}
static bool between(qa_vec3 point, qa_vec3 a, qa_vec3 b) {
    return qa_vec_dot(qa_vec_sub(point, a), qa_vec_sub(point, b)) <= 0;
}
static void closest_update(aas_reach_closest_edges *best, qa_vec3 start, qa_vec3 end, bool range) {
    float distance = qa_vec_length(qa_vec_sub(end, start));
    if (range && best->found && distance > best->distance - .5f && distance < best->distance + .5f) {
        float first = qa_vec_length(qa_vec_sub(best->start[0], start)),
            second = qa_vec_length(qa_vec_sub(best->start[1], start));
        float prior = qa_vec_length(qa_vec_sub(best->start[0], best->start[1]));
        if (first > second) {
            if (first > prior) best->start[1] = start;
        } else if (second > prior) best->start[0] = start;
        first = qa_vec_length(qa_vec_sub(best->end[0], end));
        second = qa_vec_length(qa_vec_sub(best->end[1], end));
        prior = qa_vec_length(qa_vec_sub(best->end[0], best->end[1]));
        if (first > second) {
            if (first > prior) best->end[1] = end;
        } else if (second > prior) best->end[0] = end;
    } else if (distance < best->distance) {
        best->distance = distance;
        best->start[0] = best->start[1] = start;
        best->end[0] = best->end[1] = end;
        best->found = true;
    }
}
void aas_reach_closest_edge_points(qa_vec3 v1, qa_vec3 v2, qa_vec3 v3, qa_vec3 v4,
    const qa_aas_plane *plane1, const qa_aas_plane *plane2, aas_reach_closest_edges *best) {
    qa_vec3 direction1 = qa_vec_sub(v2, v1), direction2 = qa_vec_sub(v4, v3);
    direction1.z = direction2.z = 0;
    qa_vec3 p1 = edge_projection(v1, v3, direction2, plane2),
        p2 = edge_projection(v2, v3, direction2, plane2),
        p3 = edge_projection(v3, v1, direction1, plane1),
        p4 = edge_projection(v4, v1, direction1, plane1);
    bool projected = false;
    if (between(p1, v3, v4)) { closest_update(best, v1, p1, true); projected = true; }
    if (between(p2, v3, v4)) { closest_update(best, v2, p2, true); projected = true; }
    if (between(p3, v1, v2)) { closest_update(best, p3, v3, true); projected = true; }
    if (between(p4, v1, v2)) { closest_update(best, p4, v4, true); projected = true; }
    if (!projected) {
        closest_update(best, v1, v3, false); closest_update(best, v1, v4, false);
        closest_update(best, v2, v3, false); closest_update(best, v2, v4, false);
    }
}
static float max_jump_height(const aas_reach_build *work) {
    float time = work->movement.jump_velocity / work->movement.gravity;
    return (float)(.5 * work->movement.gravity * time * time);
}
float aas_reach_max_jump_distance(const aas_reach_build *work) {
    const aas_reach_settings *settings = &work->movement;
    float fall_time = (float)sqrt(settings->max_jump_fall_height / (.5 * settings->gravity));
    return settings->max_velocity * (fall_time + settings->jump_velocity / settings->gravity);
}
bool aas_reach_horizontal_jump_speed(const aas_reach_build *work, float z_velocity,
    qa_vec3 start, qa_vec3 end, float *speed) {
    float gravity = work->movement.gravity, ascent = z_velocity / gravity;
    float maximum = (float)(.5 * gravity * ascent * ascent);
    float height = (start.z + maximum) - end.z;
    if (height < 0) return false;
    float time = (float)sqrt(height / (.5 * gravity)), denominator = time + ascent;
    if (denominator == 0) return false;
    qa_vec3 direction = qa_vec_sub(end, start); direction.z = 0;
    *speed = qa_vec_length(direction) / denominator;
    return *speed <= work->movement.max_velocity;
}
static bool jump(aas_reach_build *work, uint32_t from, uint32_t to,
    bool *found, qa_error *error) {
    if (!aas_reach_grounded(work, from) || !aas_reach_grounded(work, to) ||
        aas_reach_crouched(work, from) || aas_reach_crouched(work, to)) return true;
    const qa_aas_view *world = work->source;
    const qa_aas_area *area1 = world->areas + from, *area2 = world->areas + to;
    const aas_reach_settings *settings = &work->movement;
    float maximum_distance = 2 * aas_reach_max_jump_distance(work);
    if (area1->bounds.mins.x > area2->bounds.maxs.x + maximum_distance ||
        area1->bounds.maxs.x < area2->bounds.mins.x - maximum_distance ||
        area1->bounds.mins.y > area2->bounds.maxs.y + maximum_distance ||
        area1->bounds.maxs.y < area2->bounds.mins.y - maximum_distance ||
        area2->bounds.mins.z > area1->bounds.maxs.z + max_jump_height(work)) return true;
    aas_reach_closest_edges closest = {.distance = 999999};
    for (int32_t i = 0; i < area1->face_count; ++i) {
        const qa_aas_face *face1 = area_face(world, area1, i, NULL);
        if (!(face1->flags & AAS_REACH_FACE_GROUND)) continue;
        for (int32_t j = 0; j < area2->face_count; ++j) {
            const qa_aas_face *face2 = area_face(world, area2, j, NULL);
            if (!(face2->flags & AAS_REACH_FACE_GROUND)) continue;
            for (int32_t k = 0; k < face1->edge_count; ++k) {
                const qa_aas_edge *first = face_edge(world, face1, k, NULL);
                for (int32_t l = 0; l < face2->edge_count; ++l) {
                    const qa_aas_edge *second = face_edge(world, face2, l, NULL);
                    aas_reach_closest_edge_points(world->vertices[first->vertices[0]], world->vertices[first->vertices[1]],
                        world->vertices[second->vertices[0]], world->vertices[second->vertices[1]],
                        world->planes + face1->plane, world->planes + face2->plane, &closest);
                }
            }
        }
    }
    if (!closest.found || !(closest.distance > 4 && closest.distance < maximum_distance)) return true;
    qa_vec3 start = qa_vec_scale(qa_vec_add(closest.start[0], closest.start[1]), .5f),
        end = qa_vec_scale(qa_vec_add(closest.end[0], closest.end[1]), .5f);
    int32_t type; float speed;
    if (closest.distance <= 48 && fabsf(start.z - end.z) < 8) {
        speed = 400; type = AAS_REACH_LEDGE;
    } else if (aas_reach_horizontal_jump_speed(work, 0, start, end, &speed)) {
        speed *= 1.2f; type = AAS_REACH_LEDGE;
    } else {
        if (!aas_reach_horizontal_jump_speed(work, settings->jump_velocity, start, end, &speed)) return true;
        speed *= 1.05f; type = AAS_REACH_JUMP;
        qa_vec3 horizontal = qa_vec_sub(end, start); horizontal.z = 0;
        if (qa_vec_length(horizontal) < 10) return true;
    }
    qa_vec3 direction = qa_vec_normalize(qa_vec_sub(end, start));
    for (unsigned i = 0; i < 2; ++i) {
        qa_vec3 test = i ? qa_vec_ma(end, -1, direction) : qa_vec_ma(start, 1, direction);
        qa_vec3 below = test; below.z -= 100;
        qa_aas_presence_trace trace;
        if (!qa_aas_trace_presence(world, work->query, test, below, 2, &trace, error)) return false;
        if (trace.start_solid) return true;
        if (trace.fraction < 1 && world->planes[trace.plane].normal.z >= .7f &&
            test.z - trace.end.z <= settings->max_barrier) {
            uint32_t contents;
            if (!nav_contents(&work->navigation->services, &work->navigation->graph->view.profile,
                (qa_actor_id){0}, trace.end, true, &contents, error)) return false;
            if (!(contents & (QA_NAV_SLIME | QA_NAV_LAVA))) return true;
        }
    }
    direction = qa_vec_sub(end, start); direction.z = 0; direction = qa_vec_normalize(direction);
    qa_vec3 sideways = qa_vec_cross(direction, qa_v3(0, 0, 1));
    uint32_t stops = 1 | 4 | 8 | 16 | 32;
    if (!(work->settings[from].contents & 8) && !(work->settings[to].contents & 8)) stops |= 4096;
    bool arrived = false;
    for (unsigned i = 0; i < 3; ++i) {
        qa_vec3 test_end = i == 1 ? qa_vec_add(end, sideways) : i == 2 ? qa_vec_sub(end, sideways) : end;
        direction = qa_vec_sub(test_end, start); direction.z = 0; direction = qa_vec_normalize(direction);
        qa_nav_prediction_query query = {.origin = start, .velocity = qa_vec_scale(direction, speed),
            .command_move = {0, 0, type == AAS_REACH_JUMP ? settings->jump_velocity : 0},
            .presence = 2, .command_frames = 3, .maximum_frames = 30, .frame_ms = 100,
            .stop_events = stops, .stop_area = QA_NAV_NO_INDEX, .on_ground = true,
            .world_only = true};
        if (!aas_reach_predict(work, &query, error)) return false;
        const qa_nav_prediction_result *move = &work->prediction;
        uint32_t frames = aas_reach_prediction_frames(work);
        if (frames >= 30 || (move->stop_event & (8 | 16 | 4096))) return true;
        qa_vec3 back = qa_vec_ma(move->end, -64, direction); back.z += 1;
        qa_aas_crossing crossings[10]; size_t count;
        if (!qa_aas_trace_areas(world, work->query, move->end, back, crossings, 10, &count, error)) return false;
        for (size_t j = 0; j < count; ++j) if (crossings[j].area == to) { arrived = true; break; }
        if (arrived) break;
    }
    if (!arrived) return true;
    qa_vec3 delta = qa_vec_sub(end, start), horizontal = delta; horizontal.z = 0;
    float time = type == AAS_REACH_LEDGE && delta.z > qa_vec_length(horizontal) ?
        settings->start_ledge + delta.z * 50 / settings->gravity :
        settings->start_jump + qa_vec_length(delta) * 240 / settings->max_walk_velocity;
    uint16_t travel_time;
    if (!aas_reach_time(time, &travel_time, error)) return false;
    if (!(work->settings[to].contents & AAS_REACH_JUMPPAD)) {
        float fall = aas_reach_fall_delta(work, start.z - end.z);
        if (fall > settings->fall_delta5) {
            if (!aas_reach_time(travel_time + settings->fall_damage5, &travel_time, error)) return false;
        } else if (fall > settings->fall_delta10 &&
            !aas_reach_time(travel_time + settings->fall_damage10, &travel_time, error)) return false;
    }
    return add(work, from, to, 0, start, end, type, travel_time, found, error);
}

static bool ladder_link(aas_reach_build *work, uint32_t from, uint32_t to, int32_t face,
    int32_t edge, qa_vec3 start, qa_vec3 end, int32_t type, qa_error *error) {
    return aas_reach_add(work, from, (qa_aas_reach){.area = (int32_t)to, .face = face,
        .edge = edge, .start = start, .end = end, .travel_type = type, .travel_time = 10}, error);
}
static bool ladder(aas_reach_build *work, uint32_t from, uint32_t to,
    bool *found, qa_error *error) {
    if (!(work->settings[from].flags & AAS_REACH_LADDER_AREA) ||
        !(work->settings[to].flags & AAS_REACH_LADDER_AREA)) return true;
    const qa_aas_view *world = work->source;
    const qa_aas_area *area1 = world->areas + from, *area2 = world->areas + to;
    const qa_aas_face *first = NULL, *second = NULL;
    int32_t number1 = 0, number2 = 0, edge_number = 0;
    float area1_best = -9999, area2_best = -9999;
    for (int32_t i = 0; i < area1->face_count; ++i) {
        int32_t signed1;
        const qa_aas_face *face1 = area_face(world, area1, i, &signed1);
        if (!(face1->flags & AAS_REACH_FACE_LADDER)) continue;
        for (int32_t j = 0; j < area2->face_count; ++j) {
            int32_t signed2;
            const qa_aas_face *face2 = area_face(world, area2, j, &signed2);
            if (!(face2->flags & AAS_REACH_FACE_LADDER)) continue;
            bool shared = false;
            for (int32_t k = 0; k < face1->edge_count && !shared; ++k) {
                int32_t index = world->edge_index[face1->first_edge + k];
                for (int32_t l = 0; l < face2->edge_count; ++l) {
                    if (nav_aas_oriented_index(index) !=
                        nav_aas_oriented_index(world->edge_index[face2->first_edge + l])) continue;
                    float surface1 = face_area(world, face1), surface2 = face_area(world, face2);
                    if (surface1 > area1_best && surface2 > area2_best) {
                        first = face1; second = face2; number1 = signed1; number2 = signed2;
                        edge_number = index; area1_best = surface1; area2_best = surface2;
                    }
                    shared = true; break;
                }
            }
        }
    }
    if (!first) return true;
    const qa_aas_edge *edge = world->edges + nav_aas_oriented_index(edge_number);
    qa_vec3 v1 = world->vertices[edge->vertices[edge_number < 0 ? 1 : 0]],
        v2 = world->vertices[edge->vertices[edge_number < 0 ? 0 : 1]],
        midpoint = qa_vec_scale(qa_vec_add(v1, v2), .5f), shared = qa_vec_sub(v2, v1);
    const qa_aas_plane *plane1 = world->planes + (first->plane ^ (number1 < 0)),
        *plane2 = world->planes + (second->plane ^ (number2 < 0));
    qa_vec3 direction = qa_vec_normalize(qa_vec_cross(plane1->normal, shared));
    qa_vec3 point1 = qa_vec_ma(midpoint, -32, direction), point2 = qa_vec_ma(midpoint, 32, direction);
    /* Source abs() takes an int even though these dot products are floats. */
    bool vertical1 = fabsf(truncf(plane1->normal.z)) < .1f,
        vertical2 = fabsf(truncf(plane2->normal.z)) < .1f;
    if (!vertical1 && !vertical2) return true;
    int32_t unsigned_edge = (int32_t)nav_aas_oriented_index(edge_number);
    if (vertical1 && vertical2 && qa_vec_dot(plane1->normal, plane2->normal) > .7f &&
        fabsf(truncf(shared.z)) < .7f) {
        if (!ladder_link(work, from, to, number1, unsigned_edge, point1,
            qa_vec_ma(point2, -3, plane1->normal), AAS_REACH_LADDER, error) ||
            !ladder_link(work, to, from, number2, unsigned_edge, point2,
                qa_vec_ma(point1, -3, plane1->normal), AAS_REACH_LADDER, error)) return false;
        *found = true; return true;
    }
    if (vertical1 && (second->flags & AAS_REACH_FACE_GROUND)) {
        qa_vec3 end = point2; end.z += 16;
        if (!ladder_link(work, from, to, number1, unsigned_edge, point1,
            qa_vec_ma(end, -15, plane1->normal), AAS_REACH_LADDER, error) ||
            !ladder_link(work, to, from, number2, unsigned_edge, point2, point1,
                AAS_REACH_LEDGE, error)) return false;
        *found = true; return true;
    }
    if (!vertical1) return true;
    qa_vec3 lowest = {0}; float lowest_z = 99999; int32_t lowest_edge = 0;
    for (int32_t i = 0; i < first->edge_count; ++i) {
        int32_t number;
        const qa_aas_edge *current = face_edge(world, first, i, &number);
        qa_vec3 point = qa_vec_scale(qa_vec_add(world->vertices[current->vertices[0]],
            world->vertices[current->vertices[1]]), .5f);
        if (point.z < lowest_z) { lowest = point; lowest_z = point.z;
            lowest_edge = (int32_t)nav_aas_oriented_index(number); }
    }
    plane1 = world->planes + first->plane;
    qa_vec3 start = qa_vec_ma(lowest, 5, plane1->normal), end = start;
    start.z += 5; end.z -= 100;
    qa_aas_presence_trace trace;
    if (!qa_aas_trace_presence(world, work->query, start, end, 2, &trace, error)) return false;
    qa_vec3 trace_end = trace.end; trace_end.z += 1; uint32_t destination;
    if (!qa_aas_point_area(world, trace_end, &destination, error)) return false;
    const qa_aas_area *destination_area = world->areas + destination;
    for (int32_t i = 0; i < destination_area->face_count; ++i) {
        const qa_aas_face *face = area_face(world, destination_area, i, NULL);
        if ((face->flags & AAS_REACH_FACE_LADDER) &&
            fabsf(truncf(world->planes[face->plane].normal.z)) < .1f) return true;
    }
    if (destination == from || aas_reach_exists(work, from, destination) ||
        aas_reach_exists(work, destination, from) || !(start.z - trace_end.z < max_jump_height(work))) return true;
    qa_vec3 back = qa_vec_ma(lowest, -5, plane1->normal); back.z += 10;
    if (!ladder_link(work, from, destination, number1, lowest_edge, lowest, trace_end, AAS_REACH_LADDER, error) ||
        !ladder_link(work, destination, from, number1, lowest_edge, trace_end, back, AAS_REACH_JUMP, error)) return false;
    *found = true;
    return true;
}
bool aas_reach_geometry(aas_reach_build *work, uint32_t from, uint32_t to,
    bool *found, qa_error *error) {
    *found = false;
    if (!swim(work, from, to, found, error)) return false;
    if (*found) return true;
    if (!equal_floor(work, from, to, found, error)) return false;
    if (*found) return true;
    if (!step(work, from, to, found, error)) return false;
    if (*found) return true;
    if (!ladder(work, from, to, found, error)) return false;
    if (*found) return true;
    return jump(work, from, to, found, error);
}

bool aas_reach_walk_ledge(aas_reach_build *work, uint32_t area_number, qa_error *error) {
    if (!aas_reach_grounded(work, area_number) || aas_reach_swimming(work, area_number)) return true;
    const qa_aas_view *world = work->source;
    const qa_aas_area *area = world->areas + area_number;
    for (int32_t i = 0; i < area->face_count; ++i) {
        const qa_aas_face *floor = area_face(world, area, i, NULL);
        if (!(floor->flags & AAS_REACH_FACE_GROUND)) continue;
        for (int32_t k = 0; k < floor->edge_count; ++k) {
            int32_t edge_number;
            const qa_aas_edge *edge = face_edge(world, floor, k, &edge_number);
            for (int32_t j = 0; j < area->face_count; ++j) {
                int32_t wall_number;
                const qa_aas_face *wall = area_face(world, area, j, &wall_number);
                if (wall->flags & AAS_REACH_FACE_GROUND) continue;
                for (int32_t l = 0; l < wall->edge_count; ++l) {
                    if (nav_aas_oriented_index(edge_number) !=
                        nav_aas_oriented_index(world->edge_index[wall->first_edge + l])) continue;
                    uint32_t other = wall->front_area == (int32_t)area_number ?
                        (uint32_t)wall->back_area : (uint32_t)wall->front_area;
                    if (aas_reach_grounded(work, other)) {
                        bool gap = false, shared = false;
                        const qa_aas_area *neighbor = world->areas + other;
                        for (int32_t n = 0; n < neighbor->face_count && !shared; ++n) {
                            int32_t face_number;
                            const qa_aas_face *face = area_face(world, neighbor, n, &face_number);
                            if (nav_aas_oriented_index(face_number) == nav_aas_oriented_index(wall_number)) continue;
                            for (int32_t m = 0; m < face->edge_count; ++m)
                                if (nav_aas_oriented_index(world->edge_index[face->first_edge + m]) ==
                                    nav_aas_oriented_index(edge_number)) {
                                    gap = !(face->flags & AAS_REACH_FACE_SOLID) || !(face->flags & AAS_REACH_FACE_GROUND);
                                    shared = true; break;
                                }
                        }
                        if (!gap) break;
                    }
                    qa_vec3 v1 = world->vertices[edge->vertices[edge_number < 0 ? 1 : 0]],
                        v2 = world->vertices[edge->vertices[edge_number < 0 ? 0 : 1]];
                    qa_vec3 direction = qa_vec_normalize(qa_vec_cross(world->planes[floor->plane].normal,
                        qa_vec_sub(v2, v1)));
                    qa_vec3 midpoint = qa_vec_ma(qa_vec_scale(qa_vec_add(v1, v2), .5f), 8, direction);
                    qa_vec3 end = midpoint; end.z -= 1000;
                    qa_aas_presence_trace trace;
                    if (!qa_aas_trace_presence(world, work->query, midpoint, end, 4, &trace, error)) return false;
                    if (trace.start_solid) break;
                    uint32_t destination;
                    if (!qa_aas_point_area(world, trace.end, &destination, error)) return false;
                    if (destination == area_number || aas_reach_exists(work, area_number, destination) ||
                        (!aas_reach_grounded(work, destination) && !aas_reach_swimming(work, destination)) ||
                        (work->settings[destination].contents & 6)) break;
                    bool portal;
                    if (!crosses_portal(work, midpoint, end, &portal, error)) return false;
                    if (portal) break;
                    float distance = midpoint.z - trace.end.z;
                    if (work->movement.max_fall_height != 0 && fabsf(distance) > work->movement.max_fall_height) break;
                    bool found;
                    uint16_t time;
                    if (!ledge_time(work, destination, distance, false, &time, error)) return false;
                    if (!add(work, area_number, destination, edge_number, midpoint, trace.end,
                        AAS_REACH_LEDGE, time, &found, error)) return false;
                }
            }
        }
    }
    return true;
}
