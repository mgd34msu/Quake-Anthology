#include "internal.h"
#include "qa/binary.h"

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool light_error(qa_error *error, qa_status status, size_t face, const char *message)
{
    qa_error_set(error, status, face, "%s", message);
    return false;
}

static float project(const float axis[4], qa_vec3 point)
{
    return point.x * axis[0] + point.y * axis[1] + point.z * axis[2] + axis[3];
}

/* Uint32Array assignment in the donor truncates and wraps each addition. */
static uint32_t light_u32(double value)
{
    double integer = fmod(trunc(value), 4294967296.0);
    if (integer < 0) integer += 4294967296.0;
    return (uint32_t)integer;
}

bool qawl_light_styles(qa_scene_world *world, const qa_scene_world_input *input, qa_error *error)
{
    qawl_world *data = world->legacy_data;
    bool q1 = world->bsp.family == QA_BSP_Q1;
    const void *source = q1 ? (const void *)input->q1_styles : (const void *)input->q2_styles;
    size_t count = source != NULL ? input->style_count : 0;
    size_t stride = q1 ? sizeof(float) : sizeof(qa_vec3);
    if (count > SIZE_MAX / stride)
        return light_error(error, QA_ERROR_MEMORY, 0, "lightstyle snapshot is too large");
    for (size_t i = 0; i < count; ++i) {
        if (q1 ? !isfinite(input->q1_styles[i]) : !qa_vec_finite(input->q2_styles[i]))
            return light_error(error, QA_ERROR_ARGUMENT, i, "nonfinite lightstyle");
    }
    void *storage = q1 ? (void *)data->q1_styles : (void *)data->q2_styles;
    if (count != data->style_count) {
        void *replacement = count != 0 ? malloc(count * stride) : NULL;
        if (count != 0 && replacement == NULL)
            return light_error(error, QA_ERROR_MEMORY, 0, "cannot allocate lightstyle snapshot");
        free(storage);
        storage = replacement;
        if (q1) data->q1_styles = storage;
        else data->q2_styles = storage;
        data->style_count = count;
    }
    if (count != 0) memcpy(storage, source, count * stride);
    return true;
}

static bool face_styles(qaw_legacy *light, const qa_bsp_q1_metadata *metadata,
                        const qa_bsp_face *face, size_t index, qa_error *error)
{
    const uint8_t *bytes = face->styles;
    size_t count = 4, stride = 1;
    uint16_t terminator = UINT8_MAX;
    if (metadata->styles16.data != NULL) {
        count = metadata->styles16_per_face;
        bytes = metadata->styles16.data + index * count * 2;
        stride = 2;
        terminator = UINT16_MAX;
    } else if (metadata->styles.data != NULL) {
        count = metadata->styles_per_face;
        bytes = metadata->styles.data + index * count;
    }
    size_t used = 0;
    while (used < count) {
        uint16_t style = stride == 2 ? qa_load_u16le(bytes + used * 2) : bytes[used];
        if (style == terminator) break;
        ++used;
    }
    if (used > SIZE_MAX / sizeof(*light->styles))
        return light_error(error, QA_ERROR_MEMORY, index, "too many face lightstyles");
    if (used != 0) {
        light->styles = malloc(used * sizeof(*light->styles));
        if (light->styles == NULL)
            return light_error(error, QA_ERROR_MEMORY, index, "cannot allocate face lightstyles");
    }
    light->style_count = used;
    for (size_t i = 0; i < used; ++i)
        light->styles[i] = stride == 2 ? qa_load_u16le(bytes + i * 2) : bytes[i];
    return true;
}

