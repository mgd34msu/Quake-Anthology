#include "internal.h"
#include "qa/id_index.h"
#include "qa/text.h"
#include <stdarg.h>
#include <stdio.h>

const char *const qa_player_animation_names[QA_PLAYER_ANIMATION_COUNT] = {
    "BOTH_DEATH1",    "BOTH_DEAD1",      "BOTH_DEATH2",  "BOTH_DEAD2",     "BOTH_DEATH3",
    "BOTH_DEAD3",     "TORSO_GESTURE",   "TORSO_ATTACK", "TORSO_ATTACK2",  "TORSO_DROP",
    "TORSO_RAISE",    "TORSO_STAND",     "TORSO_STAND2", "LEGS_WALKCR",    "LEGS_WALK",
    "LEGS_RUN",       "LEGS_BACK",       "LEGS_SWIM",    "LEGS_JUMP",      "LEGS_LAND",
    "LEGS_JUMPB",     "LEGS_LANDB",      "LEGS_IDLE",    "LEGS_IDLECR",    "LEGS_TURN",
    "TORSO_GETFLAG",  "TORSO_GUARDBASE", "TORSO_PATROL", "TORSO_FOLLOWME", "TORSO_AFFIRMATIVE",
    "TORSO_NEGATIVE", "MAX_ANIMATIONS",  "LEGS_BACKCR",  "LEGS_BACKWALK",  "FLAG_RUN",
    "FLAG_STAND",     "FLAG_STAND2RUN"};
