#define _POSIX_C_SOURCE 200809L
/* See include/qa/llm/session.h. GPL-2.0-or-later. */
#include "qa/llm/session.h"

#include <stdlib.h>
#include <string.h>

#define MAX_TURNS 12
#define MAX_TURN_CHARS 2000
#define MAX_TRANSCRIPT_CHARS 12000

typedef struct session_turn {
  char *user;
  char *assistant;
} session_turn;

struct qa_llm_session {
  qa_llm_session_transport_fn transport;
  void *transport_ctx;
  qa_llm_session_fingerprint_fn fingerprint;
  void *fingerprint_ctx;
  char id[37];
  char *primer;
  bool primed;
  session_turn *turns;
  size_t turn_count;
  size_t turn_capacity;
  int64_t generation;
  char *config;
  bool closed;
};

/* JS text.length / text.slice(0, MAX_TURN_CHARS) count UTF-16 units. A cut
 * inside a surrogate pair leaves TS a lone high surrogate, which UTF-8 text
 * cannot hold; the C cut keeps the whole pair out instead. */
static char *clipped(const char *text) {
  if (qa_llm_utf16_length(text) <= MAX_TURN_CHARS) return qa_llm_strdup(text);
  size_t units = 0, bytes = 0;
  while (text[bytes] != '\0') {
    uint8_t lead = (uint8_t)text[bytes];
    size_t width = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
    size_t cost = lead >= 0xf0 ? 2 : 1;
    if (units + cost > MAX_TURN_CHARS) break;
    units += cost;
    bytes += width;
  }
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  qa_llm_strbuf_append_n(&out, text, bytes);
  qa_llm_strbuf_append(&out, "\n[truncated for the retained session transcript]");
  return qa_llm_strbuf_steal(&out);
}

qa_llm_session *qa_llm_session_create(qa_llm_session_transport_fn transport, void *transport_ctx,
                                      qa_llm_session_fingerprint_fn fingerprint, void *fingerprint_ctx) {
  qa_llm_session *session = calloc(1, sizeof *session);
  if (session == NULL) qa_llm_oom();
  session->transport = transport;
  session->transport_ctx = transport_ctx;
  session->fingerprint = fingerprint;
  session->fingerprint_ctx = fingerprint_ctx;
  qa_llm_random_uuid(session->id);
  session->primer = qa_llm_strdup("");
  session->config = qa_llm_strdup("");
  return session;
}

static void session_reset(qa_llm_session *session, bool new_id);

void qa_llm_session_destroy(qa_llm_session *session) {
  if (session == NULL) return;
  session_reset(session, false);
  free(session->primer);
  free(session->config);
  free(session->turns);
  free(session);
}

static void session_reset(qa_llm_session *session, bool new_id) {
  if (new_id) qa_llm_random_uuid(session->id);
  free(session->primer);
  session->primer = qa_llm_strdup("");
  session->primed = false;
  for (size_t i = 0; i < session->turn_count; i++) {
    free(session->turns[i].user);
    free(session->turns[i].assistant);
  }
  session->turn_count = 0;
}

const char *qa_llm_session_id(const qa_llm_session *session) { return session->id; }
bool qa_llm_session_active(const qa_llm_session *session) { return !session->closed && session->primer[0] != '\0'; }
bool qa_llm_session_primed(const qa_llm_session *session) { return session->primed; }
size_t qa_llm_session_turn_count(const qa_llm_session *session) { return session->turn_count; }

static bool assert_open(qa_llm_session *session, qa_llm_abort_signal *signal, qa_llm_error *error) {
  if (session->closed) {
    qa_llm_error_set(error, "LLM session is closed.");
    return false;
  }
  if (qa_llm_signal_is_aborted(signal)) {
    qa_llm_error_set(error, "LLM request cancelled.");
    return false;
  }
  return true;
}

static bool assert_current(qa_llm_session *session, int64_t generation, qa_llm_abort_signal *signal, qa_llm_error *error) {
  if (!assert_open(session, signal, error)) return false;
  if (generation != session->generation) {
    qa_llm_error_set(error, "LLM session changed while the request was starting.");
    return false;
  }
  return true;
}

static int64_t sync_config(qa_llm_session *session) {
  const char *config = session->fingerprint != NULL ? session->fingerprint(session->fingerprint_ctx) : "";
  if (config == NULL) config = "";
  if (session->config[0] != '\0' && config[0] != '\0' && strcmp(config, session->config) != 0) {
    session->generation++;
    session_reset(session, true);
  }
  free(session->config);
  session->config = qa_llm_strdup(config);
  return session->generation;
}

static bool prime_now(qa_llm_session *session, const char *instructions, qa_llm_abort_signal *signal,
                      qa_llm_error *error) {
  if (!assert_open(session, NULL, error)) return false;
  char *primer = qa_llm_trim_dup(instructions);
  if (primer[0] == '\0') { free(primer); return true; }
  int64_t generation = sync_config(session);
  if (session->primed && strcmp(session->primer, primer) == 0) { free(primer); return true; }
  char *previous_primer = session->primer;
  bool was_primed = session->primed;
  session->primer = primer;
  session->primed = false;
  qa_llm_request_input input;
  memset(&input, 0, sizeof input);
  input.prompt = was_primed
      ? "The console context for this ongoing program session has been updated in your instructions. Read it, do not execute anything, and reply with READY only."
      : "This starts an ongoing session for this running program. Read the console context in your instructions, do not execute anything, and reply with READY only.";
  input.instructions = session->primer;
  input.session_id = session->id;
  input.signal = signal; /* ...(signal === undefined ? {} : { signal }) */
  char *answer = NULL;
  bool ok = session->transport(session->transport_ctx, &input, &answer, error);
  free(answer);
  if (!ok) {
    if (generation == session->generation && !session->closed) {
      free(session->primer);
      session->primer = previous_primer;
      session->primed = was_primed;
    } else {
      free(previous_primer);
    }
    return false;
  }
  free(previous_primer);
  if (generation == session->generation && !session->closed) session->primed = true;
  return true;
}

