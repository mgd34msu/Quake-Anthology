#include "internal.h"
#include "qa/text.h"
#include "qa/font_world_save.h"
#include "qa/source_save.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct stored_text {
    qa_font_world_text value;
    uint8_t *bytes;
    double expires;
    uint64_t first_frame;
    bool one_frame, observed;
} stored_text;

struct qa_font_world_store {
    stored_text *entries;
    size_t count, capacity;
};

static bool world_save_fields(qa_source_save_io *io, stored_text *entry,
    const qa_font_world_checkpoint_refs *refs)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_font_world_text *text = &entry->value;
    uint32_t orientation = text->orientation, font = text->font;
    uint64_t content = 0;
    if (!reading && !refs->content_encode(refs->context, text->content, &content, io->error)) return false;
    if (!qa_source_save_u32(io, &orientation) || orientation > QA_FONT_WORLD_FIXED ||
        !qa_source_save_u32(io, &font) || font > QA_FONT_WORLD_SELECTED ||
        !qa_source_save_vec3(io, &text->origin) || !qa_source_save_vec3(io, &text->angles) ||
        !qa_source_save_f32(io, &text->color.x) || !qa_source_save_f32(io, &text->color.y) ||
        !qa_source_save_f32(io, &text->color.z) || !qa_source_save_f32(io, &text->color.w) ||
        !qa_source_save_f32(io, &text->cell_size) || !qa_source_save_f32(io, &text->distance_cull_factor) ||
        !qa_source_save_bool(io, &text->depth_test) || !qa_source_save_bool(io, &text->has_distance_cull) ||
        !qa_source_save_u64(io, &content) || !qa_source_save_f64(io, &entry->expires) ||
        !qa_source_save_u64(io, &entry->first_frame) || !qa_source_save_bool(io, &entry->one_frame) ||
        !qa_source_save_bool(io, &entry->observed)) return false;
    if (reading) {
        text->orientation = (qa_font_world_orientation)orientation;
        text->font = (qa_font_world_source)font;
        if (!refs->content_decode(refs->context, content, &text->content, io->error)) return false;
    }
    if (!qa_vec_finite(text->origin) || (orientation == QA_FONT_WORLD_FIXED && !qa_vec_finite(text->angles)) ||
        !isfinite(text->color.x) || !isfinite(text->color.y) || !isfinite(text->color.z) || !isfinite(text->color.w) ||
        !isfinite(text->cell_size) || text->cell_size <= 0 ||
        (text->has_distance_cull && !isfinite(text->distance_cull_factor)) || !isfinite(entry->expires))
        return qa_font_fail(io->error, QA_ERROR_FORMAT, io->offset, "Saved world text changes actual submission fields");
    size_t length = reading ? 0 : text->text.size;
    if (!qa_source_save_count(io, &length, reading ? io->input.size - io->offset : SIZE_MAX)) return false;
    if (reading && length) {
        entry->bytes = malloc(length);
        if (!entry->bytes) return qa_font_fail(io->error, QA_ERROR_MEMORY, io->offset, "Retaining saved world text bytes");
    }
    if (!qa_source_save_bytes(io, reading ? entry->bytes : (void *)text->text.data, length)) return false;
    if (reading) text->text = (qa_bytes){entry->bytes, length};
    return true;
}
static bool world_save_header(qa_source_save_io *io, size_t *count)
{
    uint8_t magic[4] = {'Q','W','T','X'}; uint32_t version = 1;
    size_t maximum = SIZE_MAX / sizeof(stored_text);
    if (io->direction == QA_SOURCE_SAVE_READ && io->input.size / 91 < maximum) maximum = io->input.size / 91;
    return qa_source_save_bytes(io, magic, 4) && !memcmp(magic, "QWTX", 4) &&
        qa_source_save_u32(io, &version) && version == 1 && qa_source_save_count(io, count, maximum);
}
bool qa_font_world_store_checkpoint(const qa_font_world_store *store,
    const qa_font_world_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!store || !refs || !refs->content_encode || !out || out->data || out->size ||
        store->count > store->capacity || (store->count && !store->entries))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "World text checkpoint requires actual retained rows and empty output");
    qa_source_save_io io = {0}; size_t count = store->count;
    bool ok = qa_source_save_writer(&io, NULL, error) && world_save_header(&io, &count);
    for (size_t i = 0; ok && i < count; ++i) {
        stored_text entry = store->entries[i];
        if ((entry.value.text.size && !entry.value.text.data) || entry.value.text.data != entry.bytes) {
            ok = qa_font_fail(error, QA_ERROR_FORMAT, i, "World text checkpoint lacks its actual owned text");
            break;
        }
        ok = world_save_fields(&io, &entry, refs);
    }
    ok = ok && qa_source_save_finish(&io, out); qa_source_save_dispose(&io);
    if (!ok && (!error || error->code == QA_OK)) qa_font_fail(error, QA_ERROR_FORMAT, 0, "World text checkpoint is invalid");
    return ok;
}
bool qa_font_world_store_restore(qa_font_world_store *store, qa_bytes bytes,
    const qa_font_world_checkpoint_refs *refs, qa_error *error)
{
    if (!store || !refs || !refs->content_decode)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "World text restore requires actual qualified content owners");
    qa_font_world_store candidate = {0}; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && world_save_header(&io, &candidate.count);
    candidate.capacity = candidate.count;
    if (ok && candidate.count) {
        candidate.entries = calloc(candidate.count, sizeof(*candidate.entries));
        if (!candidate.entries) ok = qa_font_fail(error, QA_ERROR_MEMORY, 0, "Preparing world text continuation rows");
    }
    for (size_t i = 0; ok && i < candidate.count; ++i) ok = world_save_fields(&io, candidate.entries + i, refs);
    ok = ok && qa_source_save_finish(&io, NULL); qa_source_save_dispose(&io);
    if (ok) {
        qa_font_world_store previous = *store; *store = candidate; candidate = previous;
    }
    if (candidate.entries) for (size_t i = 0; i < candidate.count; ++i) free(candidate.entries[i].bytes);
    free(candidate.entries);
    if (!ok && (!error || error->code == QA_OK)) qa_font_fail(error, QA_ERROR_FORMAT, 0, "Saved world text continuation is invalid");
    return ok;
}

