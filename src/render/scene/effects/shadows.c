#include "internal.h"
#include "qa/scene_effects.h"

#include <stdlib.h>
#include <string.h>

enum { SHADOW_ATLAS_SIZE = 2048, SHADOW_LIGHTS = 8, SHADOW_MINIMUM = 128 };
static const float shadow_near = 4;
typedef struct shadow_basis { qa_vec3 forward, right, up; } shadow_basis;
static const shadow_basis cube_faces[6] = {
    {{1, 0, 0}, {0, -1, 0}, {0, 0, 1}}, {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
    {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}}, {{0, -1, 0}, {-1, 0, 0}, {0, 0, 1}},
    {{0, 0, 1}, {0, 1, 0}, {1, 0, 0}}, {{0, 0, -1}, {0, -1, 0}, {1, 0, 0}}
};

/* Tokens compare actual versioned dependencies, without geometry scans or hash
 * collisions. Zero resource identities intentionally disable cache reuse. */
typedef struct shadow_token {
    uint64_t caster_identity, caster_revision, mesh_identity, mesh_revision;
    size_t vertex_count, index_count;
    qa_scene_matrix transform;
    qa_bounds bounds;
    bool world_geometry;
} shadow_token;
typedef struct shadow_cache {
    bool valid;
    uint64_t light_identity, light_revision;
    qa_vec3 origin, direction;
    float radius, cos_half_angle;
    bool spot;
    qa_scene_rect rect;
    shadow_token *tokens;
    size_t token_count;
} shadow_cache;
struct qa_scene_shadows {
    qa_scene_resources *resources;
    qa_scene_image *atlas;
    shadow_cache cache[SHADOW_LIGHTS];
};
typedef struct shadow_candidate {
    size_t light_index;
    uint32_t face_size;
    qa_scene_rect rect;
    bool packed;
    shadow_cache next;
} shadow_candidate;
typedef struct caster_sphere { qa_vec3 origin; float radius; } caster_sphere;

static qa_scene_matrix multiply(qa_scene_matrix a, qa_scene_matrix b)
{
    qa_scene_matrix result;
    for (size_t column = 0; column < 4; ++column) for (size_t row = 0; row < 4; ++row) {
        double value = 0;
        for (size_t k = 0; k < 4; ++k) value += (double)a.m[k * 4 + row] * b.m[column * 4 + k];
        result.m[column * 4 + row] = (float)value;
    }
    return result;
}

