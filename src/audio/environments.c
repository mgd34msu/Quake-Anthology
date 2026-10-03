#include "qa/audio.h"
#include "qa/json.h"
#include "qa/text.h"
#include "reverb_presets.h"
#include "checkpoint_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct material_reverb {
    qa_buffer *materials;
    size_t material_count, preset;
    bool wildcard;
} material_reverb;

typedef struct environment_group {
    double dimension;
    material_reverb *reverbs;
    size_t count;
} environment_group;

typedef struct environment_table {
    atomic_size_t references;
    environment_group *groups;
    size_t count;
} environment_table;

struct qa_audio_environments {
    environment_table *table;
};

enum { PROBE_COUNT = 14 };
struct qa_audio_environment {
    environment_table *table;
    qa_audio_trace_fn trace;
    void *user;
    size_t group, preset;
    unsigned probe;
    qa_vec3 results[PROBE_COUNT];
    double probe_time, lerp_start, lerp_end;
    float lerp_seconds;
    bool enabled;
    bool has_material;
    qa_buffer material_input, material_lower;
    qa_audio_reverb_params active, from, to;
};

static const qa_vec3 probes[PROBE_COUNT] = {{0, 0, -1},
                                            {0, 0, 1},
                                            {0.707106769f, 0, 0.707106769f},
                                            {0.353553385f, 0.612372458f, 0.707106769f},
                                            {-0.353553444f, 0.612372458f, 0.707106769f},
                                            {-0.707106769f, -6.18172393e-8f, 0.707106769f},
                                            {-0.353553325f, -0.612372518f, 0.707106769f},
                                            {0.353553355f, -0.612372458f, 0.707106769f},
                                            {1, 0, -4.37113883e-8f},
                                            {0.49999997f, 0.866025448f, -4.37113883e-8f},
                                            {-0.50000006f, 0.866025388f, -4.37113883e-8f},
                                            {-1, -8.74227766e-8f, -4.37113883e-8f},
                                            {-0.499999911f, -0.866025448f, -4.37113883e-8f},
                                            {0.499999911f, -0.866025448f, -4.37113883e-8f}};

size_t qa_audio_reverb_preset_count(void) {
    return sizeof(qa_reverb_presets) / sizeof(qa_reverb_presets[0]);
}

const char *qa_audio_reverb_preset_name(size_t index) {
    return index < qa_audio_reverb_preset_count() ? qa_reverb_presets[index].name : NULL;
}

const qa_audio_reverb_params *qa_audio_reverb_preset(size_t index) {
    return index < qa_audio_reverb_preset_count() ? &qa_reverb_presets[index].params : NULL;
}

static void table_release(environment_table *table) {
    if (!table || atomic_fetch_sub_explicit(&table->references, 1, memory_order_acq_rel) != 1)
        return;
    for (size_t i = 0; i < table->count; ++i) {
        environment_group *group = &table->groups[i];
        for (size_t j = 0; j < group->count; ++j) {
            material_reverb *reverb = &group->reverbs[j];
            for (size_t k = 0; k < reverb->material_count; ++k)
                qa_buffer_free(&reverb->materials[k]);
            free(reverb->materials);
        }
        free(group->reverbs);
    }
    free(table->groups);
    free(table);
}

static bool warn_missing_preset(const qa_json_document *document, qa_json_id preset,
                                qa_audio_log_fn warning, void *warning_user, qa_error *error) {
    if (!warning)
        return true;
    static const char prefix[] = "Missing sound environment preset ";
    /* The original JSON string retains escapes, including embedded NULs. */
    qa_bytes name = qa_json_source(document, preset);
    if (name.size > SIZE_MAX - sizeof(prefix)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Sound preset warning is too large");
        return false;
    }
    char *message = malloc(sizeof(prefix) + name.size);
    if (!message) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate sound preset warning");
        return false;
    }
    memcpy(message, prefix, sizeof(prefix) - 1);
    memcpy(message + sizeof(prefix) - 1, name.data, name.size);
    message[sizeof(prefix) - 1 + name.size] = '\0';
    warning(warning_user, message);
    free(message);
    return true;
}

