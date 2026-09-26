#include "patch.h"

#include <stdlib.h>
#include <string.h>

/* Q3 tr_curve.c, tr_bsp.c and tr_surface.c. Temporary grids are bounded by
 * MAX_GRID_SIZE; final vertices belong to the containing world surface. */
typedef struct patch_work {
    qa_scene_vertex points[QAW_PATCH_LIMIT][QAW_PATCH_LIMIT];
    qa_scene_vertex transposed[QAW_PATCH_LIMIT][QAW_PATCH_LIMIT];
} patch_work;

static qa_scene_vertex midpoint(qa_scene_vertex a, qa_scene_vertex b) {
    qa_scene_vertex p;
    p.position = qa_vec_scale(qa_vec_add(a.position, b.position), 0.5f);
    p.normal = qa_vec_normalize(qa_vec_add(a.normal, b.normal));
    p.texcoord = (qa_scene_vec2){(a.texcoord.x + b.texcoord.x) * 0.5f,
                               (a.texcoord.y + b.texcoord.y) * 0.5f};
    p.lightmap = (qa_scene_vec2){(a.lightmap.x + b.lightmap.x) * 0.5f,
                               (a.lightmap.y + b.lightmap.y) * 0.5f};
    p.color = (qa_scene_vec4){floorf((roundf(a.color.x * 255) + roundf(b.color.x * 255)) * 0.5f) / 255,
                            floorf((roundf(a.color.y * 255) + roundf(b.color.y * 255)) * 0.5f) / 255,
                            floorf((roundf(a.color.z * 255) + roundf(b.color.z * 255)) * 0.5f) / 255,
                            floorf((roundf(a.color.w * 255) + roundf(b.color.w * 255)) * 0.5f) / 255};
    return p;
}

static void transpose(patch_work *work, unsigned *width, unsigned *height) {
    for (unsigned y = 0; y < *height; ++y)
        for (unsigned x = 0; x < *width; ++x)
            work->transposed[x][y] = work->points[y][x];
    unsigned swap = *width; *width = *height; *height = swap;
    for (unsigned y = 0; y < *height; ++y)
        memcpy(work->points[y], work->transposed[y], *width * sizeof(qa_scene_vertex));
}

static bool matches(qa_vec3 a, qa_vec3 b) {
    return fabsf(a.x - b.x) <= 0.1f && fabsf(a.y - b.y) <= 0.1f && fabsf(a.z - b.z) <= 0.1f;
}

static void normals(qa_scene_vertex *vertices, unsigned width, unsigned height) {
    static const int neighbors[8][2] = {{0,1},{1,1},{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1}};
    bool wrap_width = true, wrap_height = true;
    for (unsigned y = 0; y < height; ++y) {
        qa_vec3 delta = qa_vec_sub(vertices[y * width].position, vertices[y * width + width - 1].position);
        if (qa_vec_dot(delta, delta) > 1) wrap_width = false;
    }
    for (unsigned x = 0; x < width; ++x) {
        qa_vec3 delta = qa_vec_sub(vertices[x].position, vertices[(height - 1) * width + x].position);
        if (qa_vec_dot(delta, delta) > 1) wrap_height = false;
    }
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        qa_vec3 around[8] = {{0}};
        bool found[8] = {false};
        qa_vec3 point = vertices[y * width + x].position;
        for (unsigned k = 0; k < 8; ++k) for (int step = 1; step <= 3; ++step) {
            int nx = (int)x + neighbors[k][0] * step, ny = (int)y + neighbors[k][1] * step;
            if (wrap_width) {
                if (nx < 0) nx += (int)width - 1;
                else if (nx >= (int)width) nx += 1 - (int)width;
            }
            if (wrap_height) {
                if (ny < 0) ny += (int)height - 1;
                else if (ny >= (int)height) ny += 1 - (int)height;
            }
            if (nx < 0 || ny < 0 || nx >= (int)width || ny >= (int)height) break;
            qa_vec3 delta = qa_vec_sub(vertices[(unsigned)ny * width + (unsigned)nx].position, point);
            if (qa_vec_dot(delta, delta) == 0) continue;
            around[k] = qa_vec_normalize(delta); found[k] = true; break;
        }
        qa_vec3 normal = {0};
        for (unsigned k = 0; k < 8; ++k) if (found[k] && found[(k + 1) & 7])
            normal = qa_vec_add(normal, qa_vec_normalize(qa_vec_cross(around[(k + 1) & 7], around[k])));
        vertices[y * width + x].normal = qa_vec_normalize(normal);
    }
}