static bool finite_color(qa_scene_vec4 value) {
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z) && isfinite(value.w);
}

qa_font_world_store *qa_font_world_store_create(qa_error *error) {
    qa_font_world_store *store = calloc(1, sizeof(*store));
    if (!store)
        qa_font_fail(error, QA_ERROR_MEMORY, 0, "Allocating world text store");
    return store;
}

void qa_font_world_store_clear(qa_font_world_store *store) {
    if (!store)
        return;
    for (size_t i = 0; i < store->count; ++i)
        free(store->entries[i].bytes);
    store->count = 0;
}

void qa_font_world_store_destroy(qa_font_world_store *store) {
    if (!store)
        return;
    qa_font_world_store_clear(store);
    free(store->entries);
    free(store);
}

static void prune_seconds(qa_font_world_store *store, double now) {
    size_t kept = 0;
    for (size_t i = 0; i < store->count; ++i) {
        stored_text entry = store->entries[i];
        if (!entry.one_frame && entry.expires <= now) {
            free(entry.bytes);
            continue;
        }
        store->entries[kept++] = entry;
    }
    store->count = kept;
}

bool qa_font_world_store_submit(qa_font_world_store *store, const qa_font_world_text *text,
                                double now_seconds, double lifetime_seconds, qa_error *error) {
    if (!store || !text || (!text->text.data && text->text.size) || !isfinite(now_seconds) ||
        !isfinite(lifetime_seconds) || lifetime_seconds < 0 || !qa_vec_finite(text->origin) ||
        (text->orientation == QA_FONT_WORLD_FIXED && !qa_vec_finite(text->angles)) ||
        !finite_color(text->color) || !isfinite(text->cell_size) || text->cell_size <= 0 ||
        (text->has_distance_cull && !isfinite(text->distance_cull_factor)) ||
        text->orientation < QA_FONT_WORLD_BILLBOARD || text->orientation > QA_FONT_WORLD_FIXED ||
        text->font < QA_FONT_WORLD_CLASSIC || text->font > QA_FONT_WORLD_SELECTED)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid world text submission");
    double expires = now_seconds + lifetime_seconds;
    if (lifetime_seconds > 0 && !isfinite(expires))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "World text expiration overflows");
    uint8_t *copy = NULL;
    if (text->text.size) {
        copy = malloc(text->text.size);
        if (!copy)
            return qa_font_fail(error, QA_ERROR_MEMORY, 0, "Copying world text");
        memcpy(copy, text->text.data, text->text.size);
    }
    prune_seconds(store, now_seconds);
    if (store->count == store->capacity) {
        size_t next = store->capacity ? store->capacity * 2 : 16;
        if (next < store->capacity || next > SIZE_MAX / sizeof(*store->entries)) {
            free(copy);
            return qa_font_fail(error, QA_ERROR_MEMORY, 0, "World text count overflow");
        }
        stored_text *grown = realloc(store->entries, next * sizeof(*store->entries));
        if (!grown) {
            free(copy);
            return qa_font_fail(error, QA_ERROR_MEMORY, 0, "Allocating world text entries");
        }
        store->entries = grown;
        store->capacity = next;
    }
    stored_text entry = {0};
    entry.bytes = copy;
    entry.value = *text;
    entry.value.text = (qa_bytes){copy, text->text.size};
    entry.one_frame = lifetime_seconds == 0;
    entry.expires = expires;
    store->entries[store->count++] = entry;
    return true;
}