static bool parse_reverb(const qa_json_document *document, qa_json_id id, material_reverb *reverb,
                         qa_audio_log_fn warning, void *warning_user, qa_error *error) {
    if (qa_json_type(document, id) != QA_JSON_OBJECT) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Sound reverb must be an object");
        return false;
    }
    reverb->wildcard = true;
    qa_json_id materials = qa_json_get(document, id, "materials");
    if (materials != QA_JSON_NONE) {
        qa_json_kind kind = qa_json_type(document, materials);
        if (kind == QA_JSON_STRING) {
            qa_buffer name = {0};
            if (!qa_json_string(document, materials, &name, error))
                return false;
            bool wildcard = name.size && name.data[0] == '*';
            qa_buffer_free(&name);
            if (!wildcard) {
                qa_error_set(error, QA_ERROR_FORMAT, 0, "Sound reverb wildcard must begin with *");
                return false;
            }
        } else if (kind == QA_JSON_ARRAY) {
            reverb->wildcard = false;
            size_t count = qa_json_size(document, materials);
            if (count > SIZE_MAX / sizeof(*reverb->materials)) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "Sound material array is too large");
                return false;
            }
            if (count) {
                reverb->materials = calloc(count, sizeof(*reverb->materials));
                if (!reverb->materials) {
                    qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate sound materials");
                    return false;
                }
                reverb->material_count = count;
            }
            for (size_t i = 0; i < count; ++i) {
                qa_json_id name = qa_json_at(document, materials, i);
                if (qa_json_type(document, name) != QA_JSON_STRING) {
                    qa_error_set(error, QA_ERROR_FORMAT, 0, "Sound material must be text");
                    return false;
                }
                qa_buffer text = {0};
                if (!qa_json_string(document, name, &text, error))
                    return false;
                bool lowered =
                    qa_utf8_lower((qa_bytes){text.data, text.size}, &reverb->materials[i], error);
                qa_buffer_free(&text);
                if (!lowered)
                    return false;
            }
        } else {
            qa_error_set(error, QA_ERROR_FORMAT, 0,
                         "Sound reverb materials must be an array or wildcard");
            return false;
        }
    }
    qa_json_id preset = qa_json_get(document, id, "preset");
    if (preset != QA_JSON_NONE) {
        if (qa_json_type(document, preset) != QA_JSON_STRING) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Sound reverb preset must be text");
            return false;
        }
        reverb->preset = QA_REVERB_PRESET_PLAIN;
        bool found = false;
        for (size_t i = 0; i < qa_audio_reverb_preset_count(); ++i) {
            if (qa_json_string_equal(document, preset, qa_reverb_presets[i].name)) {
                reverb->preset = i;
                found = true;
                break;
            }
        }
        if (!found && !warn_missing_preset(document, preset, warning, warning_user, error))
            return false;
    }
    return true;
}

static bool parse_group(const qa_json_document *document, qa_json_id id, environment_group *group,
                        qa_audio_log_fn warning, void *warning_user, qa_error *error) {
    if (qa_json_type(document, id) != QA_JSON_OBJECT) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Sound environment must be an object");
        return false;
    }
    qa_json_id dimension = qa_json_get(document, id, "dimension");
    if (dimension != QA_JSON_NONE && qa_json_type(document, dimension) != QA_JSON_NULL) {
        if (qa_json_type(document, dimension) != QA_JSON_NUMBER ||
            !qa_json_number(document, dimension, &group->dimension, error) ||
            !isfinite(group->dimension)) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid sound environment dimension");
            return false;
        }
    }
    qa_json_id reverbs = qa_json_get(document, id, "reverbs");
    if (reverbs == QA_JSON_NONE || qa_json_type(document, reverbs) == QA_JSON_NULL)
        return true;
    if (qa_json_type(document, reverbs) != QA_JSON_ARRAY) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Sound environment reverbs must be an array");
        return false;
    }
    size_t count = qa_json_size(document, reverbs);
    if (count > SIZE_MAX / sizeof(*group->reverbs)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Sound reverb array is too large");
        return false;
    }
    if (count) {
        group->reverbs = calloc(count, sizeof(*group->reverbs));
        if (!group->reverbs) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate sound reverbs");
            return false;
        }
        group->count = count;
    }
    for (size_t i = 0; i < count; ++i)
        if (!parse_reverb(document, qa_json_at(document, reverbs, i), &group->reverbs[i], warning,
                          warning_user, error))
            return false;
    return true;
}

