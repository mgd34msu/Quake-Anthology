/* Quake III patch collision, derived from cm_patch.c and cm_polylib.c.
 * Copyright (C) 1999-2005 Id Software, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "patch.h"

#include <stdlib.h>

enum {
    PATCH_GRID_SIZE = 129,
    PATCH_MAX_PLANES = 2048,
    PATCH_MAX_FACETS = 1024,
    PATCH_MAX_BORDERS = 27,
    PATCH_MAX_WINDING = 64
};

typedef struct patch_plane {
    qa_vec3 normal;
    float distance;
    uint8_t signbits;
} patch_plane;

typedef struct patch_border {
    int32_t plane;
    bool inward, no_adjust;
} patch_border;

typedef struct patch_facet {
    int32_t surface;
    unsigned border_count;
    patch_border borders[PATCH_MAX_BORDERS];
} patch_facet;

struct qa_q3_patch {
    qa_bounds bounds;
    size_t plane_count, facet_count;
    patch_plane *planes;
    patch_facet *facets;
};

typedef struct patch_winding {
    unsigned count;
    qa_vec3 points[PATCH_MAX_WINDING];
} patch_winding;

typedef struct patch_builder {
    unsigned width, height;
    qa_vec3 grid[PATCH_GRID_SIZE][PATCH_GRID_SIZE];
    int32_t grid_planes[PATCH_GRID_SIZE - 1][PATCH_GRID_SIZE - 1][2];
    patch_plane planes[PATCH_MAX_PLANES];
    patch_facet facets[PATCH_MAX_FACETS];
    size_t plane_count, facet_count;
    qa_error *error;
    bool failed;
} patch_builder;

/* Generation keeps full intermediates until storing a vector or distance.
 * Trace dot products use the shared native float operations. */
static double patch_dot(qa_vec3 a, qa_vec3 b) {
    return (double)a.x*b.x + (double)a.y*b.y + (double)a.z*b.z;
}

static qa_vec3 patch_cross(qa_vec3 a, qa_vec3 b) {
    return qa_v3((float)((double)a.y*b.z - (double)a.z*b.y),
                 (float)((double)a.z*b.x - (double)a.x*b.z),
                 (float)((double)a.x*b.y - (double)a.y*b.x));
}

static qa_vec3 patch_normalize(qa_vec3 v) {
    double length = sqrt(patch_dot(v, v));
    if (length == 0.0) return v;
    double inverse = 1.0 / length;
    return qa_v3((float)(v.x*inverse), (float)(v.y*inverse), (float)(v.z*inverse));
}

static uint8_t patch_signbits(qa_vec3 n) {
    return (uint8_t)((n.x < 0.0f ? 1u : 0u) |
                     (n.y < 0.0f ? 2u : 0u) |
                     (n.z < 0.0f ? 4u : 0u));
}

static patch_plane make_plane(qa_vec3 n, float distance) {
    return (patch_plane){n, distance, patch_signbits(n)};
}

static patch_plane negate_plane(patch_plane plane) {
    return make_plane(qa_vec_scale(plane.normal, -1.0f), -plane.distance);
}

static bool builder_fail(patch_builder *builder, const char *message) {
    if (!builder->failed) qa_error_set(builder->error, QA_ERROR_FORMAT, 0, "%s", message);
    builder->failed = true;
    return false;
}

static bool close_points(qa_vec3 a, qa_vec3 b) {
    return fabs((double)a.x - b.x) <= 0.1 && fabs((double)a.y - b.y) <= 0.1 &&
           fabs((double)a.z - b.z) <= 0.1;
}

static bool equal_planes(patch_plane a, patch_plane b) {
    return fabs((double)a.normal.x - b.normal.x) < 0.0001 &&
           fabs((double)a.normal.y - b.normal.y) < 0.0001 &&
           fabs((double)a.normal.z - b.normal.z) < 0.0001 &&
           fabs((double)a.distance - b.distance) < 0.02;
}

static qa_vec3 midpoint(qa_vec3 a, qa_vec3 b) {
    return qa_v3((float)(a.x + 0.5*((double)b.x-a.x)),
                 (float)(a.y + 0.5*((double)b.y-a.y)),
                 (float)(a.z + 0.5*((double)b.z-a.z)));
}