bool qa_font_world_store_snapshot(qa_font_world_store *store, double now_seconds, uint64_t frame,
                                  qa_arena *arena, qa_font_world_snapshot *out, qa_error *error) {
    if (!store || !arena || !out || !isfinite(now_seconds))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid world text snapshot");
    prune_seconds(store, now_seconds);
    qa_font_world_text *values = NULL;
    if (store->count) {
        values = qa_arena_alloc(arena, store->count * sizeof(*values), _Alignof(qa_font_world_text),
                                error);
        if (!values)
            return false;
    }
    size_t kept = 0;
    for (size_t i = 0; i < store->count; ++i) {
        stored_text entry = store->entries[i];
        if (entry.one_frame && entry.observed && entry.first_frame != frame) {
            free(entry.bytes);
            continue;
        }
        if (entry.one_frame && !entry.observed) {
            entry.observed = true;
            entry.first_frame = frame;
        }
        store->entries[kept] = entry;
        values[kept++] = entry.value;
    }
    store->count = kept;
    if (!kept) {
        *out = (qa_font_world_snapshot){0};
        return true;
    }
    *out = (qa_font_world_snapshot){values, kept};
    return true;
}

static void fixed_axis(qa_vec3 angles, qa_vec3 axis[3]) {
    const float radians = 0.01745329251994329577f;
    float sy = sinf(angles.y * radians), cy = cosf(angles.y * radians);
    float sp = sinf(angles.x * radians), cp = cosf(angles.x * radians);
    float sr = sinf(angles.z * radians), cr = cosf(angles.z * radians);
    axis[0] = qa_v3(cp * cy, cp * sy, -sp);
    axis[1] = qa_v3(sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, sr * cp);
    axis[2] = qa_v3(cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp);
}

static bool world_quad(qa_scene_frame *frame, const qa_scene_view *view, const qa_font_glyph *glyph,
                       qa_scene_vec4 color, qa_vec3 origin, qa_vec3 right, qa_vec3 down,
                       float column, float row, bool depth_test, qa_error *error) {
    qa_scene_vertex *vertices =
        qa_arena_alloc(&frame->storage, 4 * sizeof(*vertices), _Alignof(qa_scene_vertex), error);
    uint32_t *indices =
        qa_arena_alloc(&frame->storage, 6 * sizeof(*indices), _Alignof(uint32_t), error);
    if (!vertices || !indices)
        return false;
    qa_vec3 base =
        qa_vec_add(origin, qa_vec_add(qa_vec_scale(right, column), qa_vec_scale(down, row)));
    qa_vec3 positions[4] = {
        base,
        qa_vec_add(base, right),
        qa_vec_add(qa_vec_add(base, right), down),
        qa_vec_add(base, down),
    };
    qa_scene_vec2 uv[4] = {
        {glyph->uv.x, glyph->uv.y},
        {glyph->uv.z, glyph->uv.y},
        {glyph->uv.z, glyph->uv.w},
        {glyph->uv.x, glyph->uv.w},
    };
    qa_vec3 normal = qa_vec_scale(view->axis[0], -1);
    for (size_t i = 0; i < 4; ++i)
        vertices[i] = (qa_scene_vertex){.position = positions[i],
                                        .normal = normal,
                                        .texcoord = uv[i],
                                        .lightmap = uv[i],
                                        .color = color};
    const uint32_t order[6] = {0, 1, 2, 0, 2, 3};
    memcpy(indices, order, sizeof(order));
    qa_bounds bounds = {positions[0], positions[0]};
    for (size_t i = 1; i < 4; ++i)
        bounds = qa_bounds_union(bounds, (qa_bounds){positions[i], positions[i]});
    qa_scene_draw draw = {0};
    draw.mesh = (qa_scene_mesh){.vertices = vertices,
                                .indices = indices,
                                .vertex_count = 4,
                                .index_count = 6,
                                .bounds = bounds,
                                .primitive = QA_SCENE_TRIANGLES};
    qa_scene_matrix_identity(&draw.model);
    draw.mvp = qa_scene_matrix_multiply(view->projection, qa_scene_view_matrix(view));
    draw.textures[0] = glyph->image;
    draw.texture_count = 1;
    draw.environment = QA_TEXTURE_MODULATE;
    draw.lighting = QA_LIGHT_VERTEX;
    qa_scene_state_default(&draw.state);
    draw.state.blend_source = QA_BLEND_SRC_ALPHA;
    draw.state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
    draw.state.depth_test = depth_test ? QA_DEPTH_LEQUAL : QA_DEPTH_ALWAYS;
    draw.state.depth_write = false;
    draw.state.cull = QA_CULL_NONE;
    return qa_scene_frame_draw(frame, &draw, error);
}