static double dot(qa_vec3 a, qa_vec3 b)
{
    return (double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z;
}

static qa_scene_matrix perspective(double fov, float radius)
{
    double scale = 1 / tan(fov * QA_EFFECT_PI / 360), far_clip = fmax(radius, 8);
    return (qa_scene_matrix){{(float)scale, 0, 0, 0, 0, (float)scale, 0, 0,
        0, 0, (float)((far_clip + shadow_near) / (shadow_near - far_clip)), -1,
        0, 0, (float)(2 * far_clip * shadow_near / (shadow_near - far_clip)), 0}};
}

static qa_scene_matrix shadow_view(qa_vec3 origin, shadow_basis basis)
{
    qa_vec3 r = basis.right, u = basis.up, v = basis.forward;
    return (qa_scene_matrix){{r.x, u.x, -v.x, 0, r.y, u.y, -v.y, 0, r.z, u.z, -v.z, 0,
        (float)-dot(r, origin), (float)-dot(u, origin), (float)dot(v, origin), 1}};
}

static shadow_basis cone_basis(qa_vec3 direction)
{
    qa_vec3 forward = qa_vec_length(direction) < 0.000001f ? qa_v3(0, 0, -1) : qa_vec_normalize(direction);
    qa_vec3 reference = fabsf(forward.z) >= 0.99f ? qa_v3(1, 0, 0) : qa_v3(0, 0, 1);
    qa_vec3 right = qa_vec_normalize(qa_vec_cross(forward, reference));
    return (shadow_basis){forward, right, qa_vec_cross(right, forward)};
}

static double cone_fov(float cosine)
{
    return fmin(acos(fmin(1, fmax(-1, cosine))) * 180 / QA_EFFECT_PI * 2 * 1.15, 175);
}

static uint32_t resolution(uint32_t requested, uint32_t cap)
{
    uint32_t maximum = cap ? cap : 1024;
    if (maximum > 1024) maximum = 1024;
    if (maximum < SHADOW_MINIMUM) maximum = SHADOW_MINIMUM;
    uint32_t wanted = requested ? requested : 512;
    if (wanted < SHADOW_MINIMUM) wanted = SHADOW_MINIMUM;
    if (wanted > maximum) wanted = maximum;
    uint32_t result = SHADOW_MINIMUM;
    while (result * 2 <= wanted) result *= 2;
    return result;
}

static uint32_t candidate_width(const shadow_candidate *candidate, const qa_scene_shadow_light *lights)
{
    return candidate->face_size * (lights[candidate->light_index].light.spot ? 1u : 3u);
}

static uint32_t candidate_height(const shadow_candidate *candidate, const qa_scene_shadow_light *lights)
{
    return candidate->face_size * (lights[candidate->light_index].light.spot ? 1u : 2u);
}

static bool pack(shadow_candidate *candidates, size_t count, const qa_scene_shadow_light *lights)
{
    /* Stable insertion sort matches JS stable area sorting, retaining light
     * order for equal extents. Packing order does not reorder returned lights. */
    for (size_t i = 1; i < count; ++i) {
        shadow_candidate item = candidates[i];
        uint32_t area = candidate_width(&item, lights) * candidate_height(&item, lights);
        size_t j = i;
        while (j && candidate_width(&candidates[j - 1], lights) * candidate_height(&candidates[j - 1], lights) < area) {
            candidates[j] = candidates[j - 1];
            --j;
        }
        candidates[j] = item;
    }
    uint32_t x = 0, y = 0, shelf_height = 0;
    bool all = true;
    for (size_t i = 0; i < count; ++i) {
        shadow_candidate *candidate = &candidates[i];
        uint32_t width = candidate_width(candidate, lights), height = candidate_height(candidate, lights);
        candidate->packed = false;
        if (width > SHADOW_ATLAS_SIZE || height > SHADOW_ATLAS_SIZE) { all = false; continue; }
        if (x + width > SHADOW_ATLAS_SIZE) { y += shelf_height; shelf_height = 0; x = 0; }
        if (y + height > SHADOW_ATLAS_SIZE) { all = false; continue; }
        candidate->rect = (qa_scene_rect){(int32_t)x, (int32_t)y, width, height};
        candidate->packed = true;
        x += width;
        if (height > shelf_height) shelf_height = height;
    }
    return all;
}

static void fit(shadow_candidate *candidates, size_t count, const qa_scene_shadow_light *lights)
{
    if (pack(candidates, count, lights)) return;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        bool shrank = false;
        for (size_t i = 0; i < count; ++i)
            if (!lights[candidates[i].light_index].light.spot && candidates[i].face_size > SHADOW_MINIMUM) {
                candidates[i].face_size /= 2;
                shrank = true;
            }
        if (!shrank || pack(candidates, count, lights)) return;
    }
}

static caster_sphere caster_bounds(const qa_scene_shadow_caster *caster)
{
    qa_vec3 center = qa_vec_scale(qa_vec_add(caster->bounds.mins, caster->bounds.maxs), 0.5f);
    caster_sphere sphere = {qa_effect_point(caster->transform, center), 0};
    for (unsigned i = 0; i < 8; ++i) {
        qa_vec3 point = qa_v3(i & 1 ? caster->bounds.maxs.x : caster->bounds.mins.x,
                              i & 2 ? caster->bounds.maxs.y : caster->bounds.mins.y,
                              i & 4 ? caster->bounds.maxs.z : caster->bounds.mins.z);
        float radius = qa_vec_length(qa_vec_sub(qa_effect_point(caster->transform, point), sphere.origin));
        sphere.radius = fmaxf(sphere.radius, radius);
    }
    return sphere;
}