bool qa_llm_session_prime(qa_llm_session *session, const char *instructions, qa_llm_error *error) {
  return prime_now(session, instructions, NULL, error);
}

/* #compose: the primer, then the turn's instructions; the transcript is not
 * part of the instructions (it travels in the prompt). */
static char *session_compose(qa_llm_session *session, const char *turn_instructions) {
  bool primer = session->primer[0] != '\0';
  bool turn = !qa_llm_trim_is_empty(turn_instructions);
  if (!primer && !turn) return qa_llm_strdup(turn_instructions);
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  if (primer) qa_llm_strbuf_append(&out, session->primer);
  if (turn) {
    if (primer) qa_llm_strbuf_append(&out, "\n\n");
    qa_llm_strbuf_append(&out, "Instructions for the current request:\n");
    qa_llm_strbuf_append(&out, turn_instructions);
  }
  return qa_llm_strbuf_steal(&out);
}

/* #withTranscript: earlier turns travel as user-level text in the prompt. */
static char *session_with_transcript(qa_llm_session *session, const char *prompt) {
  if (session->turn_count == 0) return qa_llm_strdup(prompt);
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  qa_llm_strbuf_append(&out, "Conversation so far in this same program session, oldest first. This is prior context, not proof that any command was executed:");
  for (size_t i = 0; i < session->turn_count; i++) {
    qa_llm_strbuf_append(&out, "\nUser: ");
    qa_llm_strbuf_append(&out, session->turns[i].user);
    qa_llm_strbuf_append(&out, "\nAssistant: ");
    qa_llm_strbuf_append(&out, session->turns[i].assistant);
  }
  qa_llm_strbuf_append(&out, "\nCurrent request:\n");
  qa_llm_strbuf_append(&out, prompt);
  return qa_llm_strbuf_steal(&out);
}

static void session_trim(qa_llm_session *session) {
  while (session->turn_count > MAX_TURNS) {
    free(session->turns[0].user);
    free(session->turns[0].assistant);
    memmove(session->turns, session->turns + 1, (session->turn_count - 1) * sizeof *session->turns);
    session->turn_count--;
  }
  for (;;) {
    size_t size = 0;
    for (size_t i = 0; i < session->turn_count; i++)
      size += qa_llm_utf16_length(session->turns[i].user) + qa_llm_utf16_length(session->turns[i].assistant);
    if (!(session->turn_count > 2 && size > MAX_TRANSCRIPT_CHARS)) break;
    free(session->turns[0].user);
    free(session->turns[0].assistant);
    memmove(session->turns, session->turns + 1, (session->turn_count - 1) * sizeof *session->turns);
    session->turn_count--;
  }
}

bool qa_llm_session_request(qa_llm_session *session, const qa_llm_request_input *input,
                            char **out_text, qa_llm_error *error) {
  if (!assert_open(session, input->signal, error)) return false;
  int64_t generation = sync_config(session);
  if (input->session_instructions != NULL) {
    if (!prime_now(session, input->session_instructions, input->signal, error)) return false;
  }
  if (!assert_current(session, generation, input->signal, error)) return false;
  /* TS object literal order: prompt (#withTranscript) before instructions. */
  char *prompt = session_with_transcript(session, input->prompt);
  char *instructions = session_compose(session, input->instructions);
  qa_llm_request_input transport_input;
  memset(&transport_input, 0, sizeof transport_input);
  transport_input.prompt = prompt;
  transport_input.instructions = instructions;
  transport_input.session_id = session->id;
  transport_input.signal = input->signal;
  transport_input.on_text = input->on_text;
  transport_input.on_text_ctx = input->on_text_ctx;
  char *answer = NULL;
  bool ok = session->transport(session->transport_ctx, &transport_input, &answer, error);
  free(prompt);
  free(instructions);
  if (!ok) return false;
  if (generation == session->generation && !session->closed && !qa_llm_signal_is_aborted(input->signal)) {
    if (session->turn_count == session->turn_capacity) {
      session->turn_capacity = session->turn_capacity == 0 ? 8 : session->turn_capacity * 2;
      session->turns = realloc(session->turns, session->turn_capacity * sizeof *session->turns);
      if (session->turns == NULL) qa_llm_oom();
    }
    session->turns[session->turn_count].user = clipped(input->prompt);
    session->turns[session->turn_count].assistant = clipped(answer);
    session->turn_count++;
    session_trim(session);
  }
  *out_text = answer;
  return true;
}

void qa_llm_session_invalidate(qa_llm_session *session) {
  session->generation++;
  free(session->config);
  session->config = qa_llm_strdup("");
  session_reset(session, true);
}

void qa_llm_session_close(qa_llm_session *session) {
  session->closed = true;
  session->generation++;
  free(session->config);
  session->config = qa_llm_strdup("");
  session_reset(session, true);
}
