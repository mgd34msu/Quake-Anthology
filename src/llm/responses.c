#define _POSIX_C_SOURCE 200809L
/* See include/qa/llm/responses.h. GPL-2.0-or-later. */
#include "qa/llm/responses.h"

#include "qa/llm/auth.h"
#include "qa/llm/sse.h"

#include <stdlib.h>
#include <string.h>

static bool object_or_error(const qa_llm_json *value, qa_llm_error *error) {
  if (!qa_llm_json_is_object(value)) {
    qa_llm_error_set(error, "Invalid LLM response.");
    return false;
  }
  return true;
}

static bool json_string_is(const qa_llm_json *value, const char *text) {
  return value != NULL && value->kind == QA_LLM_JSON_STRING && strcmp(value->string, text) == 0;
}

/* outputItem(): appends the item's text to out. */
static bool output_item(const qa_llm_json *value, const char *label, qa_llm_strbuf *out, qa_llm_error *error) {
  if (!object_or_error(value, error)) return false;
  const qa_llm_json *type = qa_llm_json_get(value, "type");
  if (!json_string_is(type, "message") && !json_string_is(type, "reasoning")) {
    qa_llm_error_setf(error, "%s returned an unsupported output item.", label);
    return false;
  }
  const qa_llm_json *content = qa_llm_json_get(value, "content");
  if (json_string_is(type, "message") && content != NULL) {
    if (!qa_llm_json_is_array(content)) {
      qa_llm_error_set(error, "Invalid LLM response.");
      return false;
    }
    for (size_t i = 0; i < content->count; i++) {
      const qa_llm_json *part = content->items[i];
      if (!object_or_error(part, error)) return false;
      const qa_llm_json *part_type = qa_llm_json_get(part, "type");
      if (json_string_is(part_type, "refusal")) {
        qa_llm_error_setf(error, "%s declined the request.", label);
        return false;
      }
      if (!json_string_is(part_type, "output_text")) {
        qa_llm_error_setf(error, "%s returned unsupported content.", label);
        return false;
      }
      const qa_llm_json *text = qa_llm_json_get(part, "text");
      if (text == NULL || text->kind != QA_LLM_JSON_STRING) {
        qa_llm_error_set(error, "Invalid LLM response.");
        return false;
      }
      qa_llm_strbuf_append(out, text->string);
    }
  }
  return true;
}

typedef struct responses_state {
  const qa_llm_transport_request *input;
  const char *label;
  qa_llm_strbuf text;
  bool completed;
} responses_state;

