#include "qa/audio.h"
#include "qa/json.h"
#include "qa/text.h"
#include "reverb_presets.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct material_reverb {
    qa_string_id *materials;
    size_t material_count, preset;
    bool wildcard;
} material_reverb;

typedef struct environment_group {
    double dimension;
    material_reverb *reverbs;
    size_t count;
    size_t *material_presets, material_extent, fallback;
} environment_group;

typedef struct environment_table {
    atomic_size_t references;
    qa_strings *strings;
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
            free(reverb->materials);
        }
        free(group->reverbs);
        free(group->material_presets);
    }
    free(table->groups);
    qa_strings_destroy(table->strings);
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

static bool parse_reverb(const qa_json_document *document, qa_json_id id, material_reverb *reverb, qa_strings *strings,
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
                qa_buffer lower = {0};
                bool lowered = qa_utf8_lower((qa_bytes){text.data, text.size}, &lower, error) &&
                    qa_strings_intern(strings, (qa_bytes){lower.data, lower.size}, reverb->materials + i, error);
                qa_buffer_free(&lower); qa_buffer_free(&text);
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

static bool parse_group(const qa_json_document *document, qa_json_id id, environment_group *group, qa_strings *strings,
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
        if (!parse_reverb(document, qa_json_at(document, reverbs, i), &group->reverbs[i], strings, warning,
                          warning_user, error))
            return false;
    return true;
}

static bool group_materials_prepare(environment_group *group, qa_error *error)
{
    qa_string_id maximum = QA_STRING_NONE;
    size_t first_wildcard = group->count;
    group->fallback = SIZE_MAX;
    for (size_t i = 0; i < group->count; ++i) {
        const material_reverb *reverb = group->reverbs + i;
        if (reverb->wildcard && first_wildcard == group->count) {
            first_wildcard = i; group->fallback = reverb->preset;
        }
        for (size_t j = 0; j < reverb->material_count; ++j)
            if (reverb->materials[j] > maximum) maximum = reverb->materials[j];
    }
    group->material_extent = (size_t)maximum + 1;
    group->material_presets = malloc(group->material_extent * sizeof(*group->material_presets));
    if (!group->material_presets) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Indexing sound environment materials"); return false;
    }
    for (size_t i = 0; i < group->material_extent; ++i) group->material_presets[i] = group->fallback;
    /* Earlier declarations win, including a wildcard before any later material. */
    for (size_t i = first_wildcard; i > 0; --i) {
        const material_reverb *reverb = group->reverbs + i - 1;
        for (size_t j = 0; j < reverb->material_count; ++j)
            group->material_presets[reverb->materials[j]] = reverb->preset;
    }
    return true;
}
bool qa_audio_environments_parse(qa_bytes json, qa_strings *strings, qa_audio_environments **out, qa_error *error) {
    return qa_audio_environments_parse_ex(json, strings, NULL, NULL, out, error);
}

bool qa_audio_environments_parse_ex(qa_bytes json, qa_strings *strings, qa_audio_log_fn warning, void *warning_user,
                                    qa_audio_environments **out, qa_error *error) {
    if (!out || !strings) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Sound environments require a shared name table and output");
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
    table->strings = strings; qa_strings_retain(strings);
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
        if (!parse_group(document, qa_json_at(document, groups, i), &table->groups[i], strings, warning,
                         warning_user, error) || !group_materials_prepare(table->groups + i, error))
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
        size_t choice = floor_hit.material && floor_hit.material < group->material_extent ?
            group->material_presets[floor_hit.material] : group->fallback;
        if (choice != SIZE_MAX) selected = choice;
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