bool qa_audio_environments_parse(qa_bytes json, qa_audio_environments **out, qa_error *error) {
    return qa_audio_environments_parse_ex(json, NULL, NULL, out, error);
}

bool qa_audio_environments_parse_ex(qa_bytes json, qa_audio_log_fn warning, void *warning_user,
                                    qa_audio_environments **out, qa_error *error) {
    if (!out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Sound environments require an output");
        return false;
    }
    qa_json_document *document = NULL;
    if (!qa_json_parse(json, &document, error))
        return false;
    qa_json_id root = qa_json_root(document);
    qa_json_id groups = qa_json_get(document, root, "environments");
    if (qa_json_type(document, root) != QA_JSON_OBJECT ||
        qa_json_type(document, groups) != QA_JSON_ARRAY) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Sound environments require an environments array");
        qa_json_destroy(document);
        return false;
    }
    qa_audio_environments *environments = calloc(1, sizeof(*environments));
    environment_table *table = calloc(1, sizeof(*table));
    if (!environments || !table) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate sound environments");
        free(table);
        free(environments);
        qa_json_destroy(document);
        return false;
    }
    environments->table = table;
    atomic_init(&table->references, 1);
    size_t count = qa_json_size(document, groups);
    if (count > SIZE_MAX / sizeof(*table->groups)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Sound environment array is too large");
        goto fail;
    }
    if (count) {
        table->groups = calloc(count, sizeof(*table->groups));
        if (!table->groups) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate sound environment groups");
            goto fail;
        }
        table->count = count;
    }
    for (size_t i = 0; i < count; ++i)
        if (!parse_group(document, qa_json_at(document, groups, i), &table->groups[i], warning,
                         warning_user, error))
            goto fail;
    qa_json_destroy(document);
    *out = environments;
    return true;

fail:
    qa_json_destroy(document);
    qa_audio_environments_destroy(environments);
    return false;
}

void qa_audio_environments_destroy(qa_audio_environments *environments) {
    if (!environments)
        return;
    table_release(environments->table);
    free(environments);
}

bool qa_audio_environment_create(const qa_audio_environments *environments, qa_audio_trace_fn trace,
                                 void *user, qa_audio_environment **out, qa_error *error) {
    if (!environments || !out || (!trace && environments->table->count)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid sound environment selector arguments");
        return false;
    }
    qa_audio_environment *environment = calloc(1, sizeof(*environment));
    if (!environment) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate sound environment selector");
        return false;
    }
    environment_table *table = environments->table;
    size_t references = atomic_load_explicit(&table->references, memory_order_relaxed);
    do {
        if (references == SIZE_MAX) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Too many sound environment references");
            free(environment);
            return false;
        }
    } while (!atomic_compare_exchange_weak_explicit(&table->references, &references, references + 1,
                                                    memory_order_relaxed, memory_order_relaxed));
    environment->table = table;
    environment->trace = trace;
    environment->user = user;
    environment->group = table->count ? table->count - 1 : 0;
    environment->preset = QA_REVERB_PRESET_PLAIN;
    environment->active = qa_reverb_presets[QA_REVERB_PRESET_PLAIN].params;
    environment->from = environment->active;
    environment->to = environment->active;
    environment->lerp_seconds = 3;
    environment->enabled = true;
    *out = environment;
    return true;
}

void qa_audio_environment_destroy(qa_audio_environment *environment) {
    if (!environment)
        return;
    qa_buffer_free(&environment->material_input);
    qa_buffer_free(&environment->material_lower);
    table_release(environment->table);
    free(environment);
}