static qa_vec3 curve_midpoint(qa_vec3 a, qa_vec3 b, qa_vec3 c) {
    return qa_v3((float)(0.5*(0.5*((double)a.x+b.x)+0.5*((double)b.x+c.x))),
                 (float)(0.5*(0.5*((double)a.y+b.y)+0.5*((double)b.y+c.y))),
                 (float)(0.5*(0.5*((double)a.z+b.z)+0.5*((double)b.z+c.z))));
}

static bool grid_wrapped(const patch_builder *builder) {
    for (unsigned y = 0; y < builder->height; ++y)
        if (!close_points(builder->grid[0][y], builder->grid[builder->width-1][y])) return false;
    return true;
}

static void remove_column(patch_builder *builder, unsigned x) {
    --builder->width;
    for (; x < builder->width; ++x)
        memcpy(builder->grid[x], builder->grid[x+1], builder->height*sizeof(qa_vec3));
}

static bool subdivide_grid(patch_builder *builder) {
    unsigned x = 0;
    while (x + 2 < builder->width) {
        bool needed = false;
        for (unsigned y = 0; y < builder->height; ++y) {
            qa_vec3 a = builder->grid[x][y], b = builder->grid[x+1][y], c = builder->grid[x+2][y];
            qa_vec3 delta = qa_vec_sub(curve_midpoint(a, b, c), midpoint(a, c));
            if (qa_vec_length(delta) >= 16.0f) { needed = true; break; }
        }
        if (!needed) { remove_column(builder, x+1); ++x; continue; }
        if (builder->width + 2 > PATCH_GRID_SIZE)
            return builder_fail(builder, "Q3 patch subdivision exceeds MAX_GRID_SIZE");
        for (unsigned y = 0; y < builder->height; ++y) {
            qa_vec3 a = builder->grid[x][y], b = builder->grid[x+1][y], c = builder->grid[x+2][y];
            for (unsigned column = builder->width; column > x+2; --column)
                builder->grid[column+1][y] = builder->grid[column-1][y];
            builder->grid[x+1][y] = midpoint(a, b);
            builder->grid[x+2][y] = curve_midpoint(a, b, c);
            builder->grid[x+3][y] = midpoint(b, c);
        }
        builder->width += 2;
    }
    x = 0;
    while (x + 1 < builder->width) {
        bool same = true;
        for (unsigned y = 0; y < builder->height; ++y)
            if (!close_points(builder->grid[x][y], builder->grid[x+1][y])) { same = false; break; }
        if (same) remove_column(builder, x+1);
        else ++x;
    }
    return true;
}

static void transpose_grid(patch_builder *builder) {
    unsigned side = builder->width > builder->height ? builder->width : builder->height;
    for (unsigned x = 0; x < side; ++x) {
        for (unsigned y = x+1; y < side; ++y) {
            qa_vec3 point = builder->grid[x][y];
            builder->grid[x][y] = builder->grid[y][x];
            builder->grid[y][x] = point;
        }
    }
    unsigned width = builder->width;
    builder->width = builder->height;
    builder->height = width;
}

static int32_t append_plane(patch_builder *builder, patch_plane plane) {
    if (builder->failed) return -1;
    if (!qa_vec_finite(plane.normal) || !isfinite(plane.distance)) {
        builder_fail(builder, "Nonfinite Q3 patch collision plane");
        return -1;
    }
    if (builder->plane_count == PATCH_MAX_PLANES) {
        builder_fail(builder, "Q3 patch exceeds MAX_PATCH_PLANES");
        return -1;
    }
    int32_t index = (int32_t)builder->plane_count++;
    builder->planes[index] = plane;
    return index;
}

static int32_t find_plane(patch_builder *builder, qa_vec3 a, qa_vec3 b, qa_vec3 c) {
    if (builder->failed) return -1;
    double ax = (double)c.x-a.x, ay = (double)c.y-a.y, az = (double)c.z-a.z;
    double bx = (double)b.x-a.x, by = (double)b.y-a.y, bz = (double)b.z-a.z;
    qa_vec3 normal = patch_normalize(qa_v3((float)(ay*bz-az*by), (float)(az*bx-ax*bz), (float)(ax*by-ay*bx)));
    if (qa_vec_length(normal) == 0.0f) return -1;
    patch_plane plane = make_plane(normal, (float)patch_dot(a, normal));
    if (!qa_vec_finite(normal) || !isfinite(plane.distance)) {
        builder_fail(builder, "Nonfinite Q3 patch collision plane");
        return -1;
    }
    for (size_t i = 0; i < builder->plane_count; ++i) {
        patch_plane previous = builder->planes[i];
        if (!(patch_dot(normal, previous.normal) >= 0.0)) continue;
        float da = (float)(patch_dot(a, previous.normal)-previous.distance);
        float db = (float)(patch_dot(b, previous.normal)-previous.distance);
        float dc = (float)(patch_dot(c, previous.normal)-previous.distance);
        if (fabs((double)da) <= 0.1 && fabs((double)db) <= 0.1 && fabs((double)dc) <= 0.1)
            return (int32_t)i;
    }
    return append_plane(builder, plane);
}