static bool light_contains(const qa_scene_light *light, caster_sphere sphere)
{
    qa_vec3 delta = qa_vec_sub(sphere.origin, light->origin);
    double distance = sqrt(dot(delta, delta));
    if (distance > (double)light->radius + sphere.radius) return false;
    if (light->spot && distance > sphere.radius) {
        double diagonal = atan(sqrt(2) * tan(fmin(cone_fov(light->cos_half_angle) * QA_EFFECT_PI / 360, 87 * QA_EFFECT_PI / 180)));
        double angle = acos(fmin(1, fmax(-1, dot(delta, light->direction) / distance)));
        double separation = angle - asin(fmin(1, sphere.radius / distance));
        if (cos(fmin(QA_EFFECT_PI, fmax(0, separation))) < cos(diagonal)) return false;
    }
    return true;
}

static bool world_mesh_visible(const qa_scene_mesh *mesh, qa_scene_matrix transform,
                                const qa_scene_light *light, const qa_vec3 *forward)
{
    bool in_radius = false, in_front = forward == NULL;
    double radius = fmax(light->radius, 8), radius_squared = radius * radius;
    for (size_t i = 0; i < mesh->vertex_count; ++i) {
        qa_vec3 delta = qa_vec_sub(qa_effect_point(transform, mesh->vertices[i].position), light->origin);
        in_radius |= dot(delta, delta) <= radius_squared;
        if (forward) in_front |= dot(delta, *forward) > 0;
        if (in_radius && in_front) return true;
    }
    return false;
}

static bool make_signature(shadow_candidate *candidate, const qa_scene_light *light,
                            const qa_scene_shadow_caster *casters, const caster_sphere *spheres,
                            size_t caster_count, qa_error *error)
{
    shadow_cache *signature = &candidate->next;
    signature->valid = light->identity != 0;
    signature->light_identity = light->identity;
    signature->light_revision = light->revision;
    signature->origin = light->origin;
    signature->direction = light->direction;
    signature->radius = light->radius;
    signature->cos_half_angle = light->cos_half_angle;
    signature->spot = light->spot;
    signature->rect = candidate->rect;
    size_t count = 0;
    for (size_t i = 0; i < caster_count; ++i) {
        if (!casters[i].world_geometry && !light_contains(light, spheres[i])) continue;
        if (casters[i].mesh_count > SIZE_MAX / sizeof(shadow_token) - count) {
            qa_error_set(error, QA_ERROR_MEMORY, i, "Shadow cache dependency count overflow");
            return false;
        }
        count += casters[i].mesh_count;
    }
    if (count) {
        signature->tokens = calloc(count, sizeof(*signature->tokens));
        if (!signature->tokens) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate shadow dependencies");
            return false;
        }
    }
    signature->token_count = count;
    size_t cursor = 0;
    for (size_t i = 0; i < caster_count; ++i) {
        const qa_scene_shadow_caster *caster = &casters[i];
        if (!caster->world_geometry && !light_contains(light, spheres[i])) continue;
        if (!caster->identity) signature->valid = false;
        for (size_t j = 0; j < caster->mesh_count; ++j) {
            const qa_scene_mesh *mesh = &caster->meshes[j];
            shadow_token *token = &signature->tokens[cursor++];
            token->caster_identity = caster->identity;
            token->caster_revision = caster->revision;
            token->mesh_identity = mesh->identity;
            token->mesh_revision = mesh->revision;
            token->vertex_count = mesh->vertex_count;
            token->index_count = mesh->index_count;
            token->transform = caster->transform;
            token->bounds = caster->bounds;
            token->world_geometry = caster->world_geometry;
            if (!mesh->identity) signature->valid = false;
        }
    }
    return true;
}