static bool install(qaw_surface *surface, patch_work *work, unsigned width, unsigned height, qa_error *error) {
    size_t vertex_count = (size_t)width * height, index_count = (size_t)(width - 1) * (height - 1) * 6;
    qa_scene_vertex *vertices = malloc(vertex_count * sizeof(*vertices));
    uint32_t *indices = malloc(index_count * sizeof(*indices));
    if (!vertices || !indices) {
        free(vertices); free(indices);
        qa_error_set(error, QA_ERROR_MEMORY, surface->source_index, "allocating Q3 patch mesh");
        return false;
    }
    for (unsigned y = 0; y < height; ++y)
        memcpy(vertices + y * width, work->points[y], width * sizeof(*vertices));
    normals(vertices, width, height);
    size_t cursor = 0;
    for (unsigned y = 0; y + 1 < height; ++y) for (unsigned x = 0; x + 1 < width; ++x) {
        uint32_t a = y * width + x, b = a + width;
        indices[cursor++] = a; indices[cursor++] = b; indices[cursor++] = a + 1;
        indices[cursor++] = a + 1; indices[cursor++] = b; indices[cursor++] = b + 1;
    }
    free(surface->vertices); free(surface->indices);
    surface->vertices = vertices; surface->indices = indices;
    surface->mesh.vertices = vertices; surface->mesh.indices = indices;
    surface->mesh.vertex_count = vertex_count; surface->mesh.index_count = index_count;
    ++surface->mesh.revision;
    surface->patch->width = width; surface->patch->height = height;
    qaw_mesh_bounds(surface);
    return true;
}

