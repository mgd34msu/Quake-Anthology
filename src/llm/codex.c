#define _POSIX_C_SOURCE 200809L
/* See include/qa/llm/codex.h. GPL-2.0-or-later. */
#include "qa/llm/codex.h"

#include "qa/llm/responses.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

/* model.replace(/^openai[:/]/, "") */
static const char *strip_openai_prefix(const char *model) {
  if (qa_llm_starts_with(model, "openai:") || qa_llm_starts_with(model, "openai/")) return model + 7;
  return model;
}

bool qa_llm_subscription_account_id(const char *token, char **out_id, qa_llm_error *error) {
  bool ok = false;
  qa_llm_bytes decoded;
  qa_llm_bytes_init(&decoded);
  qa_llm_json *parsed = NULL;
  const char *first_dot = strchr(token, '.');
  if (first_dot == NULL) goto done;
  {
    const char *payload = first_dot + 1;
    const char *second_dot = strchr(payload, '.');
    size_t length = second_dot != NULL ? (size_t)(second_dot - payload) : strlen(payload);
    if (length == 0) goto done;
    for (size_t i = 0; i < length; i++) {
      char c = payload[i];
      bool valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
      if (!valid) goto done;
    }
    char *payload_text = qa_llm_strndup(payload, length);
    bool decoded_ok = qa_llm_base64url_decode(payload_text, length, &decoded);
    free(payload_text);
    if (!decoded_ok) goto done;
    /* Buffer.toString("utf8") (invalid bytes become U+FFFD) then JSON.parse */
    qa_llm_bytes text;
    qa_llm_bytes_init(&text);
    qa_llm_utf8_decode_lossy(decoded.data, decoded.length, &text);
    bool parsed_ok = qa_llm_json_parse((const char *)text.data, text.length, &parsed);
    qa_llm_bytes_free(&text);
    if (!parsed_ok) goto done;
    qa_llm_error inner;
    if (!qa_llm_record(parsed, &inner)) goto done;
    const qa_llm_json *auth = qa_llm_json_get(parsed, "https://api.openai.com/auth");
    if (!qa_llm_record(auth, &inner)) goto done;
    const qa_llm_json *id = qa_llm_json_get(auth, "chatgpt_account_id");
    if (id == NULL || id->kind != QA_LLM_JSON_STRING) goto done;
    if (qa_llm_trim_is_empty(id->string)) goto done;
    /* /[\x00-\x20\x7f]/ anywhere rejects */
    bool bad = false;
    for (const uint8_t *p = (const uint8_t *)id->string; *p != '\0'; p++) {
      if (*p <= 0x20 || *p == 0x7f) bad = true;
    }
    if (bad) goto done;
    *out_id = qa_llm_strdup(id->string);
    ok = true;
  }
done:
  qa_llm_json_free(parsed);
  qa_llm_bytes_free(&decoded);
  if (!ok) qa_llm_error_set(error, "Subscription credential has no valid ChatGPT account ID. Sign in again.");
  return ok;
}

char *qa_llm_prompt_cache_key(const char *model) {
  qa_llm_strbuf text;
  qa_llm_strbuf_init(&text);
  qa_llm_strbuf_appendf(&text, "quake-typescript:%s", strip_openai_prefix(model));
  char digest[65];
  qa_llm_sha256_hex(text.data, text.length, digest);
  qa_llm_strbuf_free(&text);
  return qa_llm_strdup(digest);
}

/* node:os platform()/release()/arch() approximations via uname. */
static void os_fields(char *platform, size_t platform_cap, char *release, size_t release_cap, char *arch, size_t arch_cap) {
  struct utsname name;
  const char *sysname = "linux";
  const char *machine = "x64";
  const char *version = "";
  if (uname(&name) == 0) {
    sysname = name.sysname;
    machine = name.machine;
    version = name.release;
  }
  const char *platform_name = strcmp(sysname, "Linux") == 0 ? "linux"
      : strcmp(sysname, "Darwin") == 0 ? "darwin"
      : strncmp(sysname, "Windows", 7) == 0 || strncmp(sysname, "CYGWIN", 6) == 0 || strncmp(sysname, "MINGW", 5) == 0 ? "win32" : "linux";
  const char *arch_name = strcmp(machine, "x86_64") == 0 || strcmp(machine, "amd64") == 0 ? "x64"
      : strcmp(machine, "aarch64") == 0 || strcmp(machine, "arm64") == 0 ? "arm64"
      : strcmp(machine, "i386") == 0 || strcmp(machine, "i686") == 0 ? "ia32" : machine;
  snprintf(platform, platform_cap, "%s", platform_name);
  snprintf(release, release_cap, "%s", version);
  snprintf(arch, arch_cap, "%s", arch_name);
}