static bool equal_vec(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

static bool same_signature(const shadow_cache *a, const shadow_cache *b)
{
    if (!a->valid || !b->valid || a->light_identity != b->light_identity || a->light_revision != b->light_revision ||
        !equal_vec(a->origin, b->origin) || !equal_vec(a->direction, b->direction) || a->radius != b->radius ||
        a->cos_half_angle != b->cos_half_angle || a->spot != b->spot || a->rect.x != b->rect.x ||
        a->rect.y != b->rect.y || a->rect.width != b->rect.width || a->rect.height != b->rect.height ||
        a->token_count != b->token_count) return false;
    for (size_t i = 0; i < a->token_count; ++i) {
        const shadow_token *x = &a->tokens[i], *y = &b->tokens[i];
        if (x->caster_identity != y->caster_identity || x->caster_revision != y->caster_revision ||
            x->mesh_identity != y->mesh_identity || x->mesh_revision != y->mesh_revision ||
            x->vertex_count != y->vertex_count || x->index_count != y->index_count ||
            x->world_geometry != y->world_geometry || !equal_vec(x->bounds.mins, y->bounds.mins) ||
            !equal_vec(x->bounds.maxs, y->bounds.maxs)) return false;
        for (size_t j = 0; j < 16; ++j) if (x->transform.m[j] != y->transform.m[j]) return false;
    }
    return true;
}

qa_scene_shadows *qa_scene_shadows_create(qa_scene_resources *resources, qa_error *error)
{
    if (!resources) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Shadow atlas requires scene resources");
        return NULL;
    }
    qa_scene_shadows *shadows = calloc(1, sizeof(*shadows));
    if (!shadows) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate shadow atlas"); return NULL; }
    shadows->resources = resources;
    return shadows;
}

void qa_scene_shadows_invalidate(qa_scene_shadows *shadows)
{
    if (!shadows) return;
    for (size_t i = 0; i < SHADOW_LIGHTS; ++i) shadows->cache[i].valid = false;
}

void qa_scene_shadows_destroy(qa_scene_shadows *shadows)
{
    if (!shadows) return;
    if (shadows->atlas) qa_scene_image_release(shadows->atlas);
    for (size_t i = 0; i < SHADOW_LIGHTS; ++i) free(shadows->cache[i].tokens);
    free(shadows);
}

static bool create_atlas(qa_scene_shadows *shadows, qa_error *error)
{
    if (shadows->atlas) return true;
    size_t count = (size_t)SHADOW_ATLAS_SIZE * SHADOW_ATLAS_SIZE;
    float *pixels = malloc(count * sizeof(*pixels));
    if (!pixels) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate shadow depth pixels"); return false; }
    for (size_t i = 0; i < count; ++i) pixels[i] = 1;
    qa_scene_image_level level = {SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE, pixels, count * sizeof(*pixels)};
    bool ok = qa_scene_image_create(shadows->resources, "*q2-shadow-atlas", QA_SCENE_DEPTH32F,
                                    &level, 1, QA_SCENE_CLAMP, QA_SCENE_NEAREST,
                                    (qa_scene_vec4){1, 1, 1, 1}, &shadows->atlas, error);
    free(pixels);
    return ok;
}

static bool depth_pass(qa_scene_frame *frame, const qa_scene_light *light,
                        const qa_scene_shadow_caster *casters, const caster_sphere *spheres,
                        size_t caster_count, qa_scene_rect rect, shadow_basis basis,
                        qa_scene_matrix projection, bool point, qa_error *error)
{
    qa_scene_command command = {0};
    command.kind = QA_SCENE_COMMAND_VIEW;
    /* Scene viewports count rows from the top. Atlas UV rectangles retain the
     * donor's bottom-left origin, including the 3 by 2 cube-face arrangement. */
    rect.y = SHADOW_ATLAS_SIZE - rect.y - (int32_t)rect.height;
    command.data.view.viewport = rect;
    command.data.view.origin = light->origin;
    command.data.view.axis[0] = basis.forward;
    command.data.view.axis[1] = qa_vec_scale(basis.right, -1);
    command.data.view.axis[2] = basis.up;
    command.data.view.projection = projection;
    command.data.view.clear_depth = true;
    command.data.view.depth = 1;
    if (!qa_scene_frame_emit(frame, &command, error)) return false;
    qa_scene_matrix matrix = multiply(projection, shadow_view(light->origin, basis));
    /* World geometry precedes dynamic casters, even if the caller interleaves
     * the two classes in its collection. */
    for (unsigned pass = 0; pass < 2; ++pass) for (size_t i = 0; i < caster_count; ++i) {
        const qa_scene_shadow_caster *caster = &casters[i];
        if (caster->world_geometry != (pass == 0)) continue;
        if (!caster->world_geometry) {
            if (!light_contains(light, spheres[i])) continue;
            if (point && dot(qa_vec_sub(spheres[i].origin, light->origin), basis.forward) + spheres[i].radius <= 0) continue;
        }
        for (size_t j = 0; j < caster->mesh_count; ++j) {
            const qa_scene_mesh *mesh = &caster->meshes[j];
            if (caster->world_geometry && !world_mesh_visible(mesh, caster->transform, light, point ? &basis.forward : NULL)) continue;
            if (mesh->index_count == 0) continue;
            qa_scene_draw draw = {0};
            draw.mesh = *mesh;
            draw.model = caster->transform;
            draw.mvp = multiply(matrix, caster->transform);
            qa_scene_state_default(&draw.state);
            draw.state.cull = QA_CULL_NONE;
            draw.state.color_write = false;
            draw.state.depth_write = true;
            draw.state.depth_test = QA_DEPTH_LEQUAL;
            draw.state.polygon_offset = true;
            draw.state.offset_factor = caster->world_geometry ? 2 : 1;
            draw.state.offset_units = caster->world_geometry ? 4 : 2;
            if (!qa_scene_frame_draw(frame, &draw, error)) return false;
        }
    }
    return true;
}