static uint8_t lower(uint8_t c) { return c >= 'A' && c <= 'Z' ? (uint8_t)(c + ('a' - 'A')) : c; }
static bool animation_diagnostic(qa_common_print callback, void *context, qa_error *error,
                                 const char *format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (length < 0 || (size_t)length >= sizeof(message)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "CG_Printf exceeds its source buffer");
        return false;
    }
    return !callback || callback(context, (qa_bytes){(const uint8_t *)message, (size_t)length}, error);
}
bool qa_player_animation_load(qa_bytes text, const char *source_path,
                              qa_player_animation_config *out, bool *loaded,
                              qa_common_print diagnostic, void *context, qa_error *error) {
    if (!out || !loaded || !source_path || (!text.data && text.size)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "animation text, source path and outputs are required");
        return false;
    }
    *loaded = false;
    if (!text.size)
        return true;
    if (text.size > 19998)
        return animation_diagnostic(diagnostic, context, error, "File %s too long\n", source_path);
    qa_common_parser parser = {0};
    qa_common_cursor cursor;
    if (!qa_common_cursor_init(&cursor, text, QA_COMMON_TERMINATED, error))
        return false;
    qa_player_animation_config config = {0};
    for (;;) {
        qa_common_cursor_state previous = qa_common_cursor_capture(&cursor);
        if (!qa_common_parse(&parser, &cursor, true, error))
            return false;
        const char *token = parser.token;
        if (qa_text_ascii_equal(token, "footsteps")) {
            if (!qa_common_parse(&parser, &cursor, true, error))
                return false;
            const char *names[] = {"normal", "boot", "flesh", "mech", "energy"};
            bool known = qa_text_ascii_equal(token, "default");
            if (known)
                config.footsteps = QA_FOOTSTEP_NORMAL;
            for (unsigned i = 0; i < 5; ++i)
                if (qa_text_ascii_equal(token, names[i])) {
                    config.footsteps = (qa_model_footstep)i;
                    known = true;
                    break;
                }
            if (!known && !animation_diagnostic(diagnostic, context, error,
                    "Bad footsteps parm in %s: %s\n", source_path, token))
                return false;
        } else if (qa_text_ascii_equal(token, "headoffset")) {
            for (unsigned i = 0; i < 3; ++i) {
                if (!qa_common_parse(&parser, &cursor, true, error))
                    return false;
                config.head_offset[i] = (float)qa_parse_quake_number(parser.token, QA_QUAKE_NUMBER_Q3_BYTECODE);
            }
        } else if (qa_text_ascii_equal(token, "sex")) {
            if (!qa_common_parse(&parser, &cursor, true, error))
                return false;
            uint8_t first = lower((uint8_t)*token);
            config.gender = first == 'f' ? QA_MODEL_FEMALE : first == 'n' ? QA_MODEL_NEUTER : QA_MODEL_MALE;
        } else if (qa_text_ascii_equal(token, "fixedlegs"))
            config.fixed_legs = true;
        else if (qa_text_ascii_equal(token, "fixedtorso"))
            config.fixed_torso = true;
        else {
            if (*token >= '0' && *token <= '9') {
                if (!qa_common_cursor_restore(&cursor, previous, error))
                    return false;
                break;
            }
            if (!animation_diagnostic(diagnostic, context, error,
                    "unknown token '%s' is %s\n", token, source_path))
                return false;
            if (previous.offset == cursor.offset && previous.ended == cursor.ended) {
                qa_error_set(error, QA_ERROR_FORMAT, 0,
                             "CG animation prelude reached the source nonprogress cycle");
                return false;
            }
        }
    }
    int32_t skip = 0;
    unsigned i = 0;
    for (; i < 31; ++i) {
        qa_player_animation *a = &config.animations[i];
        if (!qa_common_parse(&parser, &cursor, true, error))
            return false;
        if (!*parser.token) {
            if (i >= 25) {
                *a = config.animations[6];
                a->reversed = a->flipflop = false;
                a->present = true;
                continue;
            }
            break;
        }
        a->first_frame = qa_parse_decimal_i32(parser.token, QA_DECIMAL_SIGNED_WRAP);
        if (i == 13)
            skip = qa_i32_from_u32((uint32_t)a->first_frame - (uint32_t)config.animations[6].first_frame);
        if (i >= 13 && i < 25)
            a->first_frame = qa_i32_from_u32((uint32_t)a->first_frame - (uint32_t)skip);
        if (!qa_common_parse(&parser, &cursor, true, error))
            return false;
        if (!*parser.token)
            break;
        a->num_frames = qa_parse_decimal_i32(parser.token, QA_DECIMAL_SIGNED_WRAP);
        if (a->num_frames < 0) {
            a->num_frames = qa_i32_from_u32(0u - (uint32_t)a->num_frames);
            a->reversed = true;
        }
        if (!qa_common_parse(&parser, &cursor, true, error))
            return false;
        if (!*parser.token)
            break;
        a->loop_frames = qa_parse_decimal_i32(parser.token, QA_DECIMAL_SIGNED_WRAP);
        if (!qa_common_parse(&parser, &cursor, true, error))
            return false;
        if (!*parser.token)
            break;
        float fps = (float)qa_parse_quake_number(parser.token, QA_QUAKE_NUMBER_Q3_BYTECODE);
        if (fps == 0)
            fps = 1;
        float lerp = 1000.0f / fps;
        a->frame_lerp = a->initial_lerp = qa_source_float_to_i32(lerp);
        a->present = true;
    }
    if (i != 31)
        return !diagnostic || animation_diagnostic(diagnostic, context, error,
                                                   "Error parsing animation file: %s", source_path);
    config.animations[32] = config.animations[13];
    config.animations[32].reversed = true;
    config.animations[33] = config.animations[14];
    config.animations[33].reversed = true;
    config.animations[34] = (qa_player_animation){0, 16, 16, 66, 66, false, false, true};
    config.animations[35] = (qa_player_animation){16, 5, 0, 50, 50, false, false, true};
    config.animations[36] = (qa_player_animation){16, 5, 1, 66, 66, true, false, true};
    *out = config;
    *loaded = true;
    return true;
}
static bool skin_space(uint8_t c) { return c <= 32 || c >= 128; }
static bool skin_token(model_reader *r, char out[1025], bool *present) {
    for (;;) {
        while (r->pos < r->bytes.size && skin_space(r->bytes.data[r->pos]))
            ++r->pos;
        if (r->bytes.size - r->pos >= 2 && r->bytes.data[r->pos] == '/' &&
            r->bytes.data[r->pos + 1] == '/') {
            while (r->pos < r->bytes.size && r->bytes.data[r->pos] != '\n')
                ++r->pos;
            continue;
        }
        if (r->bytes.size - r->pos >= 2 && r->bytes.data[r->pos] == '/' &&
            r->bytes.data[r->pos + 1] == '*') {
            r->pos += 2;
            while (r->bytes.size - r->pos >= 2 &&
                   !(r->bytes.data[r->pos] == '*' && r->bytes.data[r->pos + 1] == '/'))
                ++r->pos;
            if (r->bytes.size - r->pos >= 2)
                r->pos += 2;
            else
                r->pos = r->bytes.size;
            continue;
        }
        break;
    }
    *present = r->pos < r->bytes.size;
    out[0] = '\0';
    if (!*present)
        return true;
    uint8_t first = r->bytes.data[r->pos++];
    size_t length = 0;
    if (first == '"') {
        while (r->pos < r->bytes.size && r->bytes.data[r->pos] != '"') {
            if (length == 1023)
                return model_fail(r, "quoted skin token exceeds source capacity");
            out[length++] = (char)r->bytes.data[r->pos++];
        }
        if (r->pos < r->bytes.size)
            ++r->pos;
    } else {
        out[length++] = (char)first;
        while (r->pos < r->bytes.size && r->bytes.data[r->pos] != ',' &&
               !skin_space(r->bytes.data[r->pos])) {
            if (length < 1024)
                out[length++] = (char)r->bytes.data[r->pos];
            ++r->pos;
        }
        if (length == 1024)
            length = 0;
    }
    out[length] = '\0';
    return true;
}
struct qa_model_skin_cache {
    qa_arena storage;
    qa_id_index models;
    size_t **bindings;
    size_t capacity;
};
bool qa_model_skin_map_prepare(qa_model_skin_map *map, qa_error *error) {
    if (map->cache || !map->count) return true;
    map->cache = calloc(1, sizeof(*map->cache));
    if (map->cache) {
        qa_arena_init(&map->cache->storage, 0);
        map->cache->models.storage = &map->cache->storage;
        return true;
    }
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining skin mesh bindings"); return false;
}
bool qa_model_skin_map_resolve(const qa_model_skin_map *map, uint64_t model,
    const qa_string_id *surfaces, size_t stride, size_t count, const size_t **indexes, qa_error *error) {
    *indexes = NULL;
    if (!map->count || !count) return true;
    struct qa_model_skin_cache *cache = map->cache;
    qa_id_index_entry *entry = qa_id_index_find(&cache->models, 0, model);
    if (entry) { *indexes = cache->bindings[entry->value]; return true; }
    if (count > SIZE_MAX / sizeof(size_t)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Skin mesh binding extent overflows"); return false;
    }
    if (!qa_id_index_reserve(&cache->models, 1, error)) return false;
    if (cache->capacity < cache->models.capacity) {
        size_t **bindings = qa_arena_grow(&cache->storage, cache->bindings,
            cache->capacity * sizeof(*cache->bindings),
            cache->models.capacity * sizeof(*cache->bindings), _Alignof(size_t *), error);
        if (!bindings) return false;
        cache->bindings = bindings; cache->capacity = cache->models.capacity;
    }
    size_t *binding = qa_arena_alloc(&cache->storage, count * sizeof(*binding),
        _Alignof(size_t), error);
    if (!binding) return false;
    for (size_t mesh = 0; mesh < count; ++mesh) {
        qa_string_id surface;
        memcpy(&surface, (const uint8_t *)surfaces + mesh * stride, sizeof(surface));
        binding[mesh] = SIZE_MAX;
        for (size_t i = 0; i < map->count; ++i)
            if (map->mappings[i].surface == surface) { binding[mesh] = i; break; }
    }
    size_t slot = cache->models.count;
    cache->bindings[slot] = binding;
    qa_id_index_insert(&cache->models, 0, model, slot);
    *indexes = binding; return true;
}
void qa_model_skin_map_free(qa_model_skin_map *map) {
    if (!map)
        return;
    for (size_t i = 0; i < map->count; ++i)
        free(map->mappings[i].shader);
    free(map->mappings);
    if (map->cache) {
        qa_id_index_destroy(&map->cache->models);
        qa_arena_destroy(&map->cache->storage);
        free(map->cache);
    }
    qa_strings_destroy(map->strings);
    memset(map, 0, sizeof(*map));
}
bool qa_model_skin_map_load(qa_bytes text, qa_strings *strings, qa_model_skin_map *out, qa_error *error) {
    if (!out || !strings || (!text.data && text.size)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "skin text and output are required");
        return false;
    }
    if (text.size) {
        const uint8_t *nul = memchr(text.data, 0, text.size);
        if (nul)
            text.size = (size_t)(nul - text.data);
    }
    model_reader r = {text, 0, error, true};
    qa_model_skin_map map = {.strings = strings};
    qa_strings_retain(strings);
    size_t capacity = 0;
    for (;;) {
        char name[1025], shader[1025];
        bool present;
        if (!skin_token(&r, name, &present))
            goto fail;
        if (!present || !name[0])
            break;
        if (r.pos < r.bytes.size && r.bytes.data[r.pos] == ',')
            ++r.pos;
        if (strstr(name, "tag_"))
            continue;
        if (!skin_token(&r, shader, &present))
            goto fail;
        if (map.count == capacity) {
            size_t grown = capacity ? capacity * 2 : 8;
            if (grown < capacity) {
                model_fail(&r, "skin map size overflows");
                goto fail;
            }
            map.mappings = model_grow(&r, map.mappings, capacity, grown, sizeof(*map.mappings));
            if (!r.ok)
                goto fail;
            capacity = grown;
        }
        qa_model_skin_mapping *entry = &map.mappings[map.count++];
        char surface[64];
        size_t length = strlen(name);
        if (length > 63)
            length = 63;
        for (size_t i = 0; i < length; ++i)
            surface[i] = (char)lower((uint8_t)name[i]);
        surface[length] = '\0';
        if (!qa_strings_intern_cstr(strings, surface, &entry->surface, error)) goto fail;
        if (!qa_strings_intern_cstr(strings, shader, &entry->shader_id, error)) goto fail;
        size_t shader_length = strlen(shader);
        entry->shader = model_alloc(&r, shader_length + 1, 1);
        if (!r.ok)
            goto fail;
        memcpy(entry->shader, shader, shader_length + 1);
    }
    map.capacity = capacity;
    if (!qa_model_skin_map_prepare(&map, error)) goto fail;
    *out = map;
    return true;
