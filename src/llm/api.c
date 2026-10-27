#define _POSIX_C_SOURCE 200809L
/* See include/qa/llm/api.h. GPL-2.0-or-later. */
#include "qa/llm/api.h"

#include "qa/llm/auth.h"
#include "qa/llm/responses.h"
#include "qa/llm/sse.h"

#include <stdlib.h>
#include <string.h>

typedef struct chat_state {
  const qa_llm_transport_request *input;
  qa_llm_strbuf text;
  bool finished;
  bool done;
} chat_state;

static qa_llm_sse_action chat_event(void *ctx, const qa_llm_sse_event *event, qa_llm_error *error) {
  chat_state *state = ctx;
  if (strcmp(event->data, "[DONE]") == 0) {
    state->done = true;
    return QA_LLM_SSE_STOP;
  }
  qa_llm_json *value = NULL;
  if (!qa_llm_json_parse(event->data, strlen(event->data), &value)) {
    qa_llm_error_set(error, "LLM service returned invalid stream data.");
    return QA_LLM_SSE_FAIL;
  }
  qa_llm_sse_action action = QA_LLM_SSE_CONTINUE;
  if (!qa_llm_record(value, error)) { action = QA_LLM_SSE_FAIL; goto done; }
  {
    const qa_llm_json *choices = qa_llm_json_get(value, "choices");
    if (qa_llm_json_get(value, "error") != NULL || strcmp(event->event, "error") == 0) {
      qa_llm_error_set(error, "LLM service reported a response error.");
      action = QA_LLM_SSE_FAIL;
      goto done;
    }
    if (!qa_llm_json_is_array(choices)) {
      qa_llm_error_set(error, "LLM service returned invalid completion data.");
      action = QA_LLM_SSE_FAIL;
      goto done;
    }
    for (size_t i = 0; i < choices->count; i++) {
      const qa_llm_json *choice = choices->items[i];
      if (!qa_llm_record(choice, error)) { action = QA_LLM_SSE_FAIL; goto done; }
      const qa_llm_json *index = qa_llm_json_get(choice, "index");
      if (index == NULL || index->kind != QA_LLM_JSON_NUMBER || index->number != 0) continue;
      const qa_llm_json *delta = qa_llm_json_get(choice, "delta");
      if (!qa_llm_record(delta, error)) { action = QA_LLM_SSE_FAIL; goto done; }
      const qa_llm_json *content = qa_llm_json_get(delta, "content");
      const qa_llm_json *finish = qa_llm_json_get(choice, "finish_reason");
      if (qa_llm_json_get(delta, "tool_calls") != NULL || qa_llm_json_get(delta, "function_call") != NULL) {
        qa_llm_error_set(error, "LLM service returned an unsupported tool call.");
        action = QA_LLM_SSE_FAIL;
        goto done;
      }
      const qa_llm_json *refusal = qa_llm_json_get(delta, "refusal");
      if (refusal != NULL && refusal->kind == QA_LLM_JSON_STRING && refusal->string[0] != '\0') {
        qa_llm_error_set(error, "LLM service declined the request.");
        action = QA_LLM_SSE_FAIL;
        goto done;
      }
      if (content != NULL && content->kind != QA_LLM_JSON_NULL) {
        if (content->kind != QA_LLM_JSON_STRING || state->finished) {
          qa_llm_error_set(error, "LLM service returned invalid completion content.");
          action = QA_LLM_SSE_FAIL;
          goto done;
        }
        qa_llm_strbuf_append(&state->text, content->string);
        if (content->string[0] != '\0' && state->input->input.on_text != NULL) {
          state->input->input.on_text(state->input->input.on_text_ctx, content->string);
        }
      }
      if (finish != NULL && finish->kind != QA_LLM_JSON_NULL) {
        if (!(finish->kind == QA_LLM_JSON_STRING && strcmp(finish->string, "stop") == 0)) {
          qa_llm_error_set(error, "LLM response was incomplete or declined. Try a shorter request.");
          action = QA_LLM_SSE_FAIL;
          goto done;
        }
        state->finished = true;
      }
    }
  }
done:
  qa_llm_json_free(value);
  return action;
}

static char *bearer(const char *api_key) {
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  qa_llm_strbuf_append(&out, "Bearer ");
  qa_llm_strbuf_append(&out, api_key);
  return qa_llm_strbuf_steal(&out);
}