bool qa_llm_request_codex(const qa_llm_transport_request *input, const qa_llm_subscription_credential *credential,
                          qa_llm_fetch_ref fetcher, char **out_text, qa_llm_error *error) {
  char *id = NULL;
  if (!qa_llm_subscription_account_id(credential->access_token, &id, error)) return false;
  char generated[37];
  const char *session = input->input.session_id;
  if (session == NULL) {
    qa_llm_random_uuid(generated);
    session = generated;
  }
  const char *model = strip_openai_prefix(input->model);
  char *cache_key = qa_llm_prompt_cache_key(model);
  char *trimmed = qa_llm_trim_dup(input->input.instructions);
  /* TS: input.instructions.trim() || "You are a helpful assistant." */
  const char *instructions = trimmed[0] != '\0' ? trimmed : "You are a helpful assistant.";

  qa_llm_json *body = qa_llm_json_object();
  qa_llm_json_object_set(body, "model", qa_llm_json_string(model));
  qa_llm_json_object_set(body, "store", qa_llm_json_bool(false));
  qa_llm_json_object_set(body, "stream", qa_llm_json_bool(true));
  qa_llm_json_object_set(body, "instructions", qa_llm_json_string(instructions));
  {
    qa_llm_json *input_array = qa_llm_json_array();
    qa_llm_json *message = qa_llm_json_object();
    qa_llm_json_object_set(message, "role", qa_llm_json_string("user"));
    qa_llm_json *content = qa_llm_json_array();
    qa_llm_json *part = qa_llm_json_object();
    qa_llm_json_object_set(part, "type", qa_llm_json_string("input_text"));
    qa_llm_json_object_set(part, "text", qa_llm_json_string(input->input.prompt));
    qa_llm_json_array_push(content, part);
    qa_llm_json_object_set(message, "content", content);
    qa_llm_json_array_push(input_array, message);
    qa_llm_json_object_set(body, "input", input_array);
  }
  if (input->reasoning_effort != NULL) {
    qa_llm_json *reasoning = qa_llm_json_object();
    qa_llm_json_object_set(reasoning, "effort", qa_llm_json_string(input->reasoning_effort));
    qa_llm_json_object_set(body, "reasoning", reasoning);
  }
  {
    qa_llm_json *text = qa_llm_json_object();
    qa_llm_json_object_set(text, "verbosity", qa_llm_json_string("low"));
    qa_llm_json_object_set(body, "text", text);
  }
  qa_llm_json_object_set(body, "prompt_cache_key", qa_llm_json_string(cache_key));
  char *body_text = qa_llm_json_stringify(body);
  qa_llm_json_free(body);

  qa_llm_strbuf authorization;
  qa_llm_strbuf_init(&authorization);
  qa_llm_strbuf_appendf(&authorization, "Bearer %s", credential->access_token);
  char platform[32], release[128], arch[128];
  os_fields(platform, sizeof platform, release, sizeof release, arch, sizeof arch);
  qa_llm_strbuf user_agent;
  qa_llm_strbuf_init(&user_agent);
  qa_llm_strbuf_appendf(&user_agent, "pi (%s %s; %s)", platform, release, arch);

  qa_llm_header headers[] = {
    { "authorization", authorization.data },
    { "chatgpt-account-id", id },
    { "originator", "pi" },
    { "OpenAI-Beta", "responses=experimental" },
    { "accept", "text/event-stream" },
    { "content-type", "application/json" },
    { "User-Agent", user_agent.data },
    { "session_id", session },
  };
  qa_llm_http_init init;
  memset(&init, 0, sizeof init);
  init.method = "POST";
  init.headers = headers;
  init.header_count = sizeof headers / sizeof headers[0];
  init.body = body_text;
  init.body_length = strlen(body_text);
  init.signal = input->input.signal;

  qa_llm_http_response response;
  bool ok = qa_llm_fetch_llm_response("https://chatgpt.com/backend-api/codex/responses", &init, fetcher,
                                      input->input.signal, &response, error);
  if (ok && !qa_llm_response_ok(&response)) {
    int status = response.status;
    qa_llm_cancel_response_body(&response);
    qa_llm_response_dispose(&response);
    qa_llm_error_http(error, status);
    ok = false;
  }
  if (ok) {
    ok = qa_llm_read_responses(input, &response, "Subscription", false, out_text, error);
    qa_llm_response_dispose(&response);
  }
  qa_llm_strbuf_free(&authorization);
  qa_llm_strbuf_free(&user_agent);
  free(body_text);
  free(cache_key);
  free(trimmed);
  free(id);
  return ok;
}