fail:
    qa_model_skin_map_free(&map);
    return false;
}
static char *join_path(const char *path, size_t prefix, const char *middle, const char *suffix,
                       qa_error *error) {
    size_t mid = strlen(middle), tail = strlen(suffix);
    if (prefix > SIZE_MAX - mid || prefix + mid > SIZE_MAX - tail - 1) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "model path is too long");
        return NULL;
    }
    size_t length = prefix + mid + tail;
    char *result = malloc(length + 1);
    if (!result) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "model path allocation failed");
        return NULL;
    }
    memcpy(result, path, prefix);
    memcpy(result + prefix, middle, mid);
    memcpy(result + prefix + mid, suffix, tail + 1);
    return result;
}
void qa_model_replacement_paths_free(qa_model_replacement_paths *paths) {
    if (!paths)
        return;
    free(paths->mesh);
    free(paths->animation);
    free(paths->scales);
    memset(paths, 0, sizeof(*paths));
}
bool qa_model_md5_paths(const char *path, bool q2, qa_model_replacement_paths *out,
                        qa_error *error) {
    if (!path || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "model path and output are required");
        return false;
    }
    const char *slash = strrchr(path, '/'), *filename = slash ? slash + 1 : path,
               *dot = strrchr(filename, '.');
    size_t stem = dot ? (size_t)(dot - path) : strlen(path), directory = (size_t)(filename - path);
    char *base = join_path(path, directory, q2 ? "md5/" : "", filename, error);
    if (!base)
        return false;
    size_t length = stem + (q2 ? 4u : 0u);
    base[length] = '\0';
    qa_model_replacement_paths paths = {0};
    paths.mesh = join_path(base, length, "", ".md5mesh", error);
    paths.animation = join_path(base, length, "", ".md5anim", error);
    paths.scales = join_path(base, length, "", ".md5scale", error);
    free(base);
    if (!paths.mesh || !paths.animation || !paths.scales) {
        qa_model_replacement_paths_free(&paths);
        return false;
    }
    *out = paths;
    return true;
}
bool qa_model_md5_skin_path(const char *path, char **out, qa_error *error) {
    if (!path || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "skin path and output are required");
        return false;
    }
    const char *slash = strrchr(path, '/'), *filename = slash ? slash + 1 : path;
    char *result = join_path(path, (size_t)(filename - path), "md5/", filename, error);
    if (!result)
        return false;
    *out = result;
    return true;
}
bool qa_model_q1_skin_path(const char *shader, uint32_t group, uint32_t frame, char **out,
                           qa_error *error) {
    if (!shader || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "shader and output are required");
        return false;
    }
    char suffix[48];
    int n = snprintf(suffix, sizeof(suffix), "_%02u_%02u", (unsigned)group, (unsigned)frame);
    if (n < 0 || (size_t)n >= sizeof(suffix)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "skin suffix exceeds capacity");
        return false;
    }
    char *base = join_path("progs/", 6, shader, "", error);
    if (!base)
        return false;
    char *result = join_path(base, strlen(base), "", suffix, error);
    free(base);
    if (!result)
        return false;
    *out = result;
    return true;
}
bool qa_model_md5_replacement_allowed(int64_t alias_rank, int64_t mesh_rank) {
    return alias_rank < 0 || mesh_rank < 0 || mesh_rank <= alias_rank;
}
bool qa_model_q1_replacement_uses_time(uint32_t source_frames, uint32_t md5_frames) {
    return source_frames != md5_frames;
}