static patch_border find_border(patch_builder *builder, patch_plane plane) {
    patch_plane opposite = negate_plane(plane);
    for (size_t i = 0; i < builder->plane_count; ++i) {
        if (equal_planes(builder->planes[i], plane)) return (patch_border){(int32_t)i, false, false};
        if (equal_planes(builder->planes[i], opposite)) return (patch_border){(int32_t)i, true, false};
    }
    return (patch_border){append_plane(builder, plane), false, false};
}

static patch_winding base_winding(patch_plane plane) {
    qa_vec3 n = plane.normal;
    float maximum = -65535.0f;
    unsigned major = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        float magnitude = fabsf(qa_vec_component(n, axis));
        if (magnitude > maximum) { maximum = magnitude; major = axis; }
    }
    qa_vec3 initial = major == 2 ? qa_v3(1, 0, 0) : qa_v3(0, 0, 1);
    double projected = patch_dot(initial, n);
    qa_vec3 up = patch_normalize(qa_v3((float)(initial.x-projected*n.x),
                                     (float)(initial.y-projected*n.y),
                                     (float)(initial.z-projected*n.z)));
    qa_vec3 right = qa_v3((float)(((double)(up.y*n.z)-(double)up.z*n.y)*65535.0),
                         (float)(((double)up.z*n.x-(double)up.x*n.z)*65535.0),
                         (float)(((double)up.x*n.y-(double)up.y*n.x)*65535.0));
    qa_vec3 vertical = qa_vec_scale(up, 65535.0f), origin = qa_vec_scale(n, plane.distance);
    patch_winding winding = {.count = 4};
    for (unsigned i = 0; i < 4; ++i) {
        double horizontal = i == 0 || i == 3 ? -1.0 : 1.0;
        double upright = i < 2 ? 1.0 : -1.0;
        winding.points[i] = qa_v3((float)(origin.x+horizontal*right.x+upright*vertical.x),
                                  (float)(origin.y+horizontal*right.y+upright*vertical.y),
                                  (float)(origin.z+horizontal*right.z+upright*vertical.z));
    }
    return winding;
}

static bool winding_append(patch_builder *builder, patch_winding *winding, qa_vec3 point) {
    if (winding->count == PATCH_MAX_WINDING)
        return builder_fail(builder, "Q3 patch exceeds MAX_POINTS_ON_WINDING");
    winding->points[winding->count++] = point;
    return true;
}

/* Keep the front half. A wholly coplanar winding is removed, as in cm_polylib. */
static bool chop_winding(patch_builder *builder, patch_winding *winding, patch_plane plane) {
    float distances[PATCH_MAX_WINDING];
    int sides[PATCH_MAX_WINDING];
    bool front = false, back = false;
    for (unsigned i = 0; i < winding->count; ++i) {
        float distance = (float)(patch_dot(winding->points[i], plane.normal)-plane.distance);
        distances[i] = distance;
        sides[i] = distance > 0.1f ? 1 : distance < -0.1f ? -1 : 0;
        front |= sides[i] > 0;
        back |= sides[i] < 0;
    }
    if (!front) { winding->count = 0; return true; }
    if (!back) return true;
    patch_winding clipped = {0};
    for (unsigned i = 0; i < winding->count; ++i) {
        unsigned next = (i+1) % winding->count;
        qa_vec3 a = winding->points[i], b = winding->points[next];
        if (sides[i] == 0) {
            if (!winding_append(builder, &clipped, a)) return false;
            continue;
        }
        if (sides[i] > 0 && !winding_append(builder, &clipped, a)) return false;
        if (sides[next] == 0 || sides[i] == sides[next]) continue;
        double fraction = (double)distances[i] / ((double)distances[i]-distances[next]);
        qa_vec3 middle;
        for (unsigned axis = 0; axis < 3; ++axis) {
            float n = qa_vec_component(plane.normal, axis);
            float first = qa_vec_component(a, axis), last = qa_vec_component(b, axis);
            float value = n == 1.0f ? plane.distance : n == -1.0f ? -plane.distance :
                          (float)(first + fraction*((double)last-first));
            qa_vec_set_component(&middle, axis, value);
        }
        if (!winding_append(builder, &clipped, middle)) return false;
    }
    *winding = clipped;
    return true;
}

