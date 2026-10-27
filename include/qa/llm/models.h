/* Port of src/llm/models.ts: model metadata parsing and discovery.
 * GPL-2.0-or-later. */
#ifndef QA_LLM_MODELS_H
#define QA_LLM_MODELS_H

#include "qa/llm/auth.h"

typedef enum qa_llm_reasoning_source {
  QA_LLM_REASONING_PROVIDER,
  QA_LLM_REASONING_REFERENCE,
  QA_LLM_REASONING_UNKNOWN,
} qa_llm_reasoning_source;

const char *qa_llm_reasoning_source_name(qa_llm_reasoning_source source);

/* LlmModel; default_reasoning_effort NULL mirrors null. */
typedef struct qa_llm_model {
  char *id;
  char *name;
  char **reasoning_efforts;
  size_t reasoning_effort_count;
  char *default_reasoning_effort;
  qa_llm_reasoning_source reasoning_source;
  bool recommended;
} qa_llm_model;

typedef struct qa_llm_model_list {
  qa_llm_model *items;
  size_t count;
  size_t capacity;
} qa_llm_model_list;

void qa_llm_model_clear(qa_llm_model *model);
void qa_llm_model_list_free(qa_llm_model_list *list);
void qa_llm_model_copy(const qa_llm_model *source, qa_llm_model *out);
void qa_llm_model_list_copy(const qa_llm_model_list *source, qa_llm_model_list *out);
bool qa_llm_model_has_effort(const qa_llm_model *model, const char *effort);

typedef enum qa_llm_catalog_status {
  QA_LLM_CATALOG_IDLE,
  QA_LLM_CATALOG_LOADING,
  QA_LLM_CATALOG_READY,
  QA_LLM_CATALOG_ERROR,
} qa_llm_catalog_status;

const char *qa_llm_catalog_status_name(qa_llm_catalog_status status);

/* LlmModelCatalog; message is set only for the error status. */
typedef struct qa_llm_model_catalog {
  qa_llm_catalog_status status;
  qa_llm_model_list models;
  char *message;
} qa_llm_model_catalog;

void qa_llm_model_catalog_free(qa_llm_model_catalog *catalog);
void qa_llm_model_catalog_copy(const qa_llm_model_catalog *source, qa_llm_model_catalog *out);

/* parseReasoningEffort(value): *out_effort NULL mirrors a null result. */
bool qa_llm_parse_reasoning_effort(const qa_llm_json *value, char **out_effort, qa_llm_error *error);
/* The same for the settings API's string | null argument. */
bool qa_llm_parse_reasoning_effort_string(const char *value, char **out_effort, qa_llm_error *error);
/* fastestReasoningEffort: borrowed effort or NULL (undefined). */
const char *qa_llm_fastest_reasoning_effort(const qa_llm_model *model);
void qa_llm_api_model(const char *id, qa_llm_model *out);
bool qa_llm_parse_api_models(const qa_llm_json *value, bool openai, qa_llm_model_list *out, qa_llm_error *error);
bool qa_llm_parse_codex_models(const qa_llm_json *value, qa_llm_model_list *out, qa_llm_error *error);
bool qa_llm_discover_api_models(const char *base_url, const char *api_key, bool openai, qa_llm_fetch_ref fetcher,
                                qa_llm_abort_signal *signal, qa_llm_model_list *out, qa_llm_error *error);
bool qa_llm_discover_codex_models(const qa_llm_subscription_credential *credential, qa_llm_fetch_ref fetcher,
                                  qa_llm_abort_signal *signal, qa_llm_model_list *out, qa_llm_error *error);

#endif