static bool classic_projection(qaw_legacy *light, const qa_bsp_texinfo *info,
                               const qa_vec3 *points, size_t count, float step,
                               size_t face, qa_error *error)
{
    if (count == 0 || !isfinite(step))
        return light_error(error, QA_ERROR_FORMAT, face, "invalid lightmap extent");
    uint32_t dimensions[2];
    for (size_t axis = 0; axis < 2; ++axis) {
        double minimum = DBL_MAX, maximum = -DBL_MAX;
        for (size_t i = 0; i < count; ++i) {
            double value = (double)points[i].x * info->projection[axis][0]
                + (double)points[i].y * info->projection[axis][1]
                + (double)points[i].z * info->projection[axis][2] + info->projection[axis][3];
            minimum = fmin(minimum, value);
            maximum = fmax(maximum, value);
        }
        double first = floor(minimum / step), last = ceil(maximum / step);
        double dimension = last - first + 1;
        if (!isfinite(dimension) || dimension < 1 || dimension > UINT32_MAX)
            return light_error(error, QA_ERROR_FORMAT, face, "lightmap dimensions overflow");
        dimensions[axis] = (uint32_t)dimension;
        for (size_t i = 0; i < 3; ++i) light->projection[axis][i] = info->projection[axis][i] / step;
        light->projection[axis][3] = (float)((double)info->projection[axis][3] / step - first);
        light->light_step[axis] = step;
        if (!isfinite(light->projection[axis][3]))
            return light_error(error, QA_ERROR_FORMAT, face, "lightmap projection overflows");
    }
    light->width = dimensions[0];
    light->height = dimensions[1];
    return true;
}

bool qawl_light_setup(qa_scene_world *world, qaw_surface *surface, const qa_bsp_face *face,
                      const qa_bsp_texinfo *info, const qa_vec3 *points, size_t count, qa_error *error)
{
    qaw_legacy *light = surface->legacy;
    const qawl_world *data = world->legacy_data;
    const qa_bsp_q1_metadata *metadata = &data->metadata;
    size_t index = surface->source_index;
    bool q1 = world->bsp.family == QA_BSP_Q1;
    qa_scene_q1_lightmap_encoding encoding = world->options.q1_lightmap_encoding;
    if (q1 && (encoding < QA_Q1_LIGHTMAP_RGB || encoding > QA_Q1_LIGHTMAP_INVERTED_ALPHA))
        return light_error(error, QA_ERROR_ARGUMENT, index, "invalid Q1 lightmap encoding");
    light->sample_offset = SIZE_MAX;
    if (!face_styles(light, metadata, face, index, error)) return false;
    int64_t offset = face->lighting_offset;
    if (metadata->offsets.data != NULL) {
        uint32_t value = qa_load_u32le(metadata->offsets.data + index * 4);
        offset = value == UINT32_MAX ? -1 : (int64_t)value;
    }
    qa_bsp_extension extension;
    if (qa_bsp_find_extension(&world->bsp, "DECOUPLED_LM", &extension, NULL)) {
        qa_bsp_decoupled_lightmap mapping;
        if (!qa_bsp_read_decoupled_lightmap(&world->bsp, index, &mapping, error)) return false;
        offset = mapping.lighting_offset;
        /* Some compilers emit empty records for sky and other unlit faces. */
        if (mapping.width != 0 && mapping.height != 0) {
            float lengths[2];
            for (size_t axis = 0; axis < 2; ++axis)
                lengths[axis] = hypotf(hypotf(mapping.projection[axis][0], mapping.projection[axis][1]), mapping.projection[axis][2]);
            if (lengths[0] > 0 && lengths[1] > 0 && isfinite(lengths[0]) && isfinite(lengths[1])
                && isfinite(1 / lengths[0]) && isfinite(1 / lengths[1])) {
                light->decoupled = true;
                light->width = mapping.width;
                light->height = mapping.height;
                memcpy(light->projection, mapping.projection, sizeof(light->projection));
                light->light_step[0] = 1 / lengths[0];
                light->light_step[1] = 1 / lengths[1];
            } else if (offset >= 0) {
                return light_error(error, QA_ERROR_FORMAT, index, "degenerate decoupled lightmap axes");
            }
        } else if (offset >= 0) {
            return light_error(error, QA_ERROR_FORMAT, index, "empty decoupled lightmap has samples");
        }
    }
    if (!light->decoupled) {
        float step = metadata->shifts.data != NULL ? ldexpf(1, metadata->shifts.data[index]) : 16;
        if (!classic_projection(light, info, points, count, step, index, error)) return false;
    }
    if (surface->sky || light->warp || offset < 0 || world->lighting.sample_count == 0) return true;
    if ((uint64_t)offset > SIZE_MAX || light->width > SIZE_MAX / light->height)
        return light_error(error, QA_ERROR_FORMAT, index, "lightmap sample range overflows");
    size_t pixels = (size_t)light->width * light->height;
    size_t start = (size_t)offset;
    size_t available = world->bsp.family == QA_BSP_Q2 ? world->lighting.samples.size : world->lighting.sample_count;
    size_t channels = world->bsp.family == QA_BSP_Q2 ? 3 : 1;
    if (start > available || light->style_count > ((available - start) / channels) / pixels)
        return light_error(error, QA_ERROR_FORMAT, index, "lightmap samples exceed lighting lump");
    if (pixels > SIZE_MAX / (3 * sizeof(float)) || pixels > SIZE_MAX / (3 * sizeof(uint32_t))
        || pixels > SIZE_MAX / 4 || light->style_count > (SIZE_MAX / sizeof(float) - 1) / 3)
        return light_error(error, QA_ERROR_MEMORY, index, "lightmap allocation overflows");
    light->sample_offset = start;
    light->light_pixels = malloc(pixels * 4);
    bool separate_encoding = !q1 || encoding != QA_Q1_LIGHTMAP_RGB;
    if (separate_encoding) light->encoded_pixels = malloc(pixels * 4);
    light->light_accumulation = malloc(pixels * 3 * (q1 ? sizeof(uint32_t) : sizeof(float)));
    light->cached_styles = malloc((light->style_count * 3 + 1) * sizeof(float));
    if (light->light_pixels == NULL || (separate_encoding && light->encoded_pixels == NULL)
        || light->light_accumulation == NULL || light->cached_styles == NULL)
        return light_error(error, QA_ERROR_MEMORY, index, "cannot allocate lightmap storage");
    light->lightmapped = true;
    return qawl_light_update(world, surface, &(qa_material_context){0}, &(qa_scene_world_input){0}, error);
}