static qa_bounds empty_bounds(void) {
    return (qa_bounds){qa_v3(65535, 65535, 65535), qa_v3(-65535, -65535, -65535)};
}

static void add_to_bounds(qa_bounds *bounds, qa_vec3 point) {
    for (unsigned axis = 0; axis < 3; ++axis) {
        float value = qa_vec_component(point, axis);
        if (value < qa_vec_component(bounds->mins, axis)) qa_vec_set_component(&bounds->mins, axis, value);
        if (value > qa_vec_component(bounds->maxs, axis)) qa_vec_set_component(&bounds->maxs, axis, value);
    }
}

static qa_bounds winding_bounds(const patch_winding *winding) {
    qa_bounds bounds = empty_bounds();
    for (unsigned i = 0; i < winding->count; ++i) add_to_bounds(&bounds, winding->points[i]);
    return bounds;
}

static bool duplicate_border(const patch_builder *builder, const patch_facet *facet, patch_plane plane) {
    patch_plane opposite = negate_plane(plane);
    patch_plane surface = builder->planes[facet->surface];
    if (equal_planes(surface, plane) || equal_planes(surface, opposite)) return true;
    for (unsigned i = 0; i < facet->border_count; ++i) {
        patch_plane other = builder->planes[facet->borders[i].plane];
        if (equal_planes(other, plane) || equal_planes(other, opposite)) return true;
    }
    return false;
}

static bool append_border(patch_builder *builder, patch_facet *facet, patch_border border) {
    if (builder->failed) return false;
    if (facet->border_count == PATCH_MAX_BORDERS)
        return builder_fail(builder, "Q3 patch exceeds facet border storage");
    facet->borders[facet->border_count++] = border;
    return true;
}

static bool append_facet(patch_builder *builder, const patch_facet *facet) {
    if (builder->facet_count == PATCH_MAX_FACETS)
        return builder_fail(builder, "Q3 patch exceeds MAX_FACETS");
    builder->facets[builder->facet_count++] = *facet;
    return true;
}

static bool bevel_facet(patch_builder *builder, patch_facet *facet) {
    patch_winding winding = base_winding(builder->planes[facet->surface]);
    for (unsigned i = 0; i < facet->border_count && winding.count != 0; ++i) {
        patch_border border = facet->borders[i];
        if (border.plane == facet->surface) continue;
        patch_plane clip = builder->planes[border.plane];
        if (!border.inward) clip = negate_plane(clip);
        if (!chop_winding(builder, &winding, clip)) return false;
    }
    if (winding.count == 0) return append_facet(builder, facet);
    qa_bounds bounds = winding_bounds(&winding);
    const qa_vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (unsigned axis = 0; axis < 3; ++axis) {
        for (int direction = -1; direction <= 1; direction += 2) {
            qa_vec3 normal = qa_vec_scale(axes[axis], (float)direction);
            float distance = qa_vec_dot(normal, direction == 1 ? bounds.maxs : bounds.mins);
            patch_plane plane = make_plane(normal, distance);
            if (!duplicate_border(builder, facet, plane) &&
                !append_border(builder, facet, find_border(builder, plane))) return false;
        }
    }
    for (unsigned i = 0; i < winding.count; ++i) {
        qa_vec3 delta = qa_vec_sub(winding.points[i], winding.points[(i+1) % winding.count]);
        if (qa_vec_length(delta) < 0.5f) continue;
        qa_vec3 edge = patch_normalize(delta);
        for (unsigned axis = 0; axis < 3; ++axis) {
            float component = qa_vec_dot(edge, axes[axis]);
            if (fabs((double)component-1.0) < 0.0001) { edge = axes[axis]; break; }
            if (fabs((double)component+1.0) < 0.0001) { edge = qa_vec_scale(axes[axis], -1.0f); break; }
        }
        if (fabsf(edge.x) == 1.0f || fabsf(edge.y) == 1.0f || fabsf(edge.z) == 1.0f) continue;
        for (unsigned axis = 0; axis < 3; ++axis) {
            for (int direction = -1; direction <= 1; direction += 2) {
                qa_vec3 normal = patch_cross(edge, qa_vec_scale(axes[axis], (float)direction));
                if (qa_vec_length(normal) < 0.5f) continue;
                normal = patch_normalize(normal);
                patch_plane plane = make_plane(normal, (float)patch_dot(winding.points[i], normal));
                bool outside = false;
                for (unsigned j = 0; j < winding.count; ++j) {
                    float distance = (float)(patch_dot(winding.points[j], normal)-plane.distance);
                    if ((double)distance > 0.1) { outside = true; break; }
                }
                if (outside || duplicate_border(builder, facet, plane)) continue;
                patch_border border = find_border(builder, plane);
                if (builder->failed) return false;
                patch_plane clip = builder->planes[border.plane];
                if (!border.inward) clip = negate_plane(clip);
                patch_winding clipped = winding;
                if (!chop_winding(builder, &clipped, clip)) return false;
                if (clipped.count != 0 && !append_border(builder, facet, border)) return false;
            }
        }
    }
    if (!append_border(builder, facet, (patch_border){facet->surface, true, false})) return false;
    return append_facet(builder, facet);
}