static qa_llm_sse_action responses_event(void *ctx, const qa_llm_sse_event *event, qa_llm_error *error) {
  responses_state *state = ctx;
  const char *label = state->label;
  if (strcmp(event->data, "[DONE]") == 0) return QA_LLM_SSE_CONTINUE;
  qa_llm_json *value = NULL;
  if (!qa_llm_json_parse(event->data, strlen(event->data), &value)) {
    qa_llm_error_set(error, "Invalid LLM response.");
    return QA_LLM_SSE_FAIL;
  }
  qa_llm_sse_action action = QA_LLM_SSE_CONTINUE;
  if (!object_or_error(value, error)) { action = QA_LLM_SSE_FAIL; goto done; }
  {
    const qa_llm_json *type_json = qa_llm_json_get(value, "type");
    /* item["type"] ?? event.event */
    const char *type = type_json != NULL && type_json->kind == QA_LLM_JSON_STRING ? type_json->string
        : type_json == NULL || type_json->kind == QA_LLM_JSON_NULL ? event->event : NULL;
    if (type != NULL && (strcmp(type, "response.failed") == 0 || strcmp(type, "response.incomplete") == 0 || strcmp(type, "error") == 0)) {
      qa_llm_error_setf(error, "%s response failed. Try again.", label);
      action = QA_LLM_SSE_FAIL;
      goto done;
    }
    if (type != NULL && qa_llm_starts_with(type, "response.refusal.")) {
      qa_llm_error_setf(error, "%s declined the request.", label);
      action = QA_LLM_SSE_FAIL;
      goto done;
    }
    if (type != NULL && qa_llm_starts_with(type, "response.function_call_arguments.")) {
      qa_llm_error_setf(error, "%s returned an unsupported tool call.", label);
      action = QA_LLM_SSE_FAIL;
      goto done;
    }
    if (type != NULL && (strcmp(type, "response.output_item.added") == 0 || strcmp(type, "response.output_item.done") == 0)) {
      qa_llm_strbuf ignored;
      qa_llm_strbuf_init(&ignored);
      bool item_ok = output_item(qa_llm_json_get(value, "item"), label, &ignored, error);
      qa_llm_strbuf_free(&ignored);
      if (!item_ok) { action = QA_LLM_SSE_FAIL; goto done; }
    }
    if (type != NULL && (strcmp(type, "response.content_part.added") == 0 || strcmp(type, "response.content_part.done") == 0)) {
      const qa_llm_json *part = qa_llm_json_get(value, "part");
      if (!object_or_error(part, error)) { action = QA_LLM_SSE_FAIL; goto done; }
      if (json_string_is(qa_llm_json_get(part, "type"), "refusal")) {
        qa_llm_error_setf(error, "%s declined the request.", label);
        action = QA_LLM_SSE_FAIL;
        goto done;
      }
    }
    if (type != NULL && strcmp(type, "response.output_text.delta") == 0) {
      const qa_llm_json *delta = qa_llm_json_get(value, "delta");
      if (state->completed || delta == NULL || delta->kind != QA_LLM_JSON_STRING) {
        qa_llm_error_set(error, "Invalid LLM response.");
        action = QA_LLM_SSE_FAIL;
        goto done;
      }
      qa_llm_strbuf_append(&state->text, delta->string);
      if (state->input->input.on_text != NULL) state->input->input.on_text(state->input->input.on_text_ctx, delta->string);
    } else if (type != NULL && strcmp(type, "response.completed") == 0) {
      const qa_llm_json *result = qa_llm_json_get(value, "response");
      if (!object_or_error(result, error)) { action = QA_LLM_SSE_FAIL; goto done; }
      if (!json_string_is(qa_llm_json_get(result, "status"), "completed")) {
        qa_llm_error_setf(error, "%s response did not complete. Try again.", label);
        action = QA_LLM_SSE_FAIL;
        goto done;
      }
      const qa_llm_json *output = qa_llm_json_get(result, "output");
      if (output != NULL) {
        if (!qa_llm_json_is_array(output)) {
          qa_llm_error_set(error, "Invalid LLM response.");
          action = QA_LLM_SSE_FAIL;
          goto done;
        }
        qa_llm_strbuf final_text;
        qa_llm_strbuf_init(&final_text);
        bool output_ok = true;
        for (size_t i = 0; i < output->count; i++) {
          if (!output_item(output->items[i], label, &final_text, error)) { output_ok = false; break; }
        }
        const char *final = qa_llm_strbuf_text(&final_text);
        const char *current = qa_llm_strbuf_text(&state->text);
        if (output_ok && output->count > 0 && current[0] != '\0' && strcmp(final, current) != 0) {
          qa_llm_error_setf(error, "%s returned inconsistent response text.", label);
          output_ok = false;
        }
        if (output_ok && current[0] == '\0' && final[0] != '\0') {
          qa_llm_strbuf_append(&state->text, final);
          if (state->input->input.on_text != NULL) state->input->input.on_text(state->input->input.on_text_ctx, final);
        }
        qa_llm_strbuf_free(&final_text);
        if (!output_ok) { action = QA_LLM_SSE_FAIL; goto done; }
      }
      state->completed = true;
      action = QA_LLM_SSE_STOP;
    }
  }
done:
  qa_llm_json_free(value);
  return action;
}

bool qa_llm_read_responses(const qa_llm_transport_request *input, qa_llm_http_response *response,
                           const char *label, bool require_content_type, char **out_text, qa_llm_error *error) {
  responses_state state;
  memset(&state, 0, sizeof state);
  state.input = input;
  state.label = label;
  qa_llm_strbuf_init(&state.text);
  qa_llm_http_response *resp = response;
  bool ok = qa_llm_consume_sse(resp, input->input.signal, responses_event, &state, require_content_type, error);
  if (ok && !state.completed) {
    qa_llm_error_setf(error, "%s response ended before completion. Try again.", label);
    ok = false;
  }
  if (ok && qa_llm_trim_is_empty(qa_llm_strbuf_text(&state.text))) {
    qa_llm_error_setf(error, "%s returned no text. Try again.", label);
    ok = false;
  }
  if (ok) *out_text = qa_llm_strbuf_steal(&state.text);
  else qa_llm_strbuf_free(&state.text);
  return ok;
}