static qa_vec3 face_style(const qa_scene_world *world, const qa_scene_world_input *input, uint16_t index)
{
    if (world->bsp.family == QA_BSP_Q1) {
        float value = input->q1_styles != NULL && index < input->style_count ? input->q1_styles[index] : 256;
        return qa_v3(value, value, value);
    }
    return input->q2_styles != NULL && index < input->style_count ? input->q2_styles[index] : qa_v3(1, 1, 1);
}

static bool face_sample(const qa_scene_world *world, const qaw_legacy *light,
                        size_t sample, uint8_t rgb[3], qa_error *error)
{
    /* Q1 offsets count samples, including .lit and packed Quake64 data.
     * Q2 offsets count bytes and need not be aligned to a triplet. */
    if (world->bsp.family == QA_BSP_Q2) {
        memcpy(rgb, world->lighting.samples.data + light->sample_offset + sample * 3, 3);
        return true;
    }
    return qa_bsp_light_sample(&world->lighting, light->sample_offset + sample, rgb, error);
}

static bool dynamic_lights(qaw_surface *surface, const qa_material_context *context, bool q1, qa_error *error)
{
    qaw_legacy *light = surface->legacy;
    for (size_t i = 0; i < context->light_count; ++i) {
        const qa_scene_light *source = &context->lights[i];
        if (!qa_vec_finite(source->origin) || !qa_vec_finite(source->color)
            || !isfinite(source->radius) || !isfinite(source->minimum))
            return light_error(error, QA_ERROR_ARGUMENT, i, "nonfinite dynamic light");
        float distance = qa_vec_dot(source->origin, surface->plane.normal) - surface->plane.distance;
        float radius = source->radius - fabsf(distance);
        if (radius < source->minimum) continue;
        double threshold = (double)radius - source->minimum;
        qa_vec3 impact = qa_vec_sub(source->origin, qa_vec_scale(surface->plane.normal, distance));
        float s = project(light->projection[0], impact), t = project(light->projection[1], impact);
        if (!isfinite(s) || !isfinite(t) || !isfinite(radius))
            return light_error(error, QA_ERROR_ARGUMENT, i, "dynamic light projection overflows");
        for (uint32_t y = 0; y < light->height; ++y) {
            double td = fabs(trunc(((double)t - y) * light->light_step[1]));
            for (uint32_t x = 0; x < light->width; ++x) {
                double sd = fabs(trunc(((double)s - x) * light->light_step[0]));
                double separation = fmax(sd, td) + floor(fmin(sd, td) / 2);
                if (separation >= threshold) continue;
                double amount = ((double)radius - separation) * (q1 ? 256 : 1);
                size_t at = ((size_t)y * light->width + x) * 3;
                if (q1) {
                    uint32_t *block = light->light_accumulation;
                    block[at] = light_u32(block[at] + amount * source->color.x);
                    block[at + 1] = light_u32(block[at + 1] + amount * source->color.y);
                    block[at + 2] = light_u32(block[at + 2] + amount * source->color.z);
                } else {
                    float *block = light->light_accumulation;
                    block[at] = (float)(block[at] + amount * source->color.x);
                    block[at + 1] = (float)(block[at + 1] + amount * source->color.y);
                    block[at + 2] = (float)(block[at + 2] + amount * source->color.z);
                }
            }
        }
    }
    return true;
}