static size_t line_end_and_count(qa_bytes text, size_t begin, size_t *characters, bool *nul) {
    size_t at = begin, count = 0;
    *nul = false;
    while (at < text.size) {
        size_t next = at;
        uint32_t codepoint;
        if (!qa_utf8_next(text, &next, &codepoint))
            break;
        if (!codepoint) {
            *nul = true;
            break;
        }
        if (codepoint == '\n')
            break;
        at = next;
        ++count;
    }
    *characters = count;
    return at;
}

bool qa_font_world_draw(qa_scene_frame *frame, const qa_scene_view *view,
                        const qa_font_world_snapshot *snapshot, const qa_font_selection *selection,
                        float distance_cull_override, bool has_distance_cull_override,
                        qa_error *error) {
    if (!frame || !view || !snapshot || !selection || !selection->classic ||
        (snapshot->count && !snapshot->texts) || view->seat != selection->seat ||
        (has_distance_cull_override && !isfinite(distance_cull_override)))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid world text draw request");
    qa_font_selection classic = {.seat = selection->seat, .classic = selection->classic};
    for (size_t index = 0; index < snapshot->count; ++index) {
        const qa_font_world_text *text = &snapshot->texts[index];
        if ((!text->text.data && text->text.size) || !qa_vec_finite(text->origin) ||
            (text->orientation == QA_FONT_WORLD_FIXED && !qa_vec_finite(text->angles)) ||
            !finite_color(text->color) || !isfinite(text->cell_size) || text->cell_size <= 0 ||
            (text->has_distance_cull && !isfinite(text->distance_cull_factor)) ||
            text->orientation < QA_FONT_WORLD_BILLBOARD ||
            text->orientation > QA_FONT_WORLD_FIXED || text->font < QA_FONT_WORLD_CLASSIC ||
            text->font > QA_FONT_WORLD_SELECTED)
            return qa_font_fail(error, QA_ERROR_ARGUMENT, index,
                                "Invalid world text snapshot entry");
        float depth = qa_vec_dot(qa_vec_sub(text->origin, view->origin), view->axis[0]);
        bool cull = text->has_distance_cull;
        float factor =
            has_distance_cull_override ? distance_cull_override : text->distance_cull_factor;
        if (cull && text->cell_size < depth * factor)
            continue;
        const qa_font_selection *font = text->font == QA_FONT_WORLD_CLASSIC ? &classic : selection;
        qa_vec3 axis[3];
        if (text->orientation == QA_FONT_WORLD_BILLBOARD) {
            axis[0] = view->axis[0];
            axis[1] = view->axis[1];
            axis[2] = view->axis[2];
        } else {
            fixed_axis(text->angles, axis);
        }
        qa_vec3 right = qa_vec_scale(axis[1], -text->cell_size);
        qa_vec3 down = qa_vec_scale(axis[2], -text->cell_size);
        size_t line_begin = 0;
        float row = 0;
        while (line_begin <= text->text.size) {
            size_t count = 0;
            bool nul = false;
            size_t line_end = line_end_and_count(text->text, line_begin, &count, &nul);
            size_t at = line_begin, column = 0;
            while (at < line_end) {
                size_t source_offset = at;
                uint32_t codepoint;
                if (!qa_utf8_next(text->text, &at, &codepoint))
                    break;
                qa_font_glyph glyph;
                if (!qa_font_resolve(font, codepoint, false, &glyph))
                    return qa_font_fail(error, QA_ERROR_FORMAT, source_offset,
                                        "World font cannot resolve replacement glyph");
                if (glyph.visible && glyph.image &&
                    !world_quad(frame, view, &glyph, text->color, text->origin, right, down,
                                (float)column - (float)count * 0.5f, row, text->depth_test, error))
                    return false;
                ++column;
            }
            if (nul || line_end == text->text.size)
                break;
            line_begin = line_end;
            uint32_t newline;
            if (!qa_utf8_next(text->text, &line_begin, &newline))
                break;
            row += 1.0f;
        }
    }
    return true;
}