static bool make_facet(patch_builder *builder, int32_t surface, const int32_t *raw,
                       const bool *no_adjust, unsigned border_count,
                       const qa_vec3 *vertices, unsigned vertex_count) {
    if (builder->failed) return false;
    if (surface == -1) return true;
    patch_facet facet = {.surface = surface, .border_count = border_count};
    for (unsigned i = 0; i < border_count; ++i) {
        if (raw[i] == -1) return true;
        patch_plane plane = builder->planes[raw[i]];
        bool front = false, back = false;
        for (unsigned j = 0; j < vertex_count; ++j) {
            float distance = (float)(patch_dot(vertices[j], plane.normal)-plane.distance);
            front |= (double)distance > 0.1;
            back |= (double)distance < -0.1;
        }
        if (!front && !back) return true;
        facet.borders[i] = (patch_border){raw[i], front && !back, no_adjust[i]};
    }
    patch_winding winding = base_winding(builder->planes[surface]);
    for (unsigned i = 0; i < border_count && winding.count != 0; ++i) {
        patch_plane clip = builder->planes[facet.borders[i].plane];
        if (!facet.borders[i].inward) clip = negate_plane(clip);
        if (!chop_winding(builder, &winding, clip)) return false;
    }
    if (winding.count == 0) return true;
    qa_bounds bounds = winding_bounds(&winding);
    qa_vec3 delta = qa_vec_sub(bounds.maxs, bounds.mins);
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (qa_vec_component(delta, axis) > 65535.0f ||
            qa_vec_component(bounds.mins, axis) >= 65535.0f ||
            qa_vec_component(bounds.maxs, axis) <= -65535.0f) return true;
    }
    return bevel_facet(builder, &facet);
}

static int32_t edge_plane(patch_builder *builder, unsigned x, unsigned y, unsigned edge) {
    qa_vec3 a, b, base;
    unsigned triangle;
    switch (edge) {
        case 0: a = builder->grid[x][y]; b = builder->grid[x+1][y]; base = a; triangle = 0; break;
        case 1: a = builder->grid[x+1][y]; b = builder->grid[x+1][y+1]; base = a; triangle = 0; break;
        case 2: a = builder->grid[x+1][y+1]; b = builder->grid[x][y+1]; base = b; triangle = 1; break;
        case 3: a = builder->grid[x][y+1]; b = builder->grid[x][y]; base = b; triangle = 1; break;
        case 4: a = builder->grid[x+1][y+1]; b = builder->grid[x][y]; base = a; triangle = 0; break;
        case 5: a = builder->grid[x][y]; b = builder->grid[x+1][y+1]; base = a; triangle = 1; break;
        default: builder_fail(builder, "Invalid Q3 patch edge"); return -1;
    }
    int32_t index = builder->grid_planes[x][y][triangle];
    if (index == -1) index = builder->grid_planes[x][y][1-triangle];
    if (index == -1) return -1;
    qa_vec3 point = qa_vec_add(base, qa_vec_scale(builder->planes[index].normal, 4.0f));
    return find_plane(builder, a, b, point);
}