static bool light_image(qa_scene_world *world, const qaw_surface *surface,
                        const qa_scene_image *previous, const uint8_t *pixels, const char *suffix,
                        qa_scene_image **out, qa_error *error)
{
    const qaw_legacy *light = surface->legacy;
    qa_scene_image_level level = {light->width, light->height, pixels, (size_t)light->width * light->height * 4};
    if (previous != NULL) return qa_scene_image_replace(world->resources, previous, 0, &level, out, error);
    char name[96];
    (void)snprintf(name, sizeof(name), "*world-%" PRIu64 "-light-%" PRIu32 "%s", world->identity, surface->source_index, suffix);
    return qa_scene_image_create(world->resources, name, QA_SCENE_RGBA8, &level, 1,
        QA_SCENE_CLAMP, QA_SCENE_LINEAR, (qa_scene_vec4){0, 0, 0, 1}, out, error);
}

bool qawl_light_update(qa_scene_world *world, qaw_surface *surface, const qa_material_context *context,
                       const qa_scene_world_input *input, qa_error *error)
{
    qaw_legacy *light = surface->legacy;
    if (!light->lightmapped) return true;
    if (input->legacy_policy.present && !input->legacy_policy.dynamic && light->light_cache_valid) return true;
    bool q1 = world->bsp.family == QA_BSP_Q1;
    qa_material_context baked = *context;
    if (input->legacy_flashblend || (!q1 && input->shadow_lights != NULL)) baked.light_count = 0;
    context = &baked;
    float modulate = q1 ? 1 : input->legacy_policy.present ? input->legacy_policy.modulate : world->options.q2_light_modulate;
    uint8_t mono = !q1 && input->legacy_policy.present ? input->legacy_policy.monolightmap : '0';
    if (!isfinite(modulate)) return light_error(error, QA_ERROR_ARGUMENT, surface->source_index, "nonfinite light modulation");
    bool changed = !light->light_cache_valid || light->light_cache_dynamic || context->light_count != 0;
    if (!changed && light->cached_styles[0] != modulate) changed = true;
    if (!changed && light->light_cache_monolightmap != mono) changed = true;
    for (size_t i = 0; i < light->style_count; ++i) {
        qa_vec3 style = face_style(world, input, light->styles[i]);
        if (!qa_vec_finite(style)) return light_error(error, QA_ERROR_ARGUMENT, light->styles[i], "nonfinite lightstyle");
        if (!changed && (light->cached_styles[i * 3 + 1] != style.x
            || light->cached_styles[i * 3 + 2] != style.y || light->cached_styles[i * 3 + 3] != style.z)) changed = true;
    }
    if (!changed) return true;
    light->light_cache_valid = false;
    size_t pixels = (size_t)light->width * light->height;
    memset(light->light_accumulation, 0, pixels * 3 * (q1 ? sizeof(uint32_t) : sizeof(float)));
    for (size_t i = 0; i < light->style_count; ++i) {
        qa_vec3 style = face_style(world, input, light->styles[i]);
        float scales[3] = {style.x * modulate, style.y * modulate, style.z * modulate};
        for (size_t pixel = 0; pixel < pixels; ++pixel) {
            uint8_t sample[3];
            if (!face_sample(world, light, i * pixels + pixel, sample, error)) return false;
            for (size_t channel = 0; channel < 3; ++channel) {
                size_t at = pixel * 3 + channel;
                if (q1) {
                    uint32_t *block = light->light_accumulation;
                    block[at] = light_u32(block[at] + (double)sample[channel] * scales[channel]);
                } else {
                    float *block = light->light_accumulation;
                    block[at] += sample[channel] * scales[channel];
                }
            }
        }
    }
    if (!dynamic_lights(surface, context, q1, error)) return false;
    for (size_t pixel = 0; pixel < pixels; ++pixel) {
        if (q1) {
            const uint32_t *block = light->light_accumulation;
            for (size_t channel = 0; channel < 3; ++channel) {
                uint32_t value = block[pixel * 3 + channel] >> 7;
                light->light_pixels[pixel * 4 + channel] = (uint8_t)(value > 255 ? 255 : value);
            }
            if (light->encoded_pixels != NULL) {
                uint8_t brightness = light->light_pixels[pixel * 4];
                uint8_t inverse = (uint8_t)(255 - brightness);
                bool alpha = world->options.q1_lightmap_encoding == QA_Q1_LIGHTMAP_INVERTED_ALPHA;
                for (size_t channel = 0; channel < 3; ++channel) {
                    light->encoded_pixels[pixel * 4 + channel] = alpha ? 255 : inverse;
                    /* Source inverted encodings carry luminance in channel 0. */
                    light->light_pixels[pixel * 4 + channel] = brightness;
                }
                light->encoded_pixels[pixel * 4 + 3] = alpha ? inverse : 255;
            }
        } else {
            const float *block = light->light_accumulation;
            float values[3], maximum = 0;
            for (size_t channel = 0; channel < 3; ++channel) {
                if (!isfinite(block[pixel * 3 + channel]))
                    return light_error(error, QA_ERROR_ARGUMENT, surface->source_index, "lightmap accumulation overflows");
                values[channel] = fmaxf(0, truncf(block[pixel * 3 + channel]));
                maximum = fmaxf(maximum, values[channel]);
            }
            float scale = maximum > 255 ? 255 / maximum : 1;
            for (size_t channel = 0; channel < 3; ++channel) {
                uint8_t value = (uint8_t)fminf(255, truncf(values[channel] * scale));
                light->light_pixels[pixel * 4 + channel] = value;
                light->encoded_pixels[pixel * 4 + channel] = value;
            }
            light->encoded_pixels[pixel * 4 + 3] = (uint8_t)fminf(255, truncf(maximum * scale));
            if (mono != '0') {
                uint8_t *encoded = light->encoded_pixels + pixel * 4;
                uint8_t *direct = light->light_pixels + pixel * 4;
                uint8_t brightness = encoded[3];
                if (mono == 'L' || mono == 'I') {
                    encoded[0] = brightness; encoded[1] = encoded[2] = 0;
                } else if (mono == 'C') {
                    uint8_t alpha = (uint8_t)(255 - ((unsigned)encoded[0] + encoded[1] + encoded[2]) / 3);
                    for (size_t channel = 0; channel < 3; ++channel)
                        encoded[channel] = (uint8_t)(encoded[channel] * (float)alpha / 255);
                    encoded[3] = alpha;
                } else {
                    encoded[0] = encoded[1] = encoded[2] = 0;
                    encoded[3] = (uint8_t)(255 - brightness);
                    direct[0] = direct[1] = direct[2] = brightness;
                }
                uint8_t format = mono;
                if (format >= 'a' && format <= 'z') format -= 'a' - 'A';
                if (format == 'L' || format == 'I') {
                    /* Internal format selection uppercases the setting;
                     * R_BuildLightMap's store switch uses its original byte. */
                    uint8_t luminance = encoded[0];
                    encoded[0] = encoded[1] = encoded[2] = luminance;
                    direct[0] = direct[1] = direct[2] = luminance;
                    encoded[3] = format == 'I' ? luminance : 255;
                } else if (format != 'A' && format != 'C') encoded[3] = 255;
            }
        }
        light->light_pixels[pixel * 4 + 3] = 255;
    }
    qa_scene_image *replacement = NULL, *direct = NULL;
    const uint8_t *encoded = light->encoded_pixels != NULL ? light->encoded_pixels : light->light_pixels;
    if (!light_image(world, surface, surface->lightmap, encoded, "", &replacement, error)) return false;
    if (light->encoded_pixels == NULL) {
        direct = replacement;
        qa_scene_image_retain(direct);
    } else {
        if (!light_image(world, surface, light->direct_lightmap, light->light_pixels, "-direct", &direct, error)) {
            qa_scene_image_release(replacement);
            return false;
        }
    }
    /* Neither current image is released until both replacements exist. */
    qa_scene_image_release(surface->lightmap);
    qa_scene_image_release(light->direct_lightmap);
    surface->lightmap = replacement;
    light->direct_lightmap = direct;
    light->cached_styles[0] = modulate;
    for (size_t i = 0; i < light->style_count; ++i) {
        qa_vec3 style = face_style(world, input, light->styles[i]);
        light->cached_styles[i * 3 + 1] = style.x;
        light->cached_styles[i * 3 + 2] = style.y;
        light->cached_styles[i * 3 + 3] = style.z;
    }
    light->light_cache_dynamic = context->light_count != 0;
    light->light_cache_monolightmap = mono;
    light->light_cache_valid = true;
    return true;
}