void qa_audio_environment_enable(qa_audio_environment *environment, bool enabled) {
    if (environment)
        environment->enabled = enabled;
}

void qa_audio_environment_lerp(qa_audio_environment *environment, float seconds) {
    if (environment && isfinite(seconds))
        environment->lerp_seconds = fmaxf(0, seconds);
}

const qa_audio_reverb_params *qa_audio_environment_params(const qa_audio_environment *environment) {
    return environment && environment->enabled && environment->table->count ? &environment->active
                                                                            : NULL;
}

static bool cache_material(qa_audio_environment *environment, const char *material) {
    size_t length = strlen(material);
    if (environment->has_material && environment->material_input.size == length &&
        (!length || !memcmp(environment->material_input.data, material, length)))
        return true;
    if (length == SIZE_MAX)
        return false;
    qa_buffer input = {malloc(length + 1), length}, lower = {0};
    if (!input.data)
        return false;
    memcpy(input.data, material, length + 1);
    if (!qa_utf8_lower((qa_bytes){input.data, length}, &lower, NULL)) {
        qa_buffer_free(&input);
        return false;
    }
    qa_buffer_free(&environment->material_input);
    qa_buffer_free(&environment->material_lower);
    environment->material_input = input;
    environment->material_lower = lower;
    environment->has_material = true;
    return true;
}

static bool material_matches(const qa_buffer *name, const qa_buffer *material) {
    return name->size == material->size &&
           (!name->size || !memcmp(name->data, material->data, name->size));
}

static qa_audio_reverb_params interpolate(const qa_audio_reverb_params *a,
                                          const qa_audio_reverb_params *b, float f) {
    qa_audio_reverb_params result;
#define QA_LERP(field) result.field = a->field + f * (b->field - a->field)
    QA_LERP(density);
    QA_LERP(diffusion);
    QA_LERP(gain);
    QA_LERP(gain_hf);
    QA_LERP(gain_lf);
    QA_LERP(decay_time);
    QA_LERP(decay_hf_ratio);
    QA_LERP(decay_lf_ratio);
    QA_LERP(reflections_gain);
    QA_LERP(reflections_delay);
    QA_LERP(late_gain);
    QA_LERP(late_delay);
    QA_LERP(echo_time);
    QA_LERP(echo_depth);
    QA_LERP(modulation_time);
    QA_LERP(modulation_depth);
    QA_LERP(air_absorption_gain_hf);
    QA_LERP(hf_reference);
    QA_LERP(lf_reference);
    QA_LERP(room_rolloff);
#undef QA_LERP
    result.decay_hf_limit = f >= 0.5f ? b->decay_hf_limit : a->decay_hf_limit;
    return result;
}