bool qaw_patch_build(qaw_surface *surface, const qa_bsp_surface *source, float subdivisions, qa_error *error) {
    if (source->patch_width < 3 || source->patch_width > QAW_PATCH_LIMIT ||
        source->patch_height < 3 || source->patch_height > QAW_PATCH_LIMIT ||
        !(source->patch_width & 1) || !(source->patch_height & 1) ||
        (size_t)source->patch_width * (size_t)source->patch_height > 1024 ||
        (size_t)source->patch_width * (size_t)source->patch_height != surface->mesh.vertex_count ||
        !isfinite(subdivisions)) {
        qa_error_set(error, QA_ERROR_FORMAT, surface->source_index, "invalid Q3 quadratic patch dimensions");
        return false;
    }
    qaw_patch *patch = calloc(1, sizeof(*patch));
    patch_work *work = malloc(sizeof(*work));
    if (!patch || !work) {
        free(patch); free(work);
        qa_error_set(error, QA_ERROR_MEMORY, surface->source_index, "allocating Q3 patch subdivision");
        return false;
    }
    surface->patch = patch;
    unsigned width = (unsigned)source->patch_width, height = (unsigned)source->patch_height;
    for (unsigned y = 0; y < height; ++y)
        memcpy(work->points[y], surface->vertices + y * width, width * sizeof(qa_scene_vertex));
    for (unsigned direction = 0; direction < 2; ++direction) {
        float *errors = direction == 0 ? patch->width_error : patch->height_error;
        for (int x = 0; x + 2 < (int)width; x += 2) {
            float maximum = 0;
            for (unsigned y = 0; y < height; ++y) {
                qa_vec3 a = work->points[y][x].position, b = work->points[y][x + 1].position;
                qa_vec3 c = work->points[y][x + 2].position;
                qa_vec3 curve = qa_vec_sub(qa_vec_scale(qa_vec_add(qa_vec_add(a, qa_vec_scale(b, 2)), c), 0.25f), a);
                qa_vec3 line = qa_vec_normalize(qa_vec_sub(c, a));
                qa_vec3 distance = qa_vec_sub(curve, qa_vec_scale(line, qa_vec_dot(curve, line)));
                maximum = fmaxf(maximum, qa_vec_dot(distance, distance));
            }
            maximum = sqrtf(maximum);
            if (maximum < 0.1f) { errors[x + 1] = 999; continue; }
            if (width + 2 > QAW_PATCH_LIMIT || maximum <= subdivisions) {
                errors[x + 1] = 1 / maximum; continue;
            }
            errors[x + 2] = 1 / maximum;
            for (unsigned y = 0; y < height; ++y) {
                qa_scene_vertex a = midpoint(work->points[y][x], work->points[y][x + 1]);
                qa_scene_vertex b = midpoint(work->points[y][x + 1], work->points[y][x + 2]);
                memmove(work->points[y] + x + 4, work->points[y] + x + 2,
                        (width - (unsigned)x - 2) * sizeof(qa_scene_vertex));
                work->points[y][x + 1] = a;
                work->points[y][x + 2] = midpoint(a, b);
                work->points[y][x + 3] = b;
            }
            width += 2; x -= 2;
        }
        transpose(work, &width, &height);
    }
    for (unsigned x = 0; x < width; ++x) for (unsigned y = 1; y + 1 < height; y += 2) {
        qa_scene_vertex p = work->points[y][x];
        work->points[y][x] = midpoint(midpoint(p, work->points[y + 1][x]), midpoint(p, work->points[y - 1][x]));
    }
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 1; x + 1 < width; x += 2) {
        qa_scene_vertex p = work->points[y][x];
        work->points[y][x] = midpoint(midpoint(p, work->points[y][x + 1]), midpoint(p, work->points[y][x - 1]));
    }
    for (unsigned x = 1; x + 1 < width; ++x) if (patch->width_error[x] == 999) {
        for (unsigned y = 0; y < height; ++y)
            memmove(work->points[y] + x, work->points[y] + x + 1, (width - x - 1) * sizeof(qa_scene_vertex));
        memmove(patch->width_error + x, patch->width_error + x + 1, (width - x - 1) * sizeof(float));
        --width;
    }
    for (unsigned y = 1; y + 1 < height; ++y) if (patch->height_error[y] == 999) {
        memmove(work->points[y], work->points[y + 1], (height - y - 1) * sizeof(work->points[0]));
        memmove(patch->height_error + y, patch->height_error + y + 1, (height - y - 1) * sizeof(float));
        --height;
    }
    if (height > width) {
        float old_width[QAW_PATCH_LIMIT];
        memcpy(old_width, patch->width_error, sizeof(old_width));
        transpose(work, &width, &height);
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width / 2; ++x) {
            qa_scene_vertex swap = work->points[y][x];
            work->points[y][x] = work->points[y][width - 1 - x];
            work->points[y][width - 1 - x] = swap;
        }
        for (unsigned x = 0; x < width; ++x) patch->width_error[x] = patch->height_error[width - 1 - x];
        memcpy(patch->height_error, old_width, height * sizeof(float));
    }
    patch->lod_origin = qa_vec_scale(qa_vec_add(source->lightmap_vectors[0], source->lightmap_vectors[1]), 0.5f);
    patch->lod_radius = qa_vec_length(qa_vec_sub(source->lightmap_vectors[0], patch->lod_origin));
    bool ok = install(surface, work, width, height, error);
    free(work);
    return ok;
}

typedef struct patch_edge { unsigned count, offset, stride, boundary; bool vertical; float *errors; } patch_edge;

static patch_edge edge(qaw_surface *surface, unsigned index) {
    qaw_patch *p = surface->patch;
    if (index < 2) return (patch_edge){p->width, index ? (p->height - 1) * p->width : 0, 1,
                                     index ? p->height - 1 : 0, false, p->width_error};
    return (patch_edge){p->height, index == 3 ? p->width - 1 : 0, p->width,
                        index == 3 ? p->width - 1 : 0, true, p->height_error};
}

static qa_vec3 point(const qaw_surface *surface, patch_edge e, unsigned index) {
    return surface->vertices[e.offset + index * e.stride].position;
}

static bool merged(const qaw_surface *surface, patch_edge e) {
    for (unsigned i = 1; i + 1 < e.count; ++i)
        for (unsigned j = i + 1; j + 1 < e.count; ++j)
            if (matches(point(surface, e, i), point(surface, e, j))) return true;
    return false;
}