void qawl_light_destroy(qaw_legacy *light)
{
    if (light == NULL) return;
    free(light->styles);
    free(light->cached_styles);
    free(light->light_pixels);
    free(light->encoded_pixels);
    qa_scene_image_release(light->direct_lightmap);
    free(light->light_accumulation);
    light->styles = NULL;
    light->cached_styles = NULL;
    light->light_pixels = NULL;
    light->encoded_pixels = NULL;
    light->direct_lightmap = NULL;
    light->light_accumulation = NULL;
    light->style_count = 0;
    light->lightmapped = light->light_cache_valid = light->light_cache_dynamic = false;
}

static qa_vec3 snapshot_style(const qa_scene_world *world,const qa_scene_world_input *input, uint16_t index)
{
    if (input) return face_style(world,input,index);
    const qawl_world *data = world->legacy_data;
    if (world->bsp.family == QA_BSP_Q1) {
        float value = index < data->style_count ? data->q1_styles[index] : 256;
        return qa_v3(value, value, value);
    }
    return index < data->style_count ? data->q2_styles[index] : qa_v3(1, 1, 1);
}

static bool sample_surface(const qa_scene_world *world, const qaw_surface *surface,
                           const qa_scene_world_input *input,qa_vec3 point, qa_vec3 *color)
{
    const qaw_legacy *light = surface->legacy;
    if (light == NULL || surface->sky || light->warp) return false;
    float uv[2] = {project(light->projection[0], point), project(light->projection[1], point)};
    if (!light->decoupled) {
        qa_bsp_face face;
        qa_bsp_texinfo info;
        if (!qa_bsp_read_face(&world->bsp, surface->source_index, &face, NULL)
            || !qa_bsp_read_texinfo(&world->bsp, face.texinfo, &info, NULL)) return false;
        for (size_t axis = 0; axis < 2; ++axis) {
            float coordinate = project(info.projection[axis], point);
            /* Truncate the original texture coordinate before texturemins,
             * particularly when a negatively positioned face crosses zero. */
            double minimum = round((double)info.projection[axis][3] / light->light_step[axis]
                                   - light->projection[axis][3]);
            uv[axis] = (float)(trunc(coordinate) / light->light_step[axis] - minimum);
        }
    }
    if (!isfinite(uv[0]) || !isfinite(uv[1]) || uv[0] < 0 || uv[1] < 0
        || (double)uv[0] > (double)light->width - 1 || (double)uv[1] > (double)light->height - 1) return false;
    *color = qa_v3(0, 0, 0);
    if (light->sample_offset == SIZE_MAX) return true;
    size_t pixels = (size_t)light->width * light->height;
    size_t pixel = (size_t)uv[1] * light->width + (size_t)uv[0];
    double sum[3] = {0, 0, 0};
    for (size_t i = 0; i < light->style_count; ++i) {
        uint8_t sample[3];
        if (!face_sample(world, light, i * pixels + pixel, sample, NULL)) return false;
        qa_vec3 style = snapshot_style(world,input, light->styles[i]);
        sum[0] += sample[0] * (double)style.x;
        sum[1] += sample[1] * (double)style.y;
        sum[2] += sample[2] * (double)style.z;
    }
    if (world->bsp.family == QA_BSP_Q1) {
        *color = qa_v3((float)(light_u32(sum[0]) >> 8) / 255,
                       (float)(light_u32(sum[1]) >> 8) / 255,
                       (float)(light_u32(sum[2]) >> 8) / 255);
    } else {
        *color = qa_v3((float)(sum[0] / 255), (float)(sum[1] / 255), (float)(sum[2] / 255));
        *color = qa_vec_scale(*color, input && input->legacy_policy.present ?
            input->legacy_policy.modulate : world->options.q2_light_modulate);
    }
    return true;
}

