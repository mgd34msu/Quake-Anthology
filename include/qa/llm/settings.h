/*
 * Port of src/llm/settings.ts: LlmSettingsService over POSIX file IO.
 * C accommodations: the mutation queue, request registry and shared
 * refresh Promise collapse to synchronous execution, so operations are
 * trivially serialized in call order and a refresh is shared by the
 * expiry re-check instead of a Promise. Cancellation still lands where
 * callbacks (fetch, on_text, open_browser) set the caller's signal, and
 * timeouts are deadlines on the internal signal checked by readers.
 * GPL-2.0-or-later.
 */
#ifndef QA_LLM_SETTINGS_H
#define QA_LLM_SETTINGS_H

#include "qa/llm/models.h"

typedef enum qa_llm_provider {
  QA_LLM_PROVIDER_SUBSCRIPTION = 0,
  QA_LLM_PROVIDER_CHATGPT_API = 1,
  QA_LLM_PROVIDER_OTHER_API = 2,
} qa_llm_provider;

#define QA_LLM_PROVIDER_COUNT 3

const char *qa_llm_provider_name(qa_llm_provider provider);
bool qa_llm_provider_parse(const char *text, qa_llm_provider *out);

/* OtherService; transport is always "openai-chat-completions". */
typedef struct qa_llm_other_service {
  char *base_url;
  char *model;
} qa_llm_other_service;

void qa_llm_other_service_clear(qa_llm_other_service *service);

typedef enum qa_llm_subscription_auth_status {
  QA_LLM_SUBSCRIPTION_AUTH_IDLE,
  QA_LLM_SUBSCRIPTION_AUTH_PENDING,
  QA_LLM_SUBSCRIPTION_AUTH_ERROR,
} qa_llm_subscription_auth_status;

const char *qa_llm_subscription_auth_status_name(qa_llm_subscription_auth_status status);

/* LlmSettingsSnapshot pieces (read()). All strings owned by snapshot. */
typedef struct qa_llm_settings_providers {
  struct {
    bool configured;
    char *model;
    bool has_expires_at;
    double expires_at;
  } subscription;
  struct {
    bool configured;
    char *model;
  } chatgpt_api;
  struct {
    char *base_url;
    char *model;
    bool configured;
  } other_api;
} qa_llm_settings_providers;

typedef struct qa_llm_settings_file_error {
  char *file;
  char *message;
} qa_llm_settings_file_error;

typedef struct qa_llm_settings_snapshot {
  qa_llm_provider provider;
  char *model;
  char *reasoning_effort; /* selected provider's, NULL mirrors null */
  char *reasoning_efforts[QA_LLM_PROVIDER_COUNT];
  qa_llm_model_catalog catalogs[QA_LLM_PROVIDER_COUNT];
  qa_llm_settings_providers providers;
  qa_llm_subscription_auth_status subscription_auth;
  char *subscription_auth_message; /* set only for the error status */
  qa_llm_settings_file_error *errors;
  size_t error_count;
} qa_llm_settings_snapshot;

void qa_llm_settings_snapshot_free(qa_llm_settings_snapshot *snapshot);

/* LlmSettingsOptions. */
typedef struct qa_llm_settings_options {
  const char *base_directory;
  qa_llm_subscription_auth_options auth;
  qa_llm_request_options request;
} qa_llm_settings_options;

typedef struct qa_llm_settings_service qa_llm_settings_service;

/* LlmSettingsService.open: unreadable/malformed files are collected in
 * the snapshot's errors exactly like TS; NULL only on OOM/bad options. */
qa_llm_settings_service *qa_llm_settings_open(const qa_llm_settings_options *options);
void qa_llm_settings_read(const qa_llm_settings_service *service, qa_llm_settings_snapshot *out);

bool qa_llm_settings_select_provider(qa_llm_settings_service *service, qa_llm_provider selected, qa_llm_error *error);
bool qa_llm_settings_set_model(qa_llm_settings_service *service, qa_llm_provider selected,
                               const char *value, qa_llm_error *error);
bool qa_llm_settings_set_reasoning_effort(qa_llm_settings_service *service, qa_llm_provider selected,
                                          const char *value, qa_llm_error *error);
/* saveOtherService: transport is the raw literal, validated as in TS. */
bool qa_llm_settings_save_other_service(qa_llm_settings_service *service, const char *base_url,
                                        const char *model, const char *transport, qa_llm_error *error);
bool qa_llm_settings_refresh_models(qa_llm_settings_service *service, qa_llm_provider selected,
                                    qa_llm_abort_signal *caller, qa_llm_model_list *out_models, qa_llm_error *error);
/* saveApiKey: selected must be chatgpt-api or other-api. */
bool qa_llm_settings_save_api_key(qa_llm_settings_service *service, qa_llm_provider selected,
                                  const char *value, qa_llm_error *error);
bool qa_llm_settings_remove_credential(qa_llm_settings_service *service, qa_llm_provider selected, qa_llm_error *error);
/* signInSubscription: a cancelled sign-in resolves in TS, so it
 * returns true with nothing saved, exactly as authenticate() does. */
bool qa_llm_settings_sign_in_subscription(qa_llm_settings_service *service, qa_llm_error *error);
bool qa_llm_settings_refresh_subscription(qa_llm_settings_service *service, qa_llm_error *error);

const char *qa_llm_settings_session_id(const qa_llm_settings_service *service);
bool qa_llm_settings_prime(qa_llm_settings_service *service, const char *instructions, qa_llm_error *error);
bool qa_llm_settings_request(qa_llm_settings_service *service, const qa_llm_request_input *input,
                             char **out_text, qa_llm_error *error);
void qa_llm_settings_cancel_sign_in(qa_llm_settings_service *service);
/* close(): aborts in-flight work and rejects later operations with
 * "LLM settings are closed."; destroy frees afterwards. */
void qa_llm_settings_close(qa_llm_settings_service *service);
void qa_llm_settings_destroy(qa_llm_settings_service *service);

#endif