bool qa_model_replacement_init(const qa_model *source, const qa_model *mesh,
                               const qa_model_animation *animation, qa_model_replacement *out,
                               qa_error *error) {
    if (!source || !mesh || !animation || !out || mesh->format != QA_MODEL_MD5 ||
        (source->format != QA_MODEL_MDL && source->format != QA_MODEL_MD2) ||
        mesh->bone_count != animation->joint_count || !animation->frame_count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "incompatible model replacement resources");
        return false;
    }
    *out = (qa_model_replacement){
        source, mesh, animation, source->format == QA_MODEL_MDL ? source->flags : 0,
        source->format == QA_MODEL_MDL && source->frame_group_count != animation->frame_count};
    return true;
}
bool qa_model_replacement_skin(const qa_model_replacement *replacement, uint32_t mesh,
                               uint32_t skin, double seconds, double sync_base, char **out,
                               qa_error *error) {
    if (!replacement || !replacement->source || !replacement->mesh || !out ||
        mesh >= replacement->mesh->mesh_count || !isfinite(seconds) || !isfinite(sync_base))
        goto invalid;
    const qa_model *source = replacement->source;
    if (source->format == QA_MODEL_MD2) {
        if (!source->skin_count)
            goto invalid;
        if (skin >= source->skin_count)
            skin = 0;
        return qa_model_md5_skin_path(source->skins[skin].name, out, error);
    }
    if (source->format != QA_MODEL_MDL || !source->skin_group_count ||
        !replacement->mesh->meshes[mesh].shader_count)
        goto invalid;
    if (skin >= source->skin_group_count)
        skin = 0;
    const qa_model_group *group = &source->skin_groups[skin];
    uint32_t frame = qa_model_group_sample(group, seconds, sync_base) - group->first;
    qa_bytes shader = qa_model_shader_name(&replacement->mesh->meshes[mesh].shaders[0]);
    if (shader.size == SIZE_MAX || memchr(shader.data, 0, shader.size))
        goto invalid;
    char *name = malloc(shader.size + 1);
    if (!name) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "replacement shader allocation failed");
        return false;
    }
    memcpy(name, shader.data, shader.size);
    name[shader.size] = '\0';
    bool ok = qa_model_q1_skin_path(name, skin, frame, out, error);
    free(name);
    return ok;
invalid:
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid replacement skin selection");
    return false;
}
uint32_t qa_model_replacement_frame(const qa_model_replacement *replacement, uint32_t entity_frame,
                                    double seconds, double sync_base) {
    if (!replacement || !replacement->source || !replacement->animation ||
        !replacement->animation->frame_count)
        return 0;
    uint32_t count = replacement->animation->frame_count;
    if (!replacement->elapsed_animation) {
        uint32_t source_count = replacement->source->format == QA_MODEL_MDL
                                    ? replacement->source->frame_group_count
                                : replacement->source->format == QA_MODEL_MD2
                                    ? replacement->source->frame_count
                                    : 0;
        return source_count && entity_frame < source_count ? entity_frame % count : 0;
    }
    if (!isfinite(seconds) || !isfinite(sync_base))
        return 0;
    double elapsed = seconds + sync_base;
    if (!isfinite(elapsed))
        return 0;
    double frame = fmod(floor(elapsed * 2), count);
    if (frame < 0)
        frame += count;
    return isfinite(frame) ? (uint32_t)frame : 0;
}