typedef struct light_trace_frame {
    uint32_t node;
    int32_t far_child;
    qa_vec3 middle, end;
} light_trace_frame;

static bool trace_floor(const qa_scene_world *world,const qa_scene_world_input *input, qa_vec3 point, qa_vec3 *color,
                         qa_vec3 *hit, bool *found)
{
    if (found) *found = false;
    *color = qa_v3(0, 0, 0);
    if (world->model_count == 0 || world->node_count == 0) return true;
    light_trace_frame local[64], *stack = local;
    size_t capacity = sizeof(local) / sizeof(local[0]), depth = 0;
    qa_vec3 start = point, end = qa_vec_add(point, qa_v3(0, 0, -2048));
    int32_t child = world->models[0].source.headnodes[0];
    bool valid = true, done = false;
    while (!done) {
        while (child >= 0) {
            if ((size_t)child >= world->node_count) { valid = false; break; }
            const qa_bsp_node *node = &world->nodes[child];
            if (node->plane >= world->plane_count) { valid = false; break; }
            const qa_bsp_plane *plane = &world->planes[node->plane];
            float front = qa_vec_dot(start, plane->normal) - plane->distance;
            float back = qa_vec_dot(end, plane->normal) - plane->distance;
            if (!isfinite(front) || !isfinite(back)) { valid = false; break; }
            unsigned side = front < 0 ? 1u : 0u;
            if ((back < 0 ? 1u : 0u) == side) { child = node->children[side]; continue; }
            qa_vec3 middle = qa_vec_lerp(start, end, front / (front - back));
            if (depth == capacity) {
                if (capacity > SIZE_MAX / 2 / sizeof(*stack)) { valid = false; break; }
                size_t next_capacity = capacity * 2;
                light_trace_frame *replacement = malloc(next_capacity * sizeof(*replacement));
                if (replacement == NULL) { valid = false; break; }
                memcpy(replacement, stack, depth * sizeof(*stack));
                if (stack != local) free(stack);
                stack = replacement;
                capacity = next_capacity;
            }
            stack[depth++] = (light_trace_frame){(uint32_t)child, node->children[side ^ 1u], middle, end};
            child = node->children[side];
            end = middle;
        }
        if (!valid || depth == 0) break;
        light_trace_frame frame = stack[--depth];
        const qa_bsp_node *node = &world->nodes[frame.node];
        if (node->faces.first > world->surface_count || node->faces.count > world->surface_count - node->faces.first) {
            valid = false;
            break;
        }
        for (size_t i = 0; i < node->faces.count; ++i) {
            if (sample_surface(world, &world->surfaces[node->faces.first + i],input, frame.middle, color)) {
                if (hit) *hit = frame.middle;
                if (found) *found = true;
                done = true;
                break;
            }
        }
        child = frame.far_child;
        start = frame.middle;
        end = frame.end;
    }
    if (stack != local) free(stack);
    return valid;
}

