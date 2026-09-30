#ifndef QA_LLM_INTERNAL_H
#define QA_LLM_INTERNAL_H
#include "llm_stream.h"
#include "qa/arena.h"
#include "qa/json.h"
#include "qa/json_writer.h"
#include "qa/llm_save.h"

typedef struct llm_preferences {
    qa_arena storage;
    qa_llm_provider provider;
    const char *models[2], *efforts[3];
} llm_preferences;
typedef struct llm_subscription {
    const char *access_token, *refresh_token, *token_type;
    double expires_at;
    const char *const *scopes;
    size_t scope_count;
} llm_subscription;
typedef struct llm_credentials {
    qa_arena storage;
    const char *api_key;
    llm_subscription subscription;
    bool subscribed;
} llm_credentials;
typedef struct llm_other {
    qa_arena storage;
    const char *base_url, *model;
} llm_other;
typedef struct llm_catalog {
    qa_arena storage;
    qa_llm_model *models;
    size_t count;
    uint64_t generation;
    bool ready, loading;
    qa_error error;
} llm_catalog;
typedef struct llm_job llm_job;
typedef struct llm_console llm_console;
typedef struct llm_auth llm_auth;
struct qa_llm {
    qa_llm_options options;
    qa_arena option_storage;
    llm_preferences preferences;
    llm_credentials credentials;
    llm_other other;
    qa_buffer other_key;
    qa_error settings_errors[4];
    llm_catalog catalogs[3];
    llm_job *jobs;
    llm_console *consoles;
    llm_auth *auth;
    uint64_t next_id;
    unsigned busy;
    qa_error auth_error;
    bool signing_in;
    bool pending_restore;
    uint64_t auth_generation;
};
const char *llm_provider_name(qa_llm_provider);
bool llm_provider_valid(qa_llm_provider);
double llm_wall_milliseconds(qa_llm *);
char *llm_arena_text(qa_arena *, qa_bytes, qa_error *);
qa_bytes llm_trim(qa_bytes);
bool llm_model_text(qa_arena *, qa_bytes, bool allow_empty, const char **, qa_error *);
bool llm_effort_text(qa_arena *, qa_bytes, const char **, qa_error *);
bool llm_api_key_text(qa_arena *, qa_bytes, const char **, qa_error *);
bool llm_preferences_load(qa_llm *, llm_preferences *, qa_error *);
bool llm_credentials_load(qa_llm *, llm_credentials *, qa_error *);
bool llm_other_load(qa_llm *, llm_other *, qa_error *);
bool llm_other_key_load(qa_llm *, qa_buffer *, qa_error *);
bool llm_preferences_save(qa_llm *, const llm_preferences *, qa_error *);
bool llm_credentials_save(qa_llm *, const llm_credentials *, qa_error *);
bool llm_other_save(qa_llm *, const llm_other *, qa_error *);
bool llm_other_prepare(const char *, const char *, llm_other *, qa_error *);
bool llm_subscription_parse(const qa_json_document *, qa_json_id, qa_arena *, llm_subscription *, qa_error *);
bool llm_catalog_parse(qa_llm_provider, qa_bytes, llm_catalog *, qa_error *);
const qa_llm_model *llm_model_metadata(const qa_llm *, qa_llm_provider, const char *, qa_llm_model *reference);
bool llm_model_effort(const qa_llm_model *, const char *);
uint32_t llm_effort_table_identity(const char *const *, size_t);
const char *const *llm_effort_table_resolve(uint32_t, size_t *);
bool llm_auth_tick(qa_llm *, qa_error *);
void llm_auth_cancel(qa_llm *);
bool llm_jobs_tick(qa_llm *, qa_error *);
void llm_jobs_cancel_provider(qa_llm *, qa_llm_provider);
void llm_jobs_destroy(qa_llm *);
bool llm_console_tick(qa_llm *, qa_error *);
bool llm_console_detach_all(qa_llm *, qa_error *);
bool llm_console_instructions(qa_console *, const qa_command_context *, const char *, bool execute,
                              qa_buffer *, qa_error *);
bool llm_auth_credential(qa_llm *, const char *rejected_access, uint64_t *ticket,
                         qa_arena *, llm_subscription *, bool *ready, qa_error *);
void llm_auth_refresh_users(qa_llm *, size_t);
bool llm_subscription_account(const char *access_token, qa_buffer *, qa_error *);
bool llm_session_id(char out[37], qa_error *);
bool llm_discover_start(qa_llm *, qa_llm_provider, qa_error *);
bool llm_saved_subscription(qa_source_save_io *, qa_arena *, llm_subscription *);
bool llm_jobs_checkpoint_ready(const qa_llm *, qa_error *);
bool llm_jobs_fields(qa_source_save_io *, qa_llm *, const qa_llm_checkpoint_refs *);
void llm_jobs_rebind(qa_llm *);
bool llm_console_fields(qa_source_save_io *, qa_llm *, const qa_llm *, const qa_llm_checkpoint_refs *);
bool llm_console_observer_encode(const qa_llm *, const qa_llm_observer *, uint64_t *);
bool llm_console_observer_decode(qa_llm *, uint64_t, qa_llm_observer *);
void llm_console_private_free(qa_llm *);
void llm_console_exchange(qa_llm *, qa_llm *);
#endif