void qa_audio_environment_update(qa_audio_environment *environment, qa_vec3 origin,
                                 double milliseconds) {
    if (!environment || !environment->table->count || !isfinite(milliseconds) ||
        !qa_vec_finite(origin))
        return;
    environment_table *table = environment->table;
    qa_vec3 zero = {0, 0, 0};
    if (milliseconds >= environment->probe_time) {
        environment->probe_time = milliseconds + 13;
        qa_vec3 end = qa_vec_add(origin, qa_vec_scale(probes[environment->probe], 8192));
        qa_audio_trace_hit hit = environment->trace(environment->user, origin, end, zero, zero);
        environment->results[environment->probe] = qa_vec_sub(hit.end, origin);
        if (environment->probe == 1 && hit.sky)
            environment->results[environment->probe].z += 4096;
        qa_vec3 mins = environment->results[0], maxs = mins;
        for (size_t i = 1; i < PROBE_COUNT; ++i) {
            qa_vec3 p = environment->results[i];
            mins.x = fminf(mins.x, p.x);
            maxs.x = fmaxf(maxs.x, p.x);
            mins.y = fminf(mins.y, p.y);
            maxs.y = fmaxf(maxs.y, p.y);
            mins.z = fminf(mins.z, p.z);
            maxs.z = fmaxf(maxs.z, p.z);
        }
        double average =
            ((double)maxs.x - mins.x + (double)maxs.y - mins.y + (double)maxs.z - mins.z) / 3;
        size_t index = environment->group;
        while (index < table->count - 1 && average > table->groups[index].dimension)
            ++index;
        if (index == environment->group)
            while (index > 0 && average < table->groups[index - 1].dimension)
                --index;
        environment->group = index;
        environment->probe = (environment->probe + 1) % PROBE_COUNT;
    }

    qa_vec3 start = qa_v3(origin.x, origin.y, origin.z + 1);
    qa_vec3 end = qa_v3(start.x, start.y, start.z - 256);
    qa_audio_trace_hit floor_hit =
        environment->trace(environment->user, start, end, qa_v3(-16, -16, 0), qa_v3(16, 16, 0));
    size_t selected = environment->preset;
    if (floor_hit.fraction >= 1 || floor_hit.sky) {
        selected = QA_REVERB_PRESET_PLAIN;
    } else {
        const environment_group *group = &table->groups[environment->group];
        if (floor_hit.material && !cache_material(environment, floor_hit.material))
            return;
        for (size_t i = 0; i < group->count; ++i) {
            const material_reverb *reverb = &group->reverbs[i];
            bool matches = reverb->wildcard;
            if (floor_hit.material) {
                for (size_t j = 0; !matches && j < reverb->material_count; ++j)
                    matches = material_matches(&reverb->materials[j], &environment->material_lower);
            }
            if (matches) {
                selected = reverb->preset;
                break;
            }
        }
    }
    if (selected != environment->preset) {
        environment->preset = selected;
        environment->from = environment->active;
        environment->to = qa_reverb_presets[selected].params;
        environment->lerp_start = milliseconds;
        environment->lerp_end = milliseconds + (double)environment->lerp_seconds * 1000;
    }
    if (milliseconds >= environment->lerp_end) {
        environment->active = environment->to;
    } else {
        double duration = environment->lerp_end - environment->lerp_start;
        double t = (milliseconds - environment->lerp_start) / duration;
        double inverse = 1 - t;
        float f = (float)fmin(1, fmax(0, 1 - inverse * inverse * inverse));
        environment->active = interpolate(&environment->from, &environment->to, f);
    }
}