static bool generate_facets(patch_builder *builder, bool wrap_width, bool wrap_height) {
    for (unsigned x = 0; x+1 < builder->width; ++x) {
        for (unsigned y = 0; y+1 < builder->height; ++y) {
            builder->grid_planes[x][y][0] = find_plane(builder, builder->grid[x][y], builder->grid[x+1][y], builder->grid[x+1][y+1]);
            builder->grid_planes[x][y][1] = find_plane(builder, builder->grid[x+1][y+1], builder->grid[x][y+1], builder->grid[x][y]);
            if (builder->failed) return false;
        }
    }
    for (unsigned x = 0; x+1 < builder->width; ++x) {
        for (unsigned y = 0; y+1 < builder->height; ++y) {
            int32_t first = builder->grid_planes[x][y][0], second = builder->grid_planes[x][y][1];
            int32_t top = y > 0 ? builder->grid_planes[x][y-1][1] :
                          wrap_height ? builder->grid_planes[x][builder->height-2][1] : -1;
            int32_t bottom = y+2 < builder->height ? builder->grid_planes[x][y+1][0] :
                             wrap_height ? builder->grid_planes[x][0][0] : -1;
            int32_t left = x > 0 ? builder->grid_planes[x-1][y][0] :
                           wrap_width ? builder->grid_planes[builder->width-2][y][0] : -1;
            int32_t right = x+2 < builder->width ? builder->grid_planes[x+1][y][1] :
                            wrap_width ? builder->grid_planes[0][y][1] : -1;
            bool no_top = top == first, no_bottom = bottom == second;
            bool no_left = left == second, no_right = right == first;
            if (top == -1 || top == first) top = edge_plane(builder, x, y, 0);
            if (bottom == -1 || bottom == second) bottom = edge_plane(builder, x, y, 2);
            if (left == -1 || left == second) left = edge_plane(builder, x, y, 3);
            if (right == -1 || right == first) right = edge_plane(builder, x, y, 1);
            if (builder->failed) return false;
            if (builder->facet_count == PATCH_MAX_FACETS)
                return builder_fail(builder, "Q3 patch exceeds MAX_FACETS");
            qa_vec3 block[4] = {builder->grid[x][y], builder->grid[x+1][y],
                                builder->grid[x+1][y+1], builder->grid[x][y+1]};
            if (first == second) {
                int32_t borders[4] = {top, right, bottom, left};
                bool no_adjust[4] = {no_top, no_right, no_bottom, no_left};
                if (!make_facet(builder, first, borders, no_adjust, 4, block, 4)) return false;
            } else {
                int32_t borders[3] = {top, right, second != -1 ? second : bottom != -1 ? bottom : edge_plane(builder, x, y, 4)};
                bool no_adjust[3] = {no_top, no_right, false};
                if (!make_facet(builder, first, borders, no_adjust, 3, block, 3)) return false;
                if (builder->facet_count == PATCH_MAX_FACETS)
                    return builder_fail(builder, "Q3 patch exceeds MAX_FACETS");
                qa_vec3 vertices[3] = {block[2], block[3], block[0]};
                borders[0] = bottom;
                borders[1] = left;
                borders[2] = first != -1 ? first : top != -1 ? top : edge_plane(builder, x, y, 5);
                no_adjust[0] = no_bottom;
                no_adjust[1] = no_left;
                if (!make_facet(builder, second, borders, no_adjust, 3, vertices, 3)) return false;
            }
        }
    }
    return true;
}

