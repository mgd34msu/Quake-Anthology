#include "llm_internal.h"
#include <stdlib.h>
#include <string.h>

static bool admit(qa_llm *s, qa_error *error) { return (s && !s->busy) || llm_fail(error, "LLM mutation requires its callbacks to return"); }
bool qa_llm_callbacks_idle(const qa_llm *s) { return !s || !s->busy; }
double llm_wall_milliseconds(qa_llm *s) {
    ++s->busy; double now = s->options.wall_milliseconds(s->options.context); --s->busy; return now;
}
static void invalidate(qa_llm *s, qa_llm_provider provider) {
    llm_catalog *c = &s->catalogs[provider]; uint64_t generation = c->generation + 1;
    llm_jobs_cancel_provider(s, provider); qa_arena_destroy(&c->storage);
    *c = (llm_catalog){.generation = generation};
}
static void commit_preferences(qa_llm *s, llm_preferences *next) {
    qa_arena_destroy(&s->preferences.storage); s->preferences = *next; *next = (llm_preferences){0};
    s->settings_errors[0] = (qa_error){0};
}
bool qa_llm_create(const qa_llm_options *options, qa_llm **out, qa_error *error) {
    if (!options || !out || !options->http || !options->settings || !options->private_mount || !options->owner ||
        !options->wall_milliseconds || !options->open_browser || !options->context_active || !options->print)
        return llm_fail(error, "invalid native LLM services");
    qa_llm *s = calloc(1, sizeof *s);
    if (!s) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating native LLM owner"); return false; }
    s->options = *options; s->next_id = 1;
    if (!s->options.request_timeout_ms) s->options.request_timeout_ms = 120000;
    if (!s->options.callback_timeout_ms) s->options.callback_timeout_ms = 180000;
    if (!s->options.callback_port) s->options.callback_port = 1455;
    s->preferences = (llm_preferences){.models = {"", ""}};
    s->other = (llm_other){.base_url = "", .model = ""};
    (void)llm_preferences_load(s, &s->preferences, &s->settings_errors[0]);
    (void)llm_credentials_load(s, &s->credentials, &s->settings_errors[1]);
    (void)llm_other_key_load(s, &s->other_key, &s->settings_errors[2]);
    (void)llm_other_load(s, &s->other, &s->settings_errors[3]);
    /* File errors remain independently visible without exposing private data. */
    static const char *const files[] = {"llm.json", "chatgpt.key", "other.key", "other.service"};
    for (size_t i = 0; i < 4; ++i) if (s->settings_errors[i].code != QA_OK)
        qa_error_set(&s->settings_errors[i], QA_ERROR_IO, 0, "could not load %s; check its format and permissions", files[i]);
    *out = s; return true;
}
bool qa_llm_destroy(qa_llm *s, qa_error *error) {
    if (!s) return true;
    if (!admit(s, error)) return false;
    if (!llm_console_detach_all(s, error)) return false;
    llm_auth_cancel(s); llm_jobs_destroy(s);
    qa_arena_destroy(&s->preferences.storage); qa_arena_destroy(&s->credentials.storage); qa_arena_destroy(&s->other.storage);
    qa_buffer_free(&s->other_key);
    for (size_t i = 0; i < 3; ++i) qa_arena_destroy(&s->catalogs[i].storage);
    free(s); return true;
}
bool qa_llm_tick(qa_llm *s, qa_error *error) {
    if (!admit(s, error)) return false;
    ++s->busy;
    bool ok = llm_auth_tick(s, error) && llm_jobs_tick(s, error) && llm_console_tick(s, error);
    --s->busy; return ok;
}
bool qa_llm_read(const qa_llm *s, qa_llm_snapshot *out, qa_error *error) {
    if (!s || !out) return llm_fail(error, "invalid LLM settings observation");
    qa_llm_snapshot snapshot = {.provider = s->preferences.provider, .models = {s->preferences.models[0], s->preferences.models[1], s->other.model},
        .efforts = {s->preferences.efforts[0], s->preferences.efforts[1], s->preferences.efforts[2]},
        .configured = {s->credentials.subscribed, s->credentials.api_key != NULL, s->other_key.data != NULL},
        .subscription_expires_at = s->credentials.subscribed ? s->credentials.subscription.expires_at : 0,
        .other_base_url = s->other.base_url,
        .signing_in = s->signing_in, .authentication_error = &s->auth_error};
    for (size_t i = 0; i < 3; ++i) {
        const llm_catalog *c = &s->catalogs[i];
        snapshot.catalogs[i] = c->loading ? QA_LLM_CATALOG_LOADING : c->error.code != QA_OK ? QA_LLM_CATALOG_ERROR : c->ready ? QA_LLM_CATALOG_READY : QA_LLM_CATALOG_IDLE;
        snapshot.catalog_errors[i] = &c->error;
    }
    for (size_t i = 0; i < 4; ++i) snapshot.settings_errors[i] = &s->settings_errors[i];
    *out = snapshot; return true;
}
bool qa_llm_select_provider(qa_llm *s, qa_llm_provider provider, qa_error *error) {
    if (!admit(s, error) || !llm_provider_valid(provider)) return llm_fail(error, "invalid LLM provider setting");
    if (provider != QA_LLM_SUBSCRIPTION) llm_auth_cancel(s);
    llm_preferences next = {0}; if (!llm_preferences_load(s, &next, error)) return false;
    next.provider = provider;
    bool ok = llm_preferences_save(s, &next, error);
    if (ok) commit_preferences(s, &next);
    qa_arena_destroy(&next.storage); return ok;
}
bool qa_llm_select_effort(qa_llm *s, qa_llm_provider provider, const char *effort, qa_error *error) {
    if (!admit(s, error) || !llm_provider_valid(provider)) return llm_fail(error, "invalid reasoning provider");
    llm_preferences next = {0}; if (!llm_preferences_load(s, &next, error)) return false;
    bool ok = false; const char *choice = NULL;
    if (effort && !llm_effort_text(&next.storage, (qa_bytes){(const uint8_t *)effort, strlen(effort)}, &choice, error)) goto done;
    const char *name = provider == QA_LLM_OTHER_API ? s->other.model : next.models[provider];
    qa_llm_model fallback; const qa_llm_model *model = llm_model_metadata(s, provider, name, &fallback);
    if (!llm_model_effort(model, choice)) { llm_fail(error, "choose a reasoning effort supported by the selected model"); goto done; }
    next.efforts[provider] = choice;
    if (!llm_preferences_save(s, &next, error)) goto done;
    commit_preferences(s, &next); ok = true;
done:
    qa_arena_destroy(&next.storage); return ok;
}
bool qa_llm_select_model(qa_llm *s, qa_llm_provider provider, const char *id, qa_error *error) {
    if (!admit(s, error) || !llm_provider_valid(provider) || !id) return llm_fail(error, "invalid model provider");
    llm_preferences next = {0}; if (!llm_preferences_load(s, &next, error)) return false;
    bool ok = false; const char *name;
    if (!llm_model_text(&next.storage, (qa_bytes){(const uint8_t *)id, strlen(id)}, true, &name, error)) goto done;
    qa_llm_model fallback; const qa_llm_model *metadata = llm_model_metadata(s, provider, name, &fallback);
    if (s->catalogs[provider].ready) {
        bool found = false;
        for (size_t i = 0; i < s->catalogs[provider].count; ++i) if (!strcmp(s->catalogs[provider].models[i].id, name)) { found = true; break; }
        if (!found) { llm_fail(error, "choose a model from the loaded provider list"); goto done; }
    }
    const char *old_effort = next.efforts[provider];
    next.efforts[provider] = old_effort && llm_model_effort(metadata, old_effort) ? old_effort : metadata ? metadata->default_effort : NULL;
    if (next.efforts[provider] && next.efforts[provider] != old_effort) {
        next.efforts[provider] = llm_arena_text(&next.storage, (qa_bytes){(const uint8_t *)next.efforts[provider], strlen(next.efforts[provider])}, error);
        if (!next.efforts[provider]) goto done;
    }
    if (provider == QA_LLM_OTHER_API) {
        llm_other other = {0}; if (!llm_other_load(s, &other, error)) goto done;
        other.model = llm_arena_text(&other.storage, (qa_bytes){(const uint8_t *)name, strlen(name)}, error);
        if (!other.model || !other.base_url[0] || !llm_other_save(s, &other, error)) { qa_arena_destroy(&other.storage); goto done; }
        qa_arena_destroy(&s->other.storage); s->other = other; s->settings_errors[3] = (qa_error){0};
    } else next.models[provider] = name;
    if (!llm_preferences_save(s, &next, error)) goto done;
    commit_preferences(s, &next); ok = true;
done:
    qa_arena_destroy(&next.storage); return ok;
}
bool qa_llm_set_api_key(qa_llm *s, qa_llm_provider provider, const char *key, qa_error *error) {
    if (!admit(s, error) || (provider != QA_LLM_OPENAI_API && provider != QA_LLM_OTHER_API)) return llm_fail(error, "invalid API-key provider");
    llm_credentials next = {0}; qa_arena arena = {0}; const char *value = NULL; bool ok = false;
    if (key && !llm_api_key_text(&arena, (qa_bytes){(const uint8_t *)key, strlen(key)}, &value, error)) goto done;
    if (provider == QA_LLM_OTHER_API) {
        if (value) {
            qa_buffer copy = {0}; copy.size = strlen(value); copy.data = malloc(copy.size + 1);
            if (!copy.data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Other API credential"); goto done; }
            memcpy(copy.data, value, copy.size + 1);
            if (!qa_vfs_write_private(s->options.settings, s->options.private_mount, "other.key", (qa_bytes){copy.data, copy.size}, error)) { qa_buffer_free(&copy); goto done; }
            qa_buffer_free(&s->other_key); s->other_key = copy;
        } else {
            qa_resource *present = NULL; qa_error local = {0};
            bool exists = qa_vfs_acquire_from(s->options.settings, s->options.private_mount, "other.key", &present, &local);
            qa_resource_release(present);
            if (!exists && local.code != QA_ERROR_NOT_FOUND) { qa_error_set(error, QA_ERROR_IO, 0, "could not inspect other.key for removal"); goto done; }
            if (exists && !qa_vfs_remove(s->options.settings, s->options.private_mount, "other.key", error)) goto done;
            qa_buffer_free(&s->other_key);
        }
        s->settings_errors[2] = (qa_error){0};
    } else {
        if (!llm_credentials_load(s, &next, error)) goto done;
        next.api_key = value ? llm_arena_text(&next.storage, (qa_bytes){(const uint8_t *)value, strlen(value)}, error) : NULL;
        if (value && !next.api_key) goto done;
        if (!llm_credentials_save(s, &next, error)) goto done;
        qa_arena_destroy(&s->credentials.storage); s->credentials = next; next = (llm_credentials){0}; s->settings_errors[1] = (qa_error){0};
    }
    invalidate(s, provider);
    if (value) (void)llm_discover_start(s, provider, &s->catalogs[provider].error);
    ok = true;
done:
    qa_arena_destroy(&next.storage); qa_arena_destroy(&arena); return ok;
}
bool qa_llm_set_other_connection(qa_llm *s, const char *url, const char *id, qa_error *error) {
    if (!admit(s, error)) return false;
    llm_other next = {0}; if (!llm_other_prepare(url, id, &next, error)) return false;
    bool changed = strcmp(next.base_url, s->other.base_url) != 0, model_changed = strcmp(next.model, s->other.model) != 0;
    if (!changed && model_changed && s->catalogs[QA_LLM_OTHER_API].ready) {
        qa_llm_model fallback;
        if (!llm_model_metadata(s, QA_LLM_OTHER_API, next.model, &fallback)) { qa_arena_destroy(&next.storage); return llm_fail(error, "choose a model from the loaded provider list"); }
    }
    if (!llm_other_save(s, &next, error)) { qa_arena_destroy(&next.storage); return false; }
    qa_arena_destroy(&s->other.storage); s->other = next; s->settings_errors[3] = (qa_error){0};
    if (changed) invalidate(s, QA_LLM_OTHER_API);
    if (changed || model_changed) {
        llm_preferences preferences = {0}; if (!llm_preferences_load(s, &preferences, error)) return false;
        qa_llm_model fallback; const qa_llm_model *metadata = llm_model_metadata(s, QA_LLM_OTHER_API, next.model, &fallback);
        const char *old = preferences.efforts[QA_LLM_OTHER_API];
        preferences.efforts[QA_LLM_OTHER_API] = old && llm_model_effort(metadata, old) ? old : metadata ? metadata->default_effort : NULL;
        if (preferences.efforts[QA_LLM_OTHER_API]) {
            const char *effort = preferences.efforts[QA_LLM_OTHER_API];
            preferences.efforts[QA_LLM_OTHER_API] = llm_arena_text(&preferences.storage, (qa_bytes){(const uint8_t *)effort, strlen(effort)}, error);
            if (!preferences.efforts[QA_LLM_OTHER_API]) { qa_arena_destroy(&preferences.storage); return false; }
        }
        if (!llm_preferences_save(s, &preferences, error)) { qa_arena_destroy(&preferences.storage); return false; }
        commit_preferences(s, &preferences);
    }
    return true;
}
bool qa_llm_sign_out(qa_llm *s, qa_error *error) {
    if (!admit(s, error)) return false;
    llm_auth_cancel(s); llm_credentials next = {0}; if (!llm_credentials_load(s, &next, error)) return false;
    next.subscribed = false; next.subscription = (llm_subscription){0};
    if (!llm_credentials_save(s, &next, error)) { qa_arena_destroy(&next.storage); return false; }
    qa_arena_destroy(&s->credentials.storage); s->credentials = next; s->settings_errors[1] = (qa_error){0}; invalidate(s, QA_LLM_SUBSCRIPTION); return true;
}
