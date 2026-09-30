#ifndef QA_LLM_H
#define QA_LLM_H
#include "qa/http.h"
#include "qa/console.h"
#include "qa/vfs.h"

typedef enum qa_llm_provider { QA_LLM_SUBSCRIPTION, QA_LLM_OPENAI_API, QA_LLM_OTHER_API } qa_llm_provider;
typedef struct qa_llm_model {
    const char *id, *name, *default_effort;
    const char *const *efforts;
    size_t effort_count;
    bool recommended;
} qa_llm_model;
typedef struct qa_llm qa_llm;
typedef uint64_t qa_llm_request_id;
typedef struct qa_llm_observer {
    void *context;
    void (*text)(void *, qa_llm_request_id, qa_bytes);
    void (*complete)(void *, qa_llm_request_id, qa_bytes answer, const qa_error *);
} qa_llm_observer;
typedef struct qa_llm_options {
    qa_http *http; /* Shared owner, driven by the ordinary frontend event loop. */
    qa_vfs *settings;
    qa_mount_id private_mount; /* Dedicated preferences/credentials, outside game content. */
    uint64_t owner; /* Callback lifetime owner; console dispatch uses engine owner zero. */
    void *context;
    double (*wall_milliseconds)(void *);
    bool (*open_browser)(void *, const char *url, qa_error *);
    bool (*context_active)(void *, const qa_command_context *);
    void (*print)(void *, const qa_command_context *, const char *);
    /* Stamp actual source/client/world generations at console admission. */
    bool (*capture_context)(void *, const qa_command_context *, qa_command_context *, qa_error *);
    uint32_t request_timeout_ms, callback_timeout_ms;
    uint16_t callback_port; /* Zero selects the source default 1455. */
    const char *authorization_url, *token_url; /* NULL selects production URLs. */
} qa_llm_options;
typedef enum qa_llm_catalog_status { QA_LLM_CATALOG_IDLE, QA_LLM_CATALOG_LOADING,
    QA_LLM_CATALOG_READY, QA_LLM_CATALOG_ERROR } qa_llm_catalog_status;
typedef struct qa_llm_snapshot {
    qa_llm_provider provider;
    const char *models[3], *efforts[3];
    const char *other_base_url;
    bool configured[3], signing_in;
    double subscription_expires_at;
    qa_llm_catalog_status catalogs[3];
    const qa_error *catalog_errors[3], *settings_errors[4], *authentication_error;
} qa_llm_snapshot;
/* UI observations contain no credentials. Borrowed until the next mutation. */
bool qa_llm_read(const qa_llm *, qa_llm_snapshot *, qa_error *);
bool qa_llm_create(const qa_llm_options *, qa_llm **, qa_error *);
bool qa_llm_destroy(qa_llm *, qa_error *);
bool qa_llm_tick(qa_llm *, qa_error *);
bool qa_llm_callbacks_idle(const qa_llm *);
bool qa_llm_request(qa_llm *, const char *prompt, const char *instructions,
                    const qa_llm_observer *, qa_llm_request_id *, qa_error *);
void qa_llm_cancel(qa_llm *, qa_llm_request_id);
bool qa_llm_select_provider(qa_llm *, qa_llm_provider, qa_error *);
bool qa_llm_select_model(qa_llm *, qa_llm_provider, const char *id, qa_error *);
bool qa_llm_select_effort(qa_llm *, qa_llm_provider, const char *effort_or_null, qa_error *);
bool qa_llm_set_api_key(qa_llm *, qa_llm_provider, const char *key_or_null, qa_error *);
bool qa_llm_set_other_connection(qa_llm *, const char *base_url, const char *model, qa_error *);
bool qa_llm_discover_models(qa_llm *, qa_llm_provider, qa_error *);
bool qa_llm_cancel_model_discovery(qa_llm *, qa_llm_provider, qa_error *);
/* Borrowed catalog until its provider's next committed discovery/reset. */
const qa_llm_model *qa_llm_model_at(const qa_llm *, qa_llm_provider, size_t);
size_t qa_llm_model_count(const qa_llm *, qa_llm_provider);
bool qa_llm_sign_in(qa_llm *, qa_error *);
void qa_llm_cancel_sign_in(qa_llm *);
bool qa_llm_sign_out(qa_llm *, qa_error *);
bool qa_llm_attach_console(qa_llm *, qa_console *, qa_error *);
bool qa_llm_detach_console(qa_llm *, qa_console *, qa_error *);
/* Retire every bound source console before its owner or world is released. */
bool qa_llm_before_world_change(qa_llm *, qa_error *);
/* Admission resolves aliases and validates the complete batch before producing
 * the exact ASCII command text. Normal command permissions still apply later. */
bool qa_llm_validate_batch(qa_console *, const qa_command_context *, qa_bytes,
                            qa_buffer *normalized, qa_error *);
#endif