bool qa_llm_request_chat_completions(const qa_llm_transport_request *input, const qa_llm_api_credential *credential,
                                     qa_llm_fetch_ref fetcher, char **out_text, qa_llm_error *error) {
  char *base = qa_llm_strip_trailing_slashes(credential->base_url);
  qa_llm_strbuf url;
  qa_llm_strbuf_init(&url);
  qa_llm_strbuf_appendf(&url, "%s/chat/completions", base);
  free(base);

  qa_llm_json *body = qa_llm_json_object();
  qa_llm_json_object_set(body, "model", qa_llm_json_string(input->model));
  if (input->reasoning_effort != NULL) qa_llm_json_object_set(body, "reasoning_effort", qa_llm_json_string(input->reasoning_effort));
  qa_llm_json *messages = qa_llm_json_array();
  qa_llm_json *system_message = qa_llm_json_object();
  qa_llm_json_object_set(system_message, "role", qa_llm_json_string("system"));
  qa_llm_json_object_set(system_message, "content", qa_llm_json_string(input->input.instructions));
  qa_llm_json_array_push(messages, system_message);
  qa_llm_json *user_message = qa_llm_json_object();
  qa_llm_json_object_set(user_message, "role", qa_llm_json_string("user"));
  qa_llm_json_object_set(user_message, "content", qa_llm_json_string(input->input.prompt));
  qa_llm_json_array_push(messages, user_message);
  qa_llm_json_object_set(body, "messages", messages);
  qa_llm_json_object_set(body, "stream", qa_llm_json_bool(true));
  char *body_text = qa_llm_json_stringify(body);
  qa_llm_json_free(body);

  char *authorization = bearer(credential->api_key);
  qa_llm_header headers[] = {
    { "authorization", authorization },
    { "content-type", "application/json" },
    { "accept", "text/event-stream" },
  };
  qa_llm_http_init init;
  memset(&init, 0, sizeof init);
  init.method = "POST";
  init.headers = headers;
  init.header_count = 3;
  init.body = body_text;
  init.body_length = strlen(body_text);

  qa_llm_http_response response;
  bool ok = qa_llm_fetch_llm_response(qa_llm_strbuf_text(&url), &init, fetcher, input->input.signal, &response, error);
  if (ok && !qa_llm_response_ok(&response)) {
    int status = response.status;
    qa_llm_cancel_response_body(&response);
    qa_llm_response_dispose(&response);
    qa_llm_error_http(error, status);
    ok = false;
  }
  if (ok) {
    chat_state state;
    memset(&state, 0, sizeof state);
    state.input = input;
    qa_llm_strbuf_init(&state.text);
    ok = qa_llm_consume_sse(&response, input->input.signal, chat_event, &state, true, error);
    if (ok && (!state.done || !state.finished)) {
      qa_llm_error_set(error, "LLM response ended before completion. Try again.");
      ok = false;
    }
    if (ok && qa_llm_trim_is_empty(qa_llm_strbuf_text(&state.text))) {
      qa_llm_error_set(error, "LLM service returned an empty response.");
      ok = false;
    }
    if (ok) *out_text = qa_llm_strbuf_steal(&state.text);
    else qa_llm_strbuf_free(&state.text);
    qa_llm_response_dispose(&response);
  }
  free(authorization);
  free(body_text);
  qa_llm_strbuf_free(&url);
  return ok;
}

static qa_llm_json *responses_input(const char *prompt) {
  qa_llm_json *input = qa_llm_json_array();
  qa_llm_json *message = qa_llm_json_object();
  qa_llm_json_object_set(message, "role", qa_llm_json_string("user"));
  qa_llm_json *content = qa_llm_json_array();
  qa_llm_json *part = qa_llm_json_object();
  qa_llm_json_object_set(part, "type", qa_llm_json_string("input_text"));
  qa_llm_json_object_set(part, "text", qa_llm_json_string(prompt));
  qa_llm_json_array_push(content, part);
  qa_llm_json_object_set(message, "content", content);
  qa_llm_json_array_push(input, message);
  return input;
}

bool qa_llm_request_open_ai_responses(const qa_llm_transport_request *input, const char *api_key,
                                      qa_llm_fetch_ref fetcher, char **out_text, qa_llm_error *error) {
  qa_llm_json *body = qa_llm_json_object();
  qa_llm_json_object_set(body, "model", qa_llm_json_string(input->model));
  qa_llm_json_object_set(body, "instructions", qa_llm_json_string(input->input.instructions));
  qa_llm_json_object_set(body, "input", responses_input(input->input.prompt));
  qa_llm_json_object_set(body, "store", qa_llm_json_bool(false));
  qa_llm_json_object_set(body, "stream", qa_llm_json_bool(true));
  if (input->input.session_id != NULL) qa_llm_json_object_set(body, "prompt_cache_key", qa_llm_json_string(input->input.session_id));
  if (input->reasoning_effort != NULL) {
    qa_llm_json *reasoning = qa_llm_json_object();
    qa_llm_json_object_set(reasoning, "effort", qa_llm_json_string(input->reasoning_effort));
    qa_llm_json_object_set(body, "reasoning", reasoning);
  }
  char *body_text = qa_llm_json_stringify(body);
  qa_llm_json_free(body);

  char *authorization = bearer(api_key);
  qa_llm_header headers[] = {
    { "authorization", authorization },
    { "content-type", "application/json" },
    { "accept", "text/event-stream" },
  };
  qa_llm_http_init init;
  memset(&init, 0, sizeof init);
  init.method = "POST";
  init.headers = headers;
  init.header_count = 3;
  init.body = body_text;
  init.body_length = strlen(body_text);

  qa_llm_http_response response;
  bool ok = qa_llm_fetch_llm_response("https://api.openai.com/v1/responses", &init, fetcher, input->input.signal, &response, error);
  if (ok && !qa_llm_response_ok(&response)) {
    int status = response.status;
    qa_llm_cancel_response_body(&response);
    qa_llm_response_dispose(&response);
    qa_llm_error_http(error, status);
    ok = false;
  }
  if (ok) {
    ok = qa_llm_read_responses(input, &response, "OpenAI API", true, out_text, error);
    qa_llm_response_dispose(&response);
  }
  free(authorization);
  free(body_text);
  return ok;
}