static qa_vec3 grid_interpolate(qa_vec3 a, qa_vec3 b, float fraction)
{
    return qa_vec_add(qa_vec_scale(a, 1 - fraction), qa_vec_scale(b, fraction));
}

static bool sample_grid(const qa_scene_world *world,const qa_scene_world_input *input, qa_vec3 position, qa_vec3 *color)
{
    const qa_bsp_lightgrid *grid = &world->lightgrid;
    if (grid->leaf_count == 0) return false;
    qa_vec3 point = qa_vec_sub(position, grid->min);
    point.x *= 1 / grid->spacing.x;
    point.y *= 1 / grid->spacing.y;
    point.z *= 1 / grid->spacing.z;
    if (!qa_vec_finite(point)) return false;
    uint32_t base[3] = {light_u32(point.x), light_u32(point.y), light_u32(point.z)};
    qa_vec3 corners[8], average = qa_v3(0, 0, 0);
    bool valid[8] = {false};
    unsigned count = 0;
    for (unsigned i = 0; i < 8; ++i) {
        int64_t coordinates[3] = {(uint32_t)(base[0] + (i & 1u)),
            (uint32_t)(base[1] + ((i >> 1) & 1u)), (uint32_t)(base[2] + ((i >> 2) & 1u))};
        const qa_bsp_lightgrid_sample *samples = qa_bsp_lightgrid_lookup(grid, coordinates);
        corners[i] = qa_v3(0, 0, 0);
        if (samples != NULL) for (size_t style = 0; style < grid->style_count; ++style) {
            const qa_bsp_lightgrid_sample *sample = &samples[style];
            if (sample->style == UINT8_MAX) break;
            /* q2repro grid styles use monochrome intensity, not Q2's RGB sum. */
            float intensity = snapshot_style(world,input, sample->style).x;
            corners[i] = qa_vec_add(corners[i], qa_vec_scale(qa_v3(sample->rgb[0], sample->rgb[1], sample->rgb[2]), intensity));
            valid[i] = true;
        }
        if (valid[i]) { average = qa_vec_add(average, corners[i]); ++count; }
    }
    if (count == 0) return false;
    average = qa_vec_scale(average, 1.0f / (float)count);
    for (size_t i = 0; i < 8; ++i) if (!valid[i]) corners[i] = average;
    float fx = point.x - (float)base[0], fy = point.y - (float)base[1], fz = point.z - (float)base[2];
    qa_vec3 bottom = grid_interpolate(grid_interpolate(corners[0], corners[1], fx), grid_interpolate(corners[2], corners[3], fx), fy);
    qa_vec3 top = grid_interpolate(grid_interpolate(corners[4], corners[5], fx), grid_interpolate(corners[6], corners[7], fx), fy);
    qa_vec3 result = qa_vec_scale(grid_interpolate(bottom, top, fz),
        input && input->legacy_policy.present ? input->legacy_policy.modulate : world->options.q2_light_modulate);
    *color = qa_vec_scale(qa_v3(fmaxf(0, result.x), fmaxf(0, result.y), fmaxf(0, result.z)), 1.0f / 255);
    return true;
}