bool qa_q3_patch_create(uint32_t width, uint32_t height, const qa_vec3 *points,
                        qa_q3_patch **out, qa_error *error) {
    if (points == NULL || out == NULL || width < 3 || height < 3 ||
        width > PATCH_GRID_SIZE || height > PATCH_GRID_SIZE ||
        (width & 1u) == 0 || (height & 1u) == 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q3 collision patch requires an odd control grid from 3 to 129");
        return false;
    }
    for (size_t i = 0; i < (size_t)width*height; ++i) {
        if (!qa_vec_finite(points[i])) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "Nonfinite Q3 patch control point");
            return false;
        }
    }
    patch_builder *builder = calloc(1, sizeof(*builder));
    if (builder == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Unable to allocate Q3 patch generation storage");
        return false;
    }
    builder->width = width;
    builder->height = height;
    builder->error = error;
    for (unsigned x = 0; x < width; ++x)
        for (unsigned y = 0; y < height; ++y) builder->grid[x][y] = points[(size_t)y*width+x];
    bool wrap_height = grid_wrapped(builder);
    if (!subdivide_grid(builder)) { free(builder); return false; }
    transpose_grid(builder);
    bool wrap_width = grid_wrapped(builder);
    if (!subdivide_grid(builder) || !generate_facets(builder, wrap_width, wrap_height)) {
        free(builder);
        return false;
    }
    qa_q3_patch *patch = calloc(1, sizeof(*patch));
    if (patch == NULL) {
        free(builder);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Unable to allocate Q3 collision patch");
        return false;
    }
    patch->plane_count = builder->plane_count;
    patch->facet_count = builder->facet_count;
    if (patch->plane_count != 0) patch->planes = malloc(patch->plane_count*sizeof(*patch->planes));
    if (patch->facet_count != 0) patch->facets = malloc(patch->facet_count*sizeof(*patch->facets));
    if ((patch->plane_count != 0 && patch->planes == NULL) ||
        (patch->facet_count != 0 && patch->facets == NULL)) {
        free(builder);
        qa_q3_patch_destroy(patch);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Unable to allocate Q3 patch planes and facets");
        return false;
    }
    if (patch->plane_count != 0) memcpy(patch->planes, builder->planes, patch->plane_count*sizeof(*patch->planes));
    if (patch->facet_count != 0) memcpy(patch->facets, builder->facets, patch->facet_count*sizeof(*patch->facets));
    patch->bounds = empty_bounds();
    for (unsigned x = 0; x < builder->width; ++x)
        for (unsigned y = 0; y < builder->height; ++y) add_to_bounds(&patch->bounds, builder->grid[x][y]);
    patch->bounds.mins = qa_vec_sub(patch->bounds.mins, qa_v3(1, 1, 1));
    patch->bounds.maxs = qa_vec_add(patch->bounds.maxs, qa_v3(1, 1, 1));
    free(builder);
    *out = patch;
    return true;
}

void qa_q3_patch_destroy(qa_q3_patch *patch) {
    if (patch == NULL) return;
    free(patch->planes);
    free(patch->facets);
    free(patch);
}

static double box_offset(patch_plane source, qa_vec3 normal, const qa_q3_shape *shape) {
    qa_vec3 corner = qa_v3((source.signbits & 1u) != 0 ? shape->extents.x : shape->mins.x,
                           (source.signbits & 2u) != 0 ? shape->extents.y : shape->mins.y,
                           (source.signbits & 4u) != 0 ? shape->extents.z : shape->mins.z);
    return patch_dot(corner, normal);
}

static patch_plane expanded_plane(patch_plane source, const qa_q3_shape *shape, const patch_border *border) {
    patch_plane plane = border != NULL && border->inward ? negate_plane(source) : source;
    if (shape->kind == QA_SHAPE_POINT) return plane;
    if (shape->kind == QA_SHAPE_CAPSULE) {
        double offset = (double)shape->radius + fabs((double)qa_vec_dot(plane.normal, shape->offset));
        plane.distance = (float)(plane.distance+offset);
    } else {
        double offset = box_offset(source, plane.normal, shape);
        /* no_adjust is retained metadata. Both source trace paths expand it. */
        plane.distance = (float)(plane.distance + (border == NULL ? -offset : fabs(offset)));
    }
    return plane;
}

bool qa_q3_patch_position(const qa_q3_patch *patch, qa_vec3 start, const qa_q3_shape *shape) {
    if (shape->kind == QA_SHAPE_POINT) return false;
    for (size_t i = 0; i < patch->facet_count; ++i) {
        const patch_facet *facet = &patch->facets[i];
        patch_plane plane = expanded_plane(patch->planes[facet->surface], shape, NULL);
        if (qa_vec_dot(start, plane.normal) > plane.distance) continue;
        unsigned border;
        for (border = 0; border < facet->border_count; ++border) {
            const patch_border *side = &facet->borders[border];
            plane = expanded_plane(patch->planes[side->plane], shape, side);
            if (qa_vec_dot(start, plane.normal) > plane.distance) break;
        }
        if (border == facet->border_count) return true;
    }
    return false;
}