bool qa_scene_shadows_prepare_options(qa_scene_shadows *shadows, const qa_scene_light *source,
                                      size_t source_count, const qa_scene_shadow_caster *casters,
                                      size_t caster_count, const qa_scene_shadow_options *options,
                                      qa_scene_frame *frame, const qa_scene_shadow_light **out_lights,
                                      size_t *out_count, const qa_scene_image **out_atlas, qa_error *error)
{
    if (!shadows || !frame || !out_lights || !out_count || !out_atlas ||
        (!source && source_count) || (!casters && caster_count)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid shadow preparation input");
        return false;
    }
    size_t light_count = 0;
    for (size_t i = 0; i < source_count && light_count < SHADOW_LIGHTS; ++i)
        if (source[i].family == QA_SCENE_Q2) ++light_count;
    qa_scene_shadow_light *lights = NULL;
    if (light_count) {
        lights = qa_arena_alloc(&frame->storage, light_count * sizeof(*lights), _Alignof(qa_scene_shadow_light), error);
        if (!lights) return false;
        memset(lights, 0, light_count * sizeof(*lights));
    }
    size_t cursor = 0, candidate_count = 0;
    shadow_candidate candidates[SHADOW_LIGHTS] = {0};
    bool enabled = !options || options->enabled;
    uint32_t resolution_cap = options ? options->resolution_cap : 1024;
    for (size_t i = 0; i < source_count && cursor < light_count; ++i) {
        if (source[i].family != QA_SCENE_Q2) continue;
        const qa_scene_light *light = &source[i];
        lights[cursor].light = *light;
        if (enabled && light->radius > 0 && (light->casts_shadow || light->spot)) {
            if (!qa_vec_finite(light->origin) || !qa_vec_finite(light->direction) ||
                !isfinite(light->radius) || !isfinite(light->cos_half_angle)) {
                qa_error_set(error, QA_ERROR_ARGUMENT, i, "Non-finite shadow light");
                return false;
            }
            candidates[candidate_count].light_index = cursor;
            candidates[candidate_count++].face_size = resolution(light->casts_shadow ? light->shadow_resolution : 0, resolution_cap);
        }
        ++cursor;
    }
    if (!candidate_count) {
        qa_scene_shadows_invalidate(shadows);
        if (!enabled && shadows->atlas) {
            qa_scene_image_release(shadows->atlas);
            shadows->atlas = NULL;
        }
        *out_lights = lights;
        *out_count = light_count;
        *out_atlas = NULL;
        return true;
    }
    if (caster_count > SIZE_MAX / sizeof(caster_sphere)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Shadow caster count overflow");
        return false;
    }
    caster_sphere *spheres = caster_count ? qa_arena_alloc(&frame->storage, caster_count * sizeof(*spheres), _Alignof(caster_sphere), error) : NULL;
    if (caster_count && !spheres) return false;
    for (size_t i = 0; i < caster_count; ++i) {
        if (!casters[i].meshes && casters[i].mesh_count) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i, "Shadow caster lacks meshes");
            return false;
        }
        spheres[i] = caster_bounds(&casters[i]);
    }
    if (!create_atlas(shadows, error)) return false;
    fit(candidates, candidate_count, lights);
    size_t commands_before = frame->command_count, images_before = frame->image_count;
    bool target_bound = false;
    for (size_t i = 0; i < candidate_count; ++i) {
        shadow_candidate *candidate = &candidates[i];
        if (!candidate->packed) continue;
        qa_scene_shadow_light *prepared = &lights[candidate->light_index];
        const qa_scene_light *light = &prepared->light;
        prepared->atlas_rect = (qa_scene_vec4){(float)candidate->rect.x / (float)SHADOW_ATLAS_SIZE,
            (float)candidate->rect.y / (float)SHADOW_ATLAS_SIZE, (float)candidate->rect.width / (float)SHADOW_ATLAS_SIZE,
            (float)candidate->rect.height / (float)SHADOW_ATLAS_SIZE};
        prepared->point_shadow = !light->spot;
        prepared->shadow_valid = true;
        qa_scene_matrix projection = perspective(light->spot ? cone_fov(light->cos_half_angle) : 90, light->radius);
        shadow_basis basis = light->spot ? cone_basis(light->direction) : cube_faces[0];
        if (light->spot) {
            qa_scene_matrix bias = {{0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 0.5f, 0, 0.5f, 0.5f, 0.5f, 1}};
            prepared->shadow_matrix = multiply(bias, multiply(projection, shadow_view(light->origin, basis)));
        } else qa_scene_matrix_identity(&prepared->shadow_matrix);
        if (!make_signature(candidate, light, casters, spheres, caster_count, error)) goto failure;
        if (same_signature(&shadows->cache[candidate->light_index], &candidate->next)) continue;
        if (!target_bound) {
            qa_scene_command target = {0};
            target.kind = QA_SCENE_COMMAND_TARGET;
            target.data.target.image = shadows->atlas;
            if (!qa_scene_frame_emit(frame, &target, error)) goto failure;
            target_bound = true;
        }
        unsigned faces = light->spot ? 1 : 6;
        for (unsigned face = 0; face < faces; ++face) {
            qa_scene_rect rect = candidate->rect;
            if (!light->spot) {
                basis = cube_faces[face];
                rect.x += (int32_t)(face % 3 * candidate->face_size);
                rect.y += (int32_t)(face / 3 * candidate->face_size);
                rect.width = rect.height = candidate->face_size;
            }
            if (!depth_pass(frame, light, casters, spheres, caster_count, rect, basis, projection, !light->spot, error)) goto failure;
        }
    }
    if (target_bound) {
        qa_scene_command target = {0};
        target.kind = QA_SCENE_COMMAND_TARGET;
        if (!qa_scene_frame_emit(frame, &target, error)) goto failure;
    }
    for (size_t i = 0; i < SHADOW_LIGHTS; ++i) {
        free(shadows->cache[i].tokens);
        memset(&shadows->cache[i], 0, sizeof(shadows->cache[i]));
    }
    for (size_t i = 0; i < candidate_count; ++i)
        if (candidates[i].packed) shadows->cache[candidates[i].light_index] = candidates[i].next;
    *out_lights = lights;
    *out_count = light_count;
    *out_atlas = shadows->atlas;
    return true;

failure:
    /* Appending a failed atlas must not leave a partially bound target or a
     * cache claim for work which will never reach the backend. */
    for (size_t i = 0; i < candidate_count; ++i) free(candidates[i].next.tokens);
    for (size_t i = images_before; i < frame->image_count; ++i) qa_scene_image_release(frame->images[i]);
    frame->image_count = images_before;
    frame->command_count = commands_before;
    return false;
}

bool qa_scene_shadows_prepare(qa_scene_shadows *shadows, const qa_scene_light *source,
                              size_t source_count, const qa_scene_shadow_caster *casters,
                              size_t caster_count, qa_scene_frame *frame,
                              const qa_scene_shadow_light **out_lights, size_t *out_count,
                              const qa_scene_image **out_atlas, qa_error *error)
{
    return qa_scene_shadows_prepare_options(shadows, source, source_count, casters, caster_count,
                                            NULL, frame, out_lights, out_count, out_atlas, error);
}