bool qaw_sample_legacy_light(const qa_scene_world *world, qa_vec3 point,
                             qa_vec3 *ambient, qa_vec3 *directed, qa_vec3 *direction)
{
    *directed = qa_v3(0, 0, 0);
    *direction = qa_v3(0, 0, 1);
    if (world->lighting.sample_count == 0) {
        *ambient = qa_v3(1, 1, 1);
        return true;
    }
    if (world->bsp.family == QA_BSP_Q2 && sample_grid(world,NULL, point, ambient)) return true;
    return trace_floor(world,NULL, point, ambient, NULL, NULL);
}

bool qa_scene_world_sample_light_input(const qa_scene_world *world,const qa_scene_world_input *input,
    qa_vec3 point,qa_vec3 *ambient,qa_vec3 *directed,qa_vec3 *direction,qa_error *error)
{
    if (!world || !input || !ambient || !directed || !direction || !qa_vec_finite(point))
        return light_error(error,QA_ERROR_ARGUMENT,0,"Point lighting requires its actual world, lightstyles and finite position");
    if (world->bsp.family==QA_BSP_Q3)
        return qa_scene_world_sample_light(world,point,ambient,directed,direction);
    for (size_t i=0;i<input->style_count;++i)
        if (world->bsp.family==QA_BSP_Q1?
            (input->q1_styles && !isfinite(input->q1_styles[i])):
            (input->q2_styles && !qa_vec_finite(input->q2_styles[i])))
            return light_error(error,QA_ERROR_ARGUMENT,i,"Point lighting received a nonfinite entered lightstyle");
    *directed=qa_v3(0,0,0); *direction=qa_v3(0,0,1);
    if (!world->lighting.sample_count) { *ambient=qa_v3(1,1,1); return true; }
    if (world->bsp.family==QA_BSP_Q2 && sample_grid(world,input,point,ambient)) return true;
    return trace_floor(world,input,point,ambient,NULL,NULL);
}

bool qa_scene_world_sample_floor(const qa_scene_world *world, qa_vec3 origin,
                                  qa_vec3 *point, bool *found)
{
    if (!world || !point || !found || !qa_vec_finite(origin)) return false;
    *found = false;
    if (world->bsp.family == QA_BSP_Q3 || world->lighting.sample_count == 0) return true;
    qa_vec3 color;
    return trace_floor(world,NULL, origin, &color, point, found);
}
