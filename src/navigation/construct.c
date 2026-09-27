#include "internal.h"

typedef struct sample_key {
    int64_t x, y, z;
    bool used;
} sample_key;
typedef struct cell_node {
    int64_t x, y;
    uint32_t node;
} cell_node;
typedef struct construction {
    const qa_nav_construction *options;
    const qa_navigation_services *services;
    qa_nav_graph *graph;
    qa_nav_profile crouched;
    bool has_crouch;
    float spacing, link_distance;
    size_t maximum_nodes;
    sample_key *seen;
    size_t seen_capacity, seen_count;
} construction;

static bool coordinate(double value, int64_t *out, qa_error *e) {
    if (!isfinite(value) || value < INT64_MIN || value >= 0x1p63) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                     "Navigation sample coordinate is outside the integer grid");
        return false;
    }
    *out = (int64_t)value;
    return true;
}
static uint64_t mix(uint64_t x) {
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static size_t key_slot(const sample_key *keys, size_t capacity, sample_key key) {
    size_t i =
        (size_t)(mix((uint64_t)key.x) ^ mix((uint64_t)key.y + 1) ^ mix((uint64_t)key.z + 2)) &
        (capacity - 1);
    while (keys[i].used && (keys[i].x != key.x || keys[i].y != key.y || keys[i].z != key.z))
        i = (i + 1) & (capacity - 1);
    return i;
}
static bool grow_keys(construction *c, qa_error *e) {
    size_t capacity = c->seen_capacity == 0 ? 256 : c->seen_capacity * 2;
    if (capacity < c->seen_capacity || capacity > SIZE_MAX / sizeof(sample_key))
        goto memory;
    sample_key *keys = calloc(capacity, sizeof(*keys));
    if (keys == NULL)
        goto memory;
    for (size_t i = 0; i < c->seen_capacity; ++i)
        if (c->seen[i].used)
            keys[key_slot(keys, capacity, c->seen[i])] = c->seen[i];
    free(c->seen);
    c->seen = keys;
    c->seen_capacity = capacity;
    return true;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, 0, "Growing navigation sample index");
    return false;
}
static bool ground(construction *c, const qa_nav_profile *p, qa_vec3 point, qa_vec3 *out,
                   bool *found, qa_error *e) {
    float height = -p->shape.bounds.mins.z;
    qa_vec3 start = point, end = point;
    start.z += height + p->maximum_step + 2;
    end.z += height - p->maximum_step - 4;
    qa_trace_result trace;
    if (!nav_trace(c->services, p, (qa_actor_id){0}, start, end, true, &trace, e))
        return false;
    if (trace.start_solid) {
        start.z = point.z + height + 1;
        if (!nav_trace(c->services, p, (qa_actor_id){0}, start, end, true, &trace, e))
            return false;
    }
    *found = !trace.start_solid && !trace.all_solid && trace.fraction < 1 && trace.contact &&
             trace.contact_plane.normal.z >= p->minimum_floor_normal;
    if (*found)
        *out = trace.end;
    return true;
}
static bool insert(construction *c, qa_vec3 point, qa_nav_origin source, const qa_nav_profile *p,
                   qa_error *e) {
    sample_key key = {.used = true};
    if (!coordinate(floor((double)point.x / 8 + 0.5), &key.x, e) ||
        !coordinate(floor((double)point.y / 8 + 0.5), &key.y, e) ||
        !coordinate(floor((double)point.z / 4 + 0.5), &key.z, e))
        return false;
    if (c->seen_capacity == 0 || c->seen_count >= c->seen_capacity / 2)
        if (!grow_keys(c, e))
            return false;
    size_t slot = key_slot(c->seen, c->seen_capacity, key);
    if (c->seen[slot].used)
        return true;
    bool clear;
    uint32_t contents;
    if (!nav_clear(c->services, p, (qa_actor_id){0}, point, point, true, &clear, e))
        return false;
    if (!clear)
        return true;
    if (!nav_contents(c->services, &c->options->profile, (qa_actor_id){0}, point, true, &contents,
                      e))
        return false;
    if ((contents & (QA_NAV_SLIME | QA_NAV_LAVA)) != 0)
        return true;
    if (c->graph->view.node_count >= c->maximum_nodes) {
        qa_error_set(
            e, QA_ERROR_ARGUMENT, c->maximum_nodes,
            "Navigation construction exceeded its node limit; no partial graph was published");
        return false;
    }
    qa_nav_node node = {.id = (uint32_t)c->graph->view.node_count,
                        .origin = point,
                        .bounds = qa_bounds_translate(p->shape.bounds, point),
                        .radius = c->spacing / 2,
                        .contents = contents,
                        .presence = p == &c->options->profile ? 2 : 4,
                        .source_cluster = -1,
                        .source = source};
    if (!nav_graph_node(c->graph, &node, e))
        return false;
    c->seen[slot] = key;
    ++c->seen_count;
    return true;
}
static bool sample(construction *c, qa_vec3 point, qa_nav_origin source, qa_error *e) {
    qa_vec3 origin;
    bool found;
    if (!ground(c, &c->options->profile, point, &origin, &found, e))
        return false;
    if (found)
        return insert(c, origin, source, &c->options->profile, e);
    if (!c->has_crouch)
        return true;
    if (!ground(c, &c->crouched, point, &origin, &found, e))
        return false;
    return !found || insert(c, origin, source, &c->crouched, e);
}
static bool inside(qa_vec3 point, const qa_vec3 *polygon, size_t count) {
    int sign = 0;
    for (size_t i = 0; i < count; ++i) {
        qa_vec3 a = polygon[i], b = polygon[(i + 1) % count];
        float cross = (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);
        if (fabsf(cross) < 0.001f)
            continue;
        int side = cross < 0 ? -1 : 1;
        if (sign != 0 && sign != side)
            return false;
        sign = side;
    }
    return true;
}
static bool polygon(construction *c, const qa_vec3 *points, size_t count, qa_vec3 normal,
                    qa_nav_origin source, qa_error *e) {
    if (count < 3 || normal.z < c->options->profile.minimum_floor_normal)
        return true;
    qa_vec3 center = {0}, lo = points[0], hi = points[0];
    for (size_t i = 0; i < count; ++i) {
        center = qa_vec_add(center, qa_vec_scale(points[i], 1.0f / (float)count));
        lo.x = fminf(lo.x, points[i].x);
        lo.y = fminf(lo.y, points[i].y);
        hi.x = fmaxf(hi.x, points[i].x);
        hi.y = fmaxf(hi.y, points[i].y);
    }
    if (!sample(c, center, source, e))
        return false;
    for (size_t i = 0; i < count; ++i)
        if (!sample(c, nav_midpoint(center, points[i]), source, e))
            return false;
    int64_t x0, x1, y0, y1;
    if (!coordinate(ceil((double)lo.x / c->spacing), &x0, e) ||
        !coordinate(floor((double)hi.x / c->spacing), &x1, e) ||
        !coordinate(ceil((double)lo.y / c->spacing), &y0, e) ||
        !coordinate(floor((double)hi.y / c->spacing), &y1, e))
        return false;
    float distance = qa_vec_dot(points[0], normal);
    for (int64_t x = x0; x <= x1; ++x)
        for (int64_t y = y0; y <= y1; ++y) {
            qa_vec3 point =
                qa_v3((float)((double)x * c->spacing), (float)((double)y * c->spacing), 0);
            point.z = (distance - point.x * normal.x - point.y * normal.y) / normal.z;
            if (inside(point, points, count) && !sample(c, point, source, e))
                return false;
        }
    return true;
}
static qa_nav_origin source_for(uint32_t surface, uint32_t leaf) {
    return (qa_nav_origin){.kind = QA_NAV_ORIGIN_CONSTRUCTED,
                           .node = QA_NAV_NO_INDEX,
                           .link = QA_NAV_NO_INDEX,
                           .surface = surface,
                           .leaf = leaf};
}
static bool face_samples(construction *c, qa_error *e) {
    const qa_bsp_view *map = c->options->geometry;
    qa_bsp_model model;
    if (!qa_bsp_read_model(map, 0, &model, e))
        return false;
    qa_vec3 *points = NULL;
    size_t capacity = 0;
    bool ok = true;
    for (uint64_t i = model.faces.first; ok && i < (uint64_t)model.faces.first + model.faces.count;
         ++i) {
        qa_bsp_face face;
        qa_bsp_plane plane;
        if (!qa_bsp_read_face(map, (size_t)i, &face, e) ||
            !qa_bsp_read_plane(map, face.plane, &plane, e)) {
            ok = false;
            break;
        }
        qa_vec3 normal = qa_vec_scale(plane.normal, face.draw_flags != 0 ? -1 : 1);
        if (normal.z < c->options->profile.minimum_floor_normal)
            continue;
        if (face.edges.count > capacity) {
            if (face.edges.count > SIZE_MAX / sizeof(*points)) {
                ok = false;
                qa_error_set(e, QA_ERROR_MEMORY, i, "Navigation face is too large");
                break;
            }
            qa_vec3 *p = realloc(points, (size_t)face.edges.count * sizeof(*p));
            if (p == NULL) {
                ok = false;
                qa_error_set(e, QA_ERROR_MEMORY, i, "Allocating navigation face samples");
                break;
            }
            points = p;
            capacity = face.edges.count;
        }
        for (uint32_t j = 0; j < face.edges.count; ++j) {
            int64_t index;
            qa_bsp_edge edge;
            qa_bsp_vertex vertex;
            if (!qa_bsp_read_index(map, QA_BSP_SURFEDGES, (size_t)face.edges.first + j, &index,
                                   e) ||
                !qa_bsp_read_edge(map, (size_t)(index < 0 ? -index : index), &edge, e) ||
                !qa_bsp_read_vertex(map, edge.vertices[index < 0 ? 1 : 0], &vertex, e)) {
                ok = false;
                break;
            }
            points[j] = vertex.position;
        }
        if (ok)
            ok = polygon(c, points, face.edges.count, normal,
                         source_for((uint32_t)i, QA_NAV_NO_INDEX), e);
    }
    free(points);
    return ok;
}
static bool surface_polygon(construction *c, const uint32_t *indices, size_t count,
                            qa_nav_origin source, qa_error *e) {
    qa_vec3 points[4], authored = {0};
    for (size_t i = 0; i < count; ++i) {
        qa_bsp_vertex vertex;
        if (!qa_bsp_read_vertex(c->options->geometry, indices[i], &vertex, e))
            return false;
        points[i] = vertex.position;
        if (i == 0)
            authored = vertex.normal;
    }
    qa_vec3 normal =
        qa_vec_cross(qa_vec_sub(points[1], points[0]), qa_vec_sub(points[2], points[0]));
    float length = qa_vec_length(normal);
    if (length == 0)
        return true;
    normal = qa_vec_scale(normal, 1 / length);
    if (qa_vec_dot(normal, authored) < 0)
        normal = qa_vec_scale(normal, -1);
    return polygon(c, points, count, normal, source, e);
}
static bool surface_samples(construction *c, qa_error *e) {
    const qa_bsp_view *map = c->options->geometry;
    size_t count = qa_bsp_record_count(map, QA_BSP_SURFACES);
    if (count > SIZE_MAX / sizeof(uint32_t)) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Navigation world surface count is too large");
        return false;
    }
    uint32_t *members = malloc((count == 0 ? 1 : count) * sizeof(*members));
    if (members == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Reading navigation world surfaces");
        return false;
    }
    bool ok = qa_bsp_model_members(map, 0, false, members, count, &count, e);
    for (size_t i = 0; ok && i < count; ++i) {
        qa_bsp_surface surface;
        if (!qa_bsp_read_surface(map, members[i], &surface, e)) {
            ok = false;
            break;
        }
        qa_nav_origin source = source_for(members[i], QA_NAV_NO_INDEX);
        if (surface.type == QA_BSP_SURFACE_FLARE)
            continue;
        if (surface.type == QA_BSP_SURFACE_PATCH) {
            for (int32_t row = 0; ok && row + 1 < surface.patch_height; ++row)
                for (int32_t col = 0; ok && col + 1 < surface.patch_width; ++col) {
                    uint64_t offset =
                        (uint64_t)(uint32_t)row * (uint32_t)surface.patch_width + (uint32_t)col;
                    if (offset + (uint32_t)surface.patch_width + 1 >= surface.vertices.count ||
                        offset + surface.vertices.first > UINT32_MAX) {
                        qa_error_set(e, QA_ERROR_FORMAT, members[i],
                                     "Invalid navigation patch control grid");
                        ok = false;
                        break;
                    }
                    uint32_t first = surface.vertices.first + (uint32_t)offset,
                             width = (uint32_t)surface.patch_width;
                    uint32_t indices[4] = {first, first + 1, first + width + 1, first + width};
                    ok = surface_polygon(c, indices, 4, source, e);
                }
        } else {
            size_t triangles = qa_bsp_surface_triangle_count(&surface);
            for (size_t j = 0; ok && j < triangles; ++j) {
                uint32_t indices[3];
                ok = qa_bsp_surface_triangle(map, &surface, j, indices, e) &&
                     surface_polygon(c, indices, 3, source, e);
            }
        }
    }
    free(members);
    return ok;
}
static int cell_compare(const void *a, const void *b) {
    const cell_node *x = a, *y = b;
    if (x->x != y->x)
        return x->x < y->x ? -1 : 1;
    if (x->y != y->y)
        return x->y < y->y ? -1 : 1;
    return x->node < y->node ? -1 : x->node > y->node;
}
static size_t cell_start(const cell_node *cells, size_t count, int64_t x, int64_t y) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const cell_node *c = cells + mid;
        if (c->x < x || (c->x == x && c->y < y))
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}
static bool link_pair(construction *c, const qa_nav_node *from, const qa_nav_node *to,
                      qa_error *e) {
    const qa_nav_profile *p = &c->options->profile;
    qa_vec3 delta = qa_vec_sub(to->origin, from->origin);
    if (hypotf(delta.x, delta.y) > c->link_distance ||
        fabsf(delta.z) > fmaxf(p->maximum_drop, c->link_distance))
        return true;
    qa_nav_travel mode = (from->contents & to->contents & QA_NAV_WATER) != 0 ? QA_NAV_SWIM
                         : (from->contents & to->contents & QA_NAV_CONTENTS_LADDER) != 0
                             ? QA_NAV_LADDER
                         : delta.z > p->maximum_step                ? QA_NAV_JUMP
                         : delta.z < -p->maximum_step               ? QA_NAV_DROP
                         : from->presence == 4 || to->presence == 4 ? QA_NAV_CROUCH
                                                                    : QA_NAV_WALK;
    if ((p->capabilities & QA_NAV_CAPABILITY(mode)) == 0 ||
        (mode == QA_NAV_DROP && -delta.z > p->maximum_drop))
        return true;
    bool clear;
    if (mode == QA_NAV_WALK || mode == QA_NAV_CROUCH) {
        const qa_nav_profile *posture = mode == QA_NAV_CROUCH && c->has_crouch ? &c->crouched : p;
        if (!nav_clear(c->services, posture, (qa_actor_id){0}, from->origin, to->origin, true,
                       &clear, e))
            return false;
        if (!clear) {
            qa_vec3 rise = qa_v3(0, 0, p->maximum_step + 1);
            if (!nav_clear(c->services, posture, (qa_actor_id){0}, qa_vec_add(from->origin, rise),
                           qa_vec_add(to->origin, rise), true, &clear, e))
                return false;
            if (!clear)
                return true;
        }
        qa_vec3 foot = nav_midpoint(from->origin, to->origin), grounded;
        bool found;
        foot.z += p->shape.bounds.mins.z;
        if (!ground(c, posture, foot, &grounded, &found, e))
            return false;
        if (!found)
            return true;
    } else if (mode == QA_NAV_SWIM) {
        if (!nav_clear(c->services, p, (qa_actor_id){0}, from->origin, to->origin, true, &clear, e))
            return false;
        if (!clear)
            return true;
    }
    qa_nav_edge edge = {.id = (uint32_t)c->graph->view.edge_count,
                        .from = from->id,
                        .to = to->id,
                        .mode = mode,
                        .start = from->origin,
                        .end = to->origin,
                        .travel_seconds = fmaxf(0.01f, qa_vec_length(delta) / 320),
                        .source = from->source};
    return nav_graph_edge(c->graph, &edge, e);
}
static bool link_nodes(construction *c, qa_error *e) {
    size_t count = c->graph->view.node_count;
    if (count > SIZE_MAX / sizeof(cell_node)) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Navigation cell index is too large");
        return false;
    }
    cell_node *cells = malloc((count == 0 ? 1 : count) * sizeof(*cells));
    if (cells == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating navigation cell index");
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        qa_vec3 point = c->graph->nodes[i].origin;
        cells[i].node = (uint32_t)i;
        ok = coordinate(floor((double)point.x / c->link_distance), &cells[i].x, e) &&
             coordinate(floor((double)point.y / c->link_distance), &cells[i].y, e);
        if (ok && (cells[i].x == INT64_MIN || cells[i].x == INT64_MAX || cells[i].y == INT64_MIN ||
                   cells[i].y == INT64_MAX)) {
            qa_error_set(e, QA_ERROR_ARGUMENT, i,
                         "Navigation cell neighbors exceed the integer grid");
            ok = false;
        }
    }
    if (ok)
        qsort(cells, count, sizeof(*cells), cell_compare);
    for (size_t i = 0; ok && i < count; ++i) {
        const qa_nav_node *node = c->graph->nodes + i;
        int64_t cx = (int64_t)floor((double)node->origin.x / c->link_distance),
                cy = (int64_t)floor((double)node->origin.y / c->link_distance);
        for (int dx = -1; ok && dx <= 1; ++dx)
            for (int dy = -1; ok && dy <= 1; ++dy) {
                int64_t x = cx + dx, y = cy + dy;
                size_t start = cell_start(cells, count, x, y);
                for (size_t j = start; ok && j < count && cells[j].x == x && cells[j].y == y; ++j)
                    if (cells[j].node != i)
                        ok = link_pair(c, node, c->graph->nodes + cells[j].node, e);
            }
    }
    free(cells);
    return ok;
}
static const qa_nav_node *nearest(const construction *c, qa_vec3 point) {
    float best = c->link_distance * 2;
    const qa_nav_node *found = NULL;
    for (size_t i = 0; i < c->graph->view.node_count; ++i) {
        const qa_nav_node *node = c->graph->nodes + i;
        float distance = nav_distance(node->origin, point);
        if (distance < best) {
            best = distance;
            found = node;
        }
    }
    return found;
}
static bool connections(construction *c, qa_error *e) {
    size_t count = c->options->connection_count;
    if (count > SIZE_MAX / sizeof(qa_nav_rejection)) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Too many navigation connections");
        return false;
    }
    if (count != 0) {
        c->graph->rejected = malloc(count * sizeof(*c->graph->rejected));
        if (c->graph->rejected == NULL) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating rejected connection diagnostics");
            return false;
        }
        c->graph->view.rejected = c->graph->rejected;
    }
    for (size_t i = 0; i < count; ++i) {
        const qa_nav_connection *connection = c->options->connections + i;
        if (!qa_vec_finite(connection->from) || !qa_vec_finite(connection->to) ||
            !isfinite(connection->travel_seconds) || connection->travel_seconds < 0 ||
            (unsigned)connection->mode >= QA_NAV_TRAVEL_COUNT) {
            qa_error_set(e, QA_ERROR_ARGUMENT, i, "Invalid authored navigation connection");
            return false;
        }
        const qa_nav_node *from = nearest(c, connection->from), *to = nearest(c, connection->to);
        if (from == NULL || to == NULL) {
            c->graph->rejected[c->graph->view.rejected_count++] =
                (qa_nav_rejection){connection->id, from == NULL, to == NULL};
            continue;
        }
        qa_nav_edge edge = {.id = (uint32_t)c->graph->view.edge_count,
                            .from = from->id,
                            .to = to->id,
                            .mode = connection->mode,
                            .start = connection->from,
                            .end = connection->to,
                            .travel_seconds = connection->travel_seconds,
                            .source_travel_type = connection->source_travel_type,
                            .has_hint = connection->has_hint,
                            .hint = connection->hint,
                            .has_entity = connection->has_entity,
                            .entity = connection->entity,
                            .source = source_for(QA_NAV_NO_INDEX, QA_NAV_NO_INDEX)};
        if (!edge.has_hint && edge.mode == QA_NAV_MOVER) {
            edge.has_hint = true;
            edge.hint = (qa_nav_hint){
                .funnel = from->origin, .start = connection->from, .end = connection->to};
        }
        if (!nav_graph_edge(c->graph, &edge, e))
            return false;
    }
    return true;
}
bool qa_nav_graph_construct(const qa_nav_construction *options,
                            const qa_navigation_services *services, qa_nav_graph **out,
                            qa_error *e) {
    if (options == NULL || out == NULL || options->geometry == NULL ||
        (options->connection_count != 0 && options->connections == NULL)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing navigation construction input");
        return false;
    }
    construction c = {.options = options,
                      .services = services,
                      .spacing = options->spacing == 0 ? 48 : options->spacing,
                      .maximum_nodes =
                          options->maximum_nodes == 0 ? 100000 : options->maximum_nodes};
    c.link_distance = options->link_distance == 0 ? c.spacing * 2.1f : options->link_distance;
    if (options->map.format != options->geometry->format || !isfinite(c.spacing) || c.spacing < 8 ||
        !isfinite(c.link_distance) || c.link_distance < c.spacing || c.maximum_nodes > UINT32_MAX) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                     "Invalid navigation construction limits or map identity");
        return false;
    }
    if (!nav_services_valid(services, false, e) ||
        !nav_graph_new(&options->map, &options->profile, &c.graph, e))
        return false;
    c.has_crouch = nav_crouch_profile(&options->profile, &c.crouched);
    bool ok = options->geometry->family == QA_BSP_Q3 ? surface_samples(&c, e) : face_samples(&c, e);
    size_t leaves = qa_bsp_record_count(options->geometry, QA_BSP_LEAVES);
    for (size_t i = 0; ok && i < leaves; ++i) {
        qa_bsp_leaf leaf;
        uint32_t medium;
        if (!qa_bsp_read_leaf(options->geometry, i, &leaf, e)) {
            ok = false;
            break;
        }
        qa_vec3 center = nav_midpoint(leaf.bounds.min, leaf.bounds.max);
        if (!nav_contents(services, &options->profile, (qa_actor_id){0}, center, true, &medium,
                          e)) {
            ok = false;
            break;
        }
        if ((medium & (QA_NAV_WATER | QA_NAV_CONTENTS_LADDER)) != 0 &&
            (medium & (QA_NAV_SLIME | QA_NAV_LAVA)) == 0)
            ok = insert(&c, center, source_for(QA_NAV_NO_INDEX, (uint32_t)i), &options->profile, e);
    }
    if (ok)
        ok = link_nodes(&c, e) && connections(&c, e) && nav_graph_finish(c.graph, e);
    free(c.seen);
    if (!ok) {
        qa_nav_graph_release(c.graph);
        return false;
    }
    *out = c.graph;
    return true;
}