static bool same_group(const qaw_patch *a, const qaw_patch *b) {
    return a->lod_radius == b->lod_radius && a->lod_origin.x == b->lod_origin.x &&
           a->lod_origin.y == b->lod_origin.y && a->lod_origin.z == b->lod_origin.z;
}

static bool insert(qaw_surface *target, patch_edge e, unsigned index, qa_vec3 anchor,
                   float lod_error, qa_error *error) {
    patch_work *work = malloc(sizeof(*work));
    if (!work) { qa_error_set(error, QA_ERROR_MEMORY, target->source_index, "allocating Q3 patch stitch"); return false; }
    qaw_patch *p = target->patch;
    unsigned width = p->width + !e.vertical, height = p->height + e.vertical;
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        unsigned coordinate = e.vertical ? y : x;
        if (coordinate != index) {
            unsigned old_x = x - (!e.vertical && x > index), old_y = y - (e.vertical && y > index);
            work->points[y][x] = target->vertices[old_y * p->width + old_x];
        } else {
            unsigned next = y * p->width + x, previous = next - (e.vertical ? p->width : 1);
            work->points[y][x] = midpoint(target->vertices[previous], target->vertices[next]);
            if ((e.vertical ? x : y) == e.boundary) work->points[y][x].position = anchor;
        }
    }
    bool ok = install(target, work, width, height, error);
    free(work);
    if (!ok) return false;
    memmove(e.errors + index + 1, e.errors + index, (e.count - index) * sizeof(float));
    e.errors[index] = lod_error;
    p->stitched = p->fixed = false;
    return true;
}

static bool stitch(qaw_surface *source, qaw_surface *target, bool *changed, qa_error *error) {
    *changed = false;
    for (unsigned reversed = 0; reversed < 2; ++reversed) for (unsigned si = 0; si < 4; ++si) {
        patch_edge a = edge(source, si);
        if (merged(source, a)) continue;
        for (int k = reversed ? (int)a.count - 1 : 0;
             reversed ? k > 1 : k + 2 < (int)a.count; k += reversed ? -2 : 2) {
            unsigned next = (unsigned)(k + (reversed ? -2 : 2)), middle = (unsigned)(k + (reversed ? -1 : 1));
            for (unsigned ti = 0; ti < 4; ++ti) {
                patch_edge b = edge(target, ti);
                if (b.count >= QAW_PATCH_LIMIT) continue;
                for (unsigned j = 0; j + 1 < b.count; ++j) {
                    qa_vec3 first = point(target, b, j), last = point(target, b, j + 1);
                    if (!matches(point(source, a, (unsigned)k), first) || !matches(point(source, a, next), last)) continue;
                    if (fabsf(first.x - last.x) < 0.01f && fabsf(first.y - last.y) < 0.01f && fabsf(first.z - last.z) < 0.01f) continue;
                    unsigned error_index = reversed && (unsigned)k + 1 == a.count ? middle : (unsigned)k + 1;
                    qa_vec3 anchor = point(source, a, middle);
                    float lod_error = a.errors[error_index];
                    if (!insert(target, b, j + 1, anchor, lod_error, error)) return false;
                    *changed = true; return true;
                }
            }
        }
    }
    return true;
}

static bool synchronize(qaw_surface *source, qaw_surface *target) {
    bool touched = false;
    for (unsigned si = 0; si < 4; ++si) {
        patch_edge a = edge(source, si);
        if (merged(source, a)) continue;
        for (unsigned k = 1; k + 1 < a.count; ++k) for (unsigned ti = 0; ti < 4; ++ti) {
            patch_edge b = edge(target, ti);
            if (merged(target, b)) continue;
            for (unsigned j = 1; j + 1 < b.count; ++j) if (matches(point(source, a, k), point(target, b, j))) {
                b.errors[j] = a.errors[k]; touched = true;
            }
        }
    }
    return touched;
}