bool qa_audio_environment_definition_checkpoint(const qa_audio_environment *environment,
                                                 qa_buffer *out, qa_error *error) {
    if (!environment || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Sound environment definition requires its owner");
        return false;
    }
    qa_ac_writer w = {.error = error};
    qa_ac_write(&w, "QAED", 4);
    qa_ac_u64(&w, environment->table->count);
    for (size_t i = 0; !w.failed && i < environment->table->count; ++i) {
        const environment_group *g = &environment->table->groups[i];
        qa_ac_double(&w, g->dimension); qa_ac_u64(&w, g->count);
        for (size_t j = 0; !w.failed && j < g->count; ++j) {
            const material_reverb *v = &g->reverbs[j];
            qa_ac_u64(&w, v->preset); qa_ac_u32(&w, v->wildcard); qa_ac_u64(&w, v->material_count);
            for (size_t k = 0; !w.failed && k < v->material_count; ++k)
                qa_ac_blob(&w, (qa_bytes){v->materials[k].data, v->materials[k].size});
        }
    }
    return qa_ac_finish(&w, out);
}
static size_t environment_count(qa_ac_reader *r, size_t item_size, size_t encoded_minimum) {
    uint64_t n = qa_ac_get64(r);
    if (n > SIZE_MAX / item_size || n > (r->bytes.size - r->offset) / encoded_minimum) {
        qa_ac_bad(r, "Sound environment table extent exceeds its record"); return 0;
    }
    return (size_t)n;
}
static void *environment_array(qa_ac_reader *r, size_t count, size_t size) {
    if (r->failed) return NULL;
    void *p = count ? calloc(count, size) : NULL;
    if (count && !p) {
        qa_error_set(r->error, QA_ERROR_MEMORY, r->offset, "Restoring sound environment table");
        r->failed = true;
    }
    return p;
}
static bool environment_buffer(qa_ac_reader *r, qa_buffer *buffer) {
    qa_bytes bytes;
    if (!qa_ac_getblob(r, &bytes)) return false;
    if (bytes.size == SIZE_MAX) return qa_ac_bad(r, "Sound material extent overflows storage");
    buffer->data = malloc(bytes.size + 1);
    if (!buffer->data) {
        qa_error_set(r->error, QA_ERROR_MEMORY, r->offset, "Restoring sound environment material");
        r->failed = true; return false;
    }
    if (bytes.size) memcpy(buffer->data, bytes.data, bytes.size);
    buffer->data[bytes.size] = 0; buffer->size = bytes.size; return true;
}
bool qa_audio_environments_restore(qa_bytes bytes, qa_audio_environments **out, qa_error *error) {
    if (!out || (!bytes.data && bytes.size)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid sound environment definition destination"); return false;
    }
    qa_ac_reader r = {.bytes = bytes, .error = error}; qa_bytes magic;
    if (!qa_ac_read(&r, 4, &magic) || memcmp(magic.data, "QAED", 4))
        return qa_ac_bad(&r, "Invalid sound environment definition header");
    qa_audio_environments *definitions = calloc(1, sizeof(*definitions));
    environment_table *table = calloc(1, sizeof(*table));
    if (!definitions || !table) {
        free(definitions); free(table);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring sound environment definition"); return false;
    }
    definitions->table = table; atomic_init(&table->references, 1);
    size_t count = environment_count(&r, sizeof(*table->groups), 16);
    table->groups = environment_array(&r, count, sizeof(*table->groups));
    if (!r.failed) table->count = count;
    for (size_t i = 0; !r.failed && i < table->count; ++i) {
        environment_group *g = &table->groups[i];
        g->dimension = qa_ac_getdouble(&r);
        count = environment_count(&r, sizeof(*g->reverbs), 20);
        g->reverbs = environment_array(&r, count, sizeof(*g->reverbs));
        if (!r.failed) g->count = count;
        for (size_t j = 0; !r.failed && j < g->count; ++j) {
            material_reverb *v = &g->reverbs[j]; uint64_t preset = qa_ac_get64(&r);
            if (preset >= qa_audio_reverb_preset_count()) { qa_ac_bad(&r, "Unknown sound reverb preset"); break; }
            v->preset = (size_t)preset; v->wildcard = qa_ac_bool(&r);
            count = environment_count(&r, sizeof(*v->materials), 8);
            v->materials = environment_array(&r, count, sizeof(*v->materials));
            if (!r.failed) v->material_count = count;
            for (size_t k = 0; !r.failed && k < v->material_count; ++k)
                (void)environment_buffer(&r, &v->materials[k]);
        }
    }
    if (!r.failed && r.offset != bytes.size) qa_ac_bad(&r, "Sound environment definition has trailing fields");
    if (r.failed) { qa_audio_environments_destroy(definitions); return false; }
    *out = definitions; return true;
}
#define QA_ENV_PARAM_FIELDS(F) \
    F(density) F(diffusion) F(gain) F(gain_hf) F(gain_lf) \
    F(decay_time) F(decay_hf_ratio) F(decay_lf_ratio) \
    F(reflections_gain) F(reflections_delay) F(late_gain) F(late_delay) \
    F(echo_time) F(echo_depth) F(modulation_time) F(modulation_depth) \
    F(air_absorption_gain_hf) F(hf_reference) F(lf_reference) F(room_rolloff)
