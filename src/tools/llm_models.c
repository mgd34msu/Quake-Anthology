#include "llm_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *const minimal[] = {"minimal", "low", "medium", "high"};
static const char *const high[] = {"high"};
static const char *const pro[] = {"medium", "high", "xhigh"};
static const char *const four[] = {"none", "low", "medium", "high"};
static const char *const five[] = {"none", "low", "medium", "high", "xhigh"};
static const char *const six[] = {"none", "low", "medium", "high", "xhigh", "max"};
static const char *const astra[] = {"low", "medium", "high", "xhigh", "max"};
static const struct { const char *id; const char *const *efforts; size_t count; } reference_models[] = {
    {"gpt-5", minimal, 4}, {"gpt-5-pro", high, 1}, {"gpt-5.2-pro", pro, 3},
    {"gpt-5.4", five, 5}, {"gpt-5.4-pro", pro, 3}, {"gpt-5.4-mini", five, 5},
    {"gpt-5.4-nano", five, 5}, {"gpt-5.6-sol", six, 6}, {"gpt-5.1", four, 4},
    {"gpt-5.2", five, 5}, {"gpt-5.5", five, 5}, {"gpt-5.5-pro", pro, 3},
    {"gpt-6-astra", astra, 5}, {"gpt-5.6-terra", six, 6}, {"gpt-5.6-luna", six, 6}
};
static bool date_suffix(const char *id, size_t n) {
    if (n < 11 || id[n - 11] != '-' || id[n - 6] != '-' || id[n - 3] != '-') return false;
    for (size_t i = n - 10; i < n; ++i) if (i != n - 6 && i != n - 3 && (id[i] < '0' || id[i] > '9')) return false;
    return true;
}
static void reference_model(const char *id, qa_llm_model *out) {
    *out = (qa_llm_model){.id = id, .name = id}; size_t n = strlen(id);
    if (date_suffix(id, n)) n -= 11;
    for (size_t i = 0; i < sizeof reference_models / sizeof reference_models[0]; ++i) {
        if (strlen(reference_models[i].id) == n && !memcmp(id, reference_models[i].id, n)) {
            out->efforts = reference_models[i].efforts; out->effort_count = reference_models[i].count; return;
        }
    }
}
bool llm_model_effort(const qa_llm_model *model, const char *effort) {
    if (!effort) return true;
    if (model) for (size_t i = 0; i < model->effort_count; ++i) if (!strcmp(model->efforts[i], effort)) return true;
    return false;
}
const qa_llm_model *llm_model_metadata(const qa_llm *s, qa_llm_provider provider, const char *id, qa_llm_model *fallback) {
    if (!s || !llm_provider_valid(provider) || !id) return NULL;
    const llm_catalog *catalog = &s->catalogs[provider];
    for (size_t i = 0; i < catalog->count; ++i) if (!strcmp(catalog->models[i].id, id)) return &catalog->models[i];
    if (provider == QA_LLM_OPENAI_API && fallback) { reference_model(id, fallback); return fallback; }
    return NULL;
}
static bool object(const qa_json_document *d, qa_json_id id, qa_error *error) {
    return qa_json_type(d, id) == QA_JSON_OBJECT || llm_fail(error, "service returned invalid model metadata");
}
static bool model_string(const qa_json_document *d, qa_json_id id, qa_arena *arena, const char **out, qa_error *error) {
    qa_buffer text = {0};
    if (!qa_json_string(d, id, &text, error)) return llm_fail(error, "service returned invalid model metadata");
    bool ok = llm_model_text(arena, (qa_bytes){text.data, text.size}, false, out, error); qa_buffer_free(&text); return ok;
}
static bool effort_string(const qa_json_document *d, qa_json_id id, qa_arena *arena, const char **out, qa_error *error) {
    if (id == QA_JSON_NONE || qa_json_type(d, id) == QA_JSON_NULL) { *out = NULL; return true; }
    qa_buffer text = {0};
    if (!qa_json_string(d, id, &text, error)) return llm_fail(error, "service returned invalid reasoning choices");
    bool ok = llm_effort_text(arena, (qa_bytes){text.data, text.size}, out, error); qa_buffer_free(&text); return ok;
}
static bool folded_contains(const char *text, const char *part) {
    size_t n = strlen(part);
    for (size_t i = 0; text[i]; ++i) {
        size_t j = 0;
        while (j < n && text[i + j]) {
            unsigned c = (unsigned char)text[i + j]; if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (c != (unsigned char)part[j]) break;
            ++j;
        }
        if (j == n) return true;
    }
    return false;
}
static bool non_chat(const char *id) {
    static const char *const names[] = {"embedding", "whisper", "tts", "dall-e", "davinci", "babbage", "moderation", "text-search", "similarity", "transcribe", "speech", "realtime", "image"};
    if (strlen(id) >= 3 && (id[0] == 'a' || id[0] == 'A') && (id[1] == 'd' || id[1] == 'D') && (id[2] == 'a' || id[2] == 'A')) return true;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) if (folded_contains(id, names[i])) return true;
    return false;
}
typedef struct ranked_model { qa_llm_model model; double priority; size_t ordinal; } ranked_model;
static int ranked_compare(const void *a, const void *b) {
    const ranked_model *x = a, *y = b;
    if (x->priority < y->priority) return -1;
    if (x->priority > y->priority) return 1;
    return x->ordinal < y->ordinal ? -1 : x->ordinal > y->ordinal ? 1 : 0;
}
static int name_compare(const void *a, const void *b) {
    const qa_llm_model *x = a, *y = b; return strcmp(x->name, y->name);
}
bool llm_catalog_parse(qa_llm_provider provider, qa_bytes bytes, llm_catalog *out, qa_error *error) {
    if (!llm_provider_valid(provider) || !out) return llm_fail(error, "invalid model catalog provider");
    qa_json_document *d = NULL; if (!qa_json_parse(bytes, &d, error)) return llm_fail(error, "service returned an invalid model list response");
    llm_catalog next = {0}; bool ok = false; qa_json_id root = qa_json_root(d);
    if (!object(d, root, error)) goto done;
    qa_json_id array = qa_json_get(d, root, provider == QA_LLM_SUBSCRIPTION ? "models" : "data");
    if (qa_json_type(d, array) != QA_JSON_ARRAY) { llm_fail(error, "service returned an invalid model list"); goto done; }
    size_t n = qa_json_size(d, array);
    if (n > SIZE_MAX / sizeof(ranked_model) || n > SIZE_MAX / sizeof(qa_llm_model)) { llm_fail(error, "model list count overflow"); goto done; }
    ranked_model *ranked = n ? qa_arena_alloc(&next.storage, n * sizeof(*ranked), _Alignof(ranked_model), error) : NULL;
    next.models = n ? qa_arena_alloc(&next.storage, n * sizeof(*next.models), _Alignof(qa_llm_model), error) : NULL;
    if (n && (!ranked || !next.models)) goto done;
    size_t included = 0;
    for (size_t i = 0; i < n; ++i) {
        qa_json_id item = qa_json_at(d, array, i); if (!object(d, item, error)) goto done;
        ranked_model value = {.ordinal = i};
        if (provider == QA_LLM_SUBSCRIPTION) {
            if (!qa_json_string_equal(d, qa_json_get(d, item, "visibility"), "list")) continue;
            if (!model_string(d, qa_json_get(d, item, "slug"), &next.storage, &value.model.id, error)) goto done;
            qa_json_id name = qa_json_get(d, item, "display_name");
            if (name == QA_JSON_NONE || qa_json_type(d, name) == QA_JSON_NULL) value.model.name = value.model.id;
            else if (!model_string(d, name, &next.storage, &value.model.name, error)) goto done;
            qa_json_id levels = qa_json_get(d, item, "supported_reasoning_levels");
            if (qa_json_type(d, levels) != QA_JSON_ARRAY) { llm_fail(error, "subscription service returned invalid reasoning choices"); goto done; }
            size_t count = qa_json_size(d, levels);
            if (count > SIZE_MAX / sizeof(char *)) { llm_fail(error, "reasoning choices overflow"); goto done; }
            const char **efforts = count ? qa_arena_alloc(&next.storage, count * sizeof(*efforts), _Alignof(char *), error) : NULL;
            if (count && !efforts) goto done;
            for (size_t j = 0; j < count; ++j) {
                qa_json_id level = qa_json_at(d, levels, j); const char *effort;
                if (!object(d, level, error) || !effort_string(d, qa_json_get(d, level, "effort"), &next.storage, &effort, error)) goto done;
                if (!effort) { llm_fail(error, "subscription service returned invalid reasoning choices"); goto done; }
                bool duplicate = false;
                for (size_t k = 0; k < value.model.effort_count; ++k) if (!strcmp(efforts[k], effort)) { duplicate = true; break; }
                if (!duplicate) efforts[value.model.effort_count++] = effort;
            }
            value.model.efforts = efforts;
            const char *default_effort;
            if (!effort_string(d, qa_json_get(d, item, "default_reasoning_level"), &next.storage, &default_effort, error)) goto done;
            if (default_effort && llm_model_effort(&value.model, default_effort)) value.model.default_effort = default_effort;
            if (!qa_json_number(d, qa_json_get(d, item, "priority"), &value.priority, error) || !isfinite(value.priority)) { llm_fail(error, "subscription service returned invalid model priority metadata"); goto done; }
        } else {
            if (!model_string(d, qa_json_get(d, item, "id"), &next.storage, &value.model.id, error)) goto done;
            if (provider == QA_LLM_OPENAI_API) { if (non_chat(value.model.id)) continue; reference_model(value.model.id, &value.model); }
            else value.model.name = value.model.id;
        }
        ranked[included++] = value;
    }
    if (provider == QA_LLM_SUBSCRIPTION && included > 1) qsort(ranked, included, sizeof(*ranked), ranked_compare);
    for (size_t i = 0; i < included; ++i) {
        size_t position = 0;
        while (position < next.count && strcmp(next.models[position].id, ranked[i].model.id)) ++position;
        next.models[position] = ranked[i].model;
        if (position == next.count) ++next.count;
    }
    if (provider == QA_LLM_SUBSCRIPTION) { if (next.count) next.models[0].recommended = true; }
    else if (next.count > 1) qsort(next.models, next.count, sizeof(*next.models), name_compare);
    next.ready = true; ok = true;
done:
    qa_json_destroy(d); if (ok) *out = next; else qa_arena_destroy(&next.storage); return ok;
}
const qa_llm_model *qa_llm_model_at(const qa_llm *s, qa_llm_provider provider, size_t index) {
    if (!s || !llm_provider_valid(provider) || index >= s->catalogs[provider].count) return NULL;
    return &s->catalogs[provider].models[index];
}
size_t qa_llm_model_count(const qa_llm *s, qa_llm_provider provider) { return s && llm_provider_valid(provider) ? s->catalogs[provider].count : 0; }