bool qaw_patch_prepare(qa_scene_world *world, qa_error *error) {
    bool visited;
    do {
        visited = false;
        for (size_t i = 0; i < world->surface_count; ++i) {
            qaw_surface *source = world->surfaces + i;
            if (!source->patch || source->patch->stitched) continue;
            source->patch->stitched = true; visited = true;
            for (size_t j = 0; j < world->surface_count; ++j) {
                qaw_surface *target = world->surfaces + j;
                if (!target->patch || !same_group(source->patch, target->patch)) continue;
                bool changed;
                do { if (!stitch(source, target, &changed, error)) return false; } while (changed);
            }
        }
    } while (visited);
    typedef struct sync_entry { size_t source, next; } sync_entry;
    if (world->surface_count > SIZE_MAX / sizeof(sync_entry)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Q3 patch LOD traversal exceeds address space"); return false;
    }
    sync_entry *stack = world->surface_count ? malloc(world->surface_count * sizeof(*stack)) : NULL;
    if (world->surface_count && !stack) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 patch LOD traversal"); return false;
    }
    for (size_t i = 0; i < world->surface_count; ++i) {
        if (!world->surfaces[i].patch || world->surfaces[i].patch->fixed) continue;
        world->surfaces[i].patch->fixed = true;
        size_t depth = 1; stack[0] = (sync_entry){i, i + 1};
        while (depth) {
            sync_entry *entry = stack + depth - 1;
            if (entry->next == world->surface_count) { --depth; continue; }
            qaw_surface *source = world->surfaces + entry->source;
            size_t index = entry->next++;
            qaw_surface *target = world->surfaces + index;
            if (!target->patch || target->patch->fixed || !same_group(source->patch, target->patch)) continue;
            if (synchronize(source, target)) {
                target->patch->fixed = true;
                stack[depth++] = (sync_entry){index, i + 1};
            }
        }
    }
    free(stack); return true;
}

bool qaw_patch_lod(const qaw_surface *surface, const qa_material_context *context, float curve_error,
                   qa_scene_frame *frame, qa_scene_mesh *mesh, qa_error *error) {
    const qaw_patch *p = surface->patch;
    qa_scene_vec4 transformed = qa_scene_matrix_point(context->model, p->lod_origin);
    qa_vec3 origin = qa_v3(transformed.x, transformed.y, transformed.z);
    float distance = fabsf(qa_vec_dot(qa_vec_sub(origin, context->view.origin), context->view.axis[0])) - p->lod_radius;
    float threshold = curve_error < 0 ? 0 : curve_error / fmaxf(1, distance);
    unsigned columns[QAW_PATCH_LIMIT], rows[QAW_PATCH_LIMIT], column_count = 1, row_count = 1;
    columns[0] = rows[0] = 0;
    for (unsigned x = 1; x + 1 < p->width; ++x) if (p->width_error[x] <= threshold) columns[column_count++] = x;
    for (unsigned y = 1; y + 1 < p->height; ++y) if (p->height_error[y] <= threshold) rows[row_count++] = y;
    columns[column_count++] = p->width - 1; rows[row_count++] = p->height - 1;
    *mesh = surface->mesh;
    if (column_count == p->width && row_count == p->height) return true;
    size_t count = (size_t)(column_count - 1) * (row_count - 1) * 6;
    size_t vertex_count = (size_t)column_count * row_count;
    qa_scene_vertex *vertices = qa_arena_alloc(&frame->storage, vertex_count * sizeof(*vertices),
                                              _Alignof(qa_scene_vertex), error);
    if (!vertices) return false;
    for (unsigned y = 0; y < row_count; ++y) for (unsigned x = 0; x < column_count; ++x)
        vertices[y * column_count + x] = surface->vertices[rows[y] * p->width + columns[x]];
    uint32_t *indices = qa_arena_alloc(&frame->storage, count * sizeof(*indices), _Alignof(uint32_t), error);
    if (!indices) return false;
    size_t cursor = 0;
    for (unsigned y = 0; y + 1 < row_count; ++y) for (unsigned x = 0; x + 1 < column_count; ++x) {
        uint32_t a = y * column_count + x, b = a + column_count;
        uint32_t c = a + 1, d = b + 1;
        indices[cursor++] = a; indices[cursor++] = b; indices[cursor++] = c;
        indices[cursor++] = c; indices[cursor++] = b; indices[cursor++] = d;
    }
    mesh->identity = mesh->revision = 0;
    mesh->vertices = vertices; mesh->vertex_count = vertex_count;
    mesh->indices = indices; mesh->index_count = count;
    return true;
}