static void put_environment_params(qa_ac_writer *w, const qa_audio_reverb_params *v) {
#define QA_ENV_PUT(field) qa_ac_float(w, v->field);
    QA_ENV_PARAM_FIELDS(QA_ENV_PUT)
#undef QA_ENV_PUT
    qa_ac_u32(w, v->decay_hf_limit);
}
static void get_environment_params(qa_ac_reader *r, qa_audio_reverb_params *v) {
#define QA_ENV_GET(field) v->field = qa_ac_getfloat(r);
    QA_ENV_PARAM_FIELDS(QA_ENV_GET)
#undef QA_ENV_GET
    v->decay_hf_limit = qa_ac_bool(r);
}
#undef QA_ENV_PARAM_FIELDS
bool qa_audio_environment_checkpoint(const qa_audio_environment *v, qa_buffer *out, qa_error *error) {
    if (!v || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Sound environment checkpoint requires its owner"); return false;
    }
    qa_ac_writer w = {.error = error};
    qa_ac_write(&w, "QAES", 4); qa_ac_u32(&w, v->trace != NULL);
    qa_ac_u64(&w, v->group); qa_ac_u64(&w, v->preset); qa_ac_u32(&w, v->probe);
    for (size_t i = 0; i < PROBE_COUNT; ++i) qa_ac_vec(&w, v->results[i]);
    qa_ac_double(&w, v->probe_time); qa_ac_double(&w, v->lerp_start); qa_ac_double(&w, v->lerp_end);
    qa_ac_float(&w, v->lerp_seconds); qa_ac_u32(&w, v->enabled); qa_ac_u32(&w, v->has_material);
    qa_ac_blob(&w, (qa_bytes){v->material_input.data, v->material_input.size});
    qa_ac_blob(&w, (qa_bytes){v->material_lower.data, v->material_lower.size});
    put_environment_params(&w, &v->active); put_environment_params(&w, &v->from); put_environment_params(&w, &v->to);
    return qa_ac_finish(&w, out);
}
bool qa_audio_environment_restore(qa_bytes bytes, const qa_audio_environments *definitions,
                                   qa_audio_trace_fn trace, void *user, qa_audio_environment **out,
                                   qa_error *error) {
    if (!definitions || !out || (!bytes.data && bytes.size)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid candidate sound environment"); return false;
    }
    qa_ac_reader r = {.bytes = bytes, .error = error}; qa_bytes magic;
    if (!qa_ac_read(&r, 4, &magic) || memcmp(magic.data, "QAES", 4) ||
        qa_ac_bool(&r) != (trace != NULL)) return qa_ac_bad(&r, "Sound environment trace admission differs");
    qa_audio_environment *v = NULL;
    if (r.failed || !qa_audio_environment_create(definitions, trace, user, &v, error)) return false;
    uint64_t group = qa_ac_get64(&r), preset = qa_ac_get64(&r);
    if ((definitions->table->count ? group >= definitions->table->count : group != 0) ||
        preset >= qa_audio_reverb_preset_count()) qa_ac_bad(&r, "Sound environment selection leaves its definition");
    v->group = (size_t)group; v->preset = (size_t)preset; v->probe = qa_ac_get32(&r);
    if (v->probe >= PROBE_COUNT) qa_ac_bad(&r, "Invalid sound environment probe cursor");
    for (size_t i = 0; !r.failed && i < PROBE_COUNT; ++i) v->results[i] = qa_ac_getvec(&r);
    v->probe_time = qa_ac_getdouble(&r); v->lerp_start = qa_ac_getdouble(&r); v->lerp_end = qa_ac_getdouble(&r);
    v->lerp_seconds = qa_ac_getfloat(&r); v->enabled = qa_ac_bool(&r); v->has_material = qa_ac_bool(&r);
    if (v->lerp_seconds < 0 || v->lerp_end < v->lerp_start) qa_ac_bad(&r, "Invalid sound environment interpolation phase");
    (void)environment_buffer(&r, &v->material_input); (void)environment_buffer(&r, &v->material_lower);
    if (!v->has_material && (v->material_input.size || v->material_lower.size))
        qa_ac_bad(&r, "Unowned sound material cache");
    get_environment_params(&r, &v->active); get_environment_params(&r, &v->from); get_environment_params(&r, &v->to);
    if (!r.failed && r.offset != bytes.size) qa_ac_bad(&r, "Sound environment checkpoint has trailing fields");
    if (r.failed) { qa_audio_environment_destroy(v); return false; }
    *out = v; return true;
}