static bool trace_point(const qa_q3_patch *patch, qa_vec3 start, qa_vec3 end,
                         const qa_q3_shape *shape, float *fraction, qa_collision_plane *hit_plane) {
    float intersections[PATCH_MAX_PLANES];
    bool front[PATCH_MAX_PLANES];
    for (size_t i = 0; i < patch->plane_count; ++i) {
        patch_plane plane = patch->planes[i];
        double offset = box_offset(plane, plane.normal, shape);
        float d1 = (float)((double)qa_vec_dot(start, plane.normal)-plane.distance+offset);
        float d2 = (float)((double)qa_vec_dot(end, plane.normal)-plane.distance+offset);
        float crossing = d1 == d2 ? 99999.0f : (float)((double)d1/((double)d1-d2));
        front[i] = d1 > 0.0f;
        intersections[i] = crossing <= 0.0f ? 99999.0f : crossing;
    }
    bool hit = false;
    for (size_t i = 0; i < patch->facet_count; ++i) {
        const patch_facet *facet = &patch->facets[i];
        float surface_crossing = intersections[facet->surface];
        if (!front[facet->surface] || surface_crossing > *fraction) continue;
        unsigned border;
        for (border = 0; border < facet->border_count; ++border) {
            patch_border side = facet->borders[border];
            if (front[side.plane] != side.inward) {
                if (intersections[side.plane] > surface_crossing) break;
            } else if (intersections[side.plane] < surface_crossing) break;
        }
        if (border != facet->border_count) continue;
        patch_plane plane = patch->planes[facet->surface];
        double offset = box_offset(plane, plane.normal, shape);
        float d1 = (float)((double)qa_vec_dot(start, plane.normal)-plane.distance+offset);
        float d2 = (float)((double)qa_vec_dot(end, plane.normal)-plane.distance+offset);
        *fraction = fmaxf(0.0f, (float)(((double)d1-0.125)/((double)d1-d2)));
        hit_plane->normal = plane.normal;
        hit_plane->distance = plane.distance;
        hit = true;
    }
    return hit;
}

typedef struct facet_trace {
    float enter, leave;
    int hit_index;
    patch_plane best;
} facet_trace;

static bool clip_trace(qa_vec3 start, qa_vec3 end, patch_plane plane,
                        const qa_q3_shape *shape, int index, facet_trace *trace) {
    float d1 = qa_vec_dot(start, plane.normal)-plane.distance;
    float d2 = qa_vec_dot(end, plane.normal)-plane.distance;
    if (d1 > 0.0f && (d2 >= 0.125f || d2 >= d1)) return false;
    if (d1 <= 0.0f && d2 <= 0.0f) return true;
    if (d1 > d2) {
        float fraction = fmaxf(0.0f, (float)(((double)d1-0.125)/((double)d1-d2)));
        if (fraction > trace->enter) {
            trace->enter = fraction;
            trace->hit_index = index;
            trace->best = plane;
            if (shape->kind == QA_SHAPE_CAPSULE)
                trace->best.distance -= fabsf(qa_vec_dot(plane.normal, shape->offset));
        }
    } else {
        float fraction = fminf(1.0f, (float)(((double)d1+0.125)/((double)d1-d2)));
        trace->leave = fminf(trace->leave, fraction);
    }
    return true;
}

bool qa_q3_patch_trace(const qa_q3_patch *patch, qa_vec3 start, qa_vec3 end,
                       const qa_q3_shape *shape, float *fraction, qa_collision_plane *plane) {
    if (shape->kind == QA_SHAPE_POINT) return trace_point(patch, start, end, shape, fraction, plane);
    bool hit = false;
    for (size_t i = 0; i < patch->facet_count; ++i) {
        const patch_facet *facet = &patch->facets[i];
        facet_trace trace = {.enter = -1.0f, .leave = 1.0f, .hit_index = -1};
        patch_plane surface = expanded_plane(patch->planes[facet->surface], shape, NULL);
        if (!clip_trace(start, end, surface, shape, -1, &trace)) continue;
        unsigned border;
        for (border = 0; border < facet->border_count; ++border) {
            const patch_border *side = &facet->borders[border];
            patch_plane clip = expanded_plane(patch->planes[side->plane], shape, side);
            if (!clip_trace(start, end, clip, shape, (int)border, &trace)) break;
        }
        if (border == facet->border_count && trace.hit_index != (int)facet->border_count-1 &&
            trace.enter < trace.leave && trace.enter >= 0.0f && trace.enter < *fraction) {
            *fraction = trace.enter;
            plane->normal = trace.best.normal;
            plane->distance = trace.best.distance;
            hit = true;
        }
    }
    return hit;
}
