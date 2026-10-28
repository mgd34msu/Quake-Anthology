/* See include/qa/ui/common/legacy/script/precomp_memory.h.
 * GPL-2.0-or-later. */
#include "qa/ui/common/legacy/script/precomp_memory.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

struct qa_ui_precomp_token {
  int64_t id;
  qa_ui_script_allocation allocation;
  qa_ui_source_token_memory *token;
  qa_ui_source_token_context context;
  char *unsupported;
  bool owns_allocation_bytes; /* local tokens malloc'd outside memory */
};

struct qa_ui_precomp_define {
  int64_t id;
  qa_ui_script_allocation allocation;
  qa_ui_precomp_memory *owner;
};

struct qa_ui_precomp_memory {
  const qa_ui_script_memory *memory;
  qa_ui_precomp_token **tokens; /* index id-1 */
  size_t token_count, token_cap;
  qa_ui_precomp_define **defines;
  size_t define_count, define_cap;
  int64_t next_token, next_define;
  size_t live_tokens;
};

static uint8_t *allocation_borrow(void *ctx, size_t *out_length) {
  qa_ui_script_allocation *allocation = ctx;
  *out_length = allocation->length;
  return allocation->bytes;
}

qa_ui_precomp_token *qa_ui_precomp_token_create(
    int64_t id, qa_ui_script_allocation allocation) {
  qa_ui_precomp_token *token = calloc(1, sizeof *token);
  if (token == NULL) return NULL;
  token->id = id;
  token->allocation = allocation;
  token->token = qa_ui_source_token_memory_create(allocation_borrow,
                                                  &token->allocation);
  bool context_ok = qa_ui_source_token_context_set(&token->context, "", 1,
                                                   "", 0);
  if (token->token == NULL || !context_ok) {
    qa_ui_precomp_token_destroy(token);
    return NULL;
  }
  return token;
}

qa_ui_precomp_token *qa_ui_precomp_token_local(void) {
  qa_ui_script_allocation allocation;
  memset(&allocation, 0, sizeof allocation);
  allocation.bytes = calloc(QA_UI_SOURCE_TOKEN_BYTES, 1);
  if (allocation.bytes == NULL) return NULL;
  allocation.length = QA_UI_SOURCE_TOKEN_BYTES;
  qa_ui_precomp_token *token = qa_ui_precomp_token_create(0, allocation);
  if (token == NULL) {
    free(allocation.bytes);
    return NULL;
  }
  token->owns_allocation_bytes = true;
  return token;
}

void qa_ui_precomp_token_destroy(qa_ui_precomp_token *token) {
  if (token == NULL) return;
  qa_ui_source_token_memory_destroy(token->token);
  qa_ui_source_token_context_destroy(&token->context);
  free(token->unsupported);
  if (token->owns_allocation_bytes) free(token->allocation.bytes);
  free(token);
}

int64_t qa_ui_precomp_token_id(const qa_ui_precomp_token *token) {
  return token->id;
}
qa_ui_source_token_memory *qa_ui_precomp_token_memory(
    qa_ui_precomp_token *token) {
  return token->token;
}
const qa_ui_script_allocation *qa_ui_precomp_token_allocation(
    const qa_ui_precomp_token *token) {
  return &token->allocation;
}
const qa_ui_source_token_context *qa_ui_precomp_token_context(
    const qa_ui_precomp_token *token) {
  return &token->context;
}
bool qa_ui_precomp_token_set_context(
    qa_ui_precomp_token *token, const qa_ui_source_token_context *context) {
  return qa_ui_source_token_context_set(
      &token->context, context->path, context->column,
      context->leading_whitespace, context->leading_whitespace_length);
}
const char *qa_ui_precomp_token_unsupported(
    const qa_ui_precomp_token *token) {
  return token->unsupported;
}
bool qa_ui_precomp_token_set_unsupported(qa_ui_precomp_token *token,
                                         const char *reason) {
  char *copy = NULL;
  if (reason != NULL) {
    copy = malloc(strlen(reason) + 1);
    if (copy == NULL) return false;
    strcpy(copy, reason);
  }
  free(token->unsupported);
  token->unsupported = copy;
  return true;
}

void qa_ui_precomp_token_copy_from(qa_ui_precomp_token *token,
                                   qa_ui_precomp_token *other) {
  qa_ui_source_token_copy_from(token->token, other->token);
  if (!qa_ui_precomp_token_set_context(token, &other->context)
      || !qa_ui_precomp_token_set_unsupported(token, other->unsupported))
    qa_range_error("precomp token allocation failed");
}

void qa_ui_precomp_token_clear_whitespace(qa_ui_precomp_token *token) {
  qa_ui_source_token_set_whitespace_start(token->token, 0);
  qa_ui_source_token_set_whitespace_end(token->token, 0);
  qa_ui_source_token_set_lines_crossed(token->token, 0);
  if (!qa_ui_source_token_context_set(&token->context, token->context.path,
                                      token->context.column, "", 0))
    qa_range_error("precomp token allocation failed");
}

qa_save_value *qa_ui_precomp_token_capture_save_state(
    qa_ui_precomp_token *token) {
  qa_save_value *object = qa_save_value_object();
  qa_save_value *context = qa_save_value_object();
  qa_save_value *state = qa_ui_source_token_capture_save_state(token->token);
  if (object == NULL || context == NULL || state == NULL) goto oom;
  if (!qa_save_value_set(object, "token", state)) goto oom;
  state = NULL;
  if (!qa_save_value_set(context, "path",
                         qa_save_value_string(token->context.path)))
    goto oom;
  if (!qa_save_value_set(context, "column",
                         qa_save_value_number((double)token->context.column)))
    goto oom;
  {
    char *ws = malloc(token->context.leading_whitespace_length + 1);
    if (ws == NULL) goto oom;
    memcpy(ws, token->context.leading_whitespace,
           token->context.leading_whitespace_length);
    ws[token->context.leading_whitespace_length] = '\0';
    bool ok = qa_save_value_set(context, "leadingWhitespace",
                                qa_save_value_string(ws));
    free(ws);
    if (!ok) goto oom;
  }
  if (!qa_save_value_set(object, "context", context)) goto oom;
  context = NULL;
  if (token->unsupported != NULL) {
    if (!qa_save_value_set(object, "unsupported",
                           qa_save_value_string(token->unsupported)))
      goto oom;
  } else if (!qa_save_value_set(object, "unsupported",
                                qa_save_value_null())) {
    goto oom;
  }
  return object;
oom:
  qa_save_value_free(object);
  qa_save_value_free(context);
  qa_save_value_free(state);
  return NULL;
}

void qa_ui_precomp_token_restore_save_state(qa_ui_precomp_token *token,
                                            const qa_save_value *value,
                                            bool verify_bytes) {
  qa_save_reader reader, field, context;
  if (!qa_save_reader_init(&reader, value, "script.precompToken"))
    qa_range_error("precomp token restore allocation failed");
  if (!qa_save_reader_field(&reader, "token", &field))
    qa_range_error("precomp token restore allocation failed");
  qa_ui_source_token_restore_save_state(token->token, field.value,
                                        verify_bytes);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(&reader, "context", &context))
    qa_range_error("precomp token restore allocation failed");
  char *path = NULL, *ws = NULL;
  int64_t column = 1;
  if (qa_save_reader_field(&context, "path", &field)) {
    const char *text = qa_save_reader_string(&field);
    path = malloc(strlen(text) + 1);
    if (path != NULL) strcpy(path, text);
    qa_save_reader_destroy(&field);
  }
  if (qa_save_reader_field(&context, "column", &field)) {
    column = (int64_t)qa_save_reader_integer_min(&field, 0);
    qa_save_reader_destroy(&field);
  }
  if (qa_save_reader_field(&context, "leadingWhitespace", &field)) {
    const char *text = qa_save_reader_string(&field);
    ws = malloc(strlen(text) + 1);
    if (ws != NULL) strcpy(ws, text);
    qa_save_reader_destroy(&field);
  }
  qa_save_reader_destroy(&context);
  if (path == NULL || ws == NULL
      || !qa_ui_source_token_context_set(&token->context, path, column, ws,
                                         strlen(ws))) {
    free(path);
    free(ws);
    qa_range_error("precomp token restore allocation failed");
  }
  free(path);
  free(ws);
  if (!qa_save_reader_field(&reader, "unsupported", &field))
    qa_range_error("precomp token restore allocation failed");
  if (qa_save_reader_is_null(&field)) {
    (void)qa_ui_precomp_token_set_unsupported(token, NULL);
  } else {
    if (!qa_ui_precomp_token_set_unsupported(
            token, qa_save_reader_string(&field)))
      qa_range_error("precomp token restore allocation failed");
  }
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&reader);
}

/* ---- define ---- */

static uint32_t def_rd_u32(qa_ui_precomp_define *define, size_t offset) {
  uint32_t value;
  memcpy(&value, define->allocation.bytes + offset, sizeof value);
  return value;
}
static void def_wr_u32(qa_ui_precomp_define *define, size_t offset,
                       uint32_t value) {
  memcpy(define->allocation.bytes + offset, &value, sizeof value);
}

int64_t qa_ui_precomp_define_id(const qa_ui_precomp_define *define) {
  return define->id;
}
qa_ui_precomp_memory *qa_ui_precomp_define_owner(
    qa_ui_precomp_define *define) {
  return define->owner;
}
const qa_ui_script_allocation *qa_ui_precomp_define_allocation(
    const qa_ui_precomp_define *define) {
  return &define->allocation;
}

size_t qa_ui_precomp_define_name_into(qa_ui_precomp_define *define,
                                      char *out, size_t capacity) {
  const uint8_t *bytes = define->allocation.bytes;
  uint32_t start = def_rd_u32(define, 0);
  if (start >= define->allocation.length)
    qa_range_error("define name pointer is outside its allocation");
  size_t length = 0;
  for (size_t index = start; index < define->allocation.length; index++) {
    if (bytes[index] == 0) {
      if (out != NULL && capacity > 0) {
        size_t copied = length < capacity - 1 ? length : capacity - 1;
        memcpy(out, bytes + start, copied);
        out[copied] = '\0';
      }
      return length;
    }
    length++;
  }
  qa_range_error("define name has no terminator in its allocation");
}

char *qa_ui_precomp_define_name(qa_ui_precomp_define *define) {
  size_t length = qa_ui_precomp_define_name_into(define, NULL, 0);
  char *result = malloc(length + 1);
  if (result == NULL) return NULL;
  qa_ui_precomp_define_name_into(define, result, length + 1);
  return result;
}

#define DEF_FIELD(name, offset)                                              \
  int32_t qa_ui_precomp_define_##name(qa_ui_precomp_define *define) {        \
    return (int32_t)def_rd_u32(define, offset);                              \
  }                                                                          \
  void qa_ui_precomp_define_set_##name(qa_ui_precomp_define *define,         \
                                       int32_t value) {                      \
    def_wr_u32(define, offset, (uint32_t)value);                             \
  }
DEF_FIELD(flags, 4)
DEF_FIELD(builtin, 8)
DEF_FIELD(numparms, 12)
#undef DEF_FIELD
#define DEF_FIELDU(name, offset)                                             \
  uint32_t qa_ui_precomp_define_##name(qa_ui_precomp_define *define) {       \
    return def_rd_u32(define, offset);                                       \
  }                                                                          \
  void qa_ui_precomp_define_set_##name(qa_ui_precomp_define *define,         \
                                       uint32_t value) {                     \
    def_wr_u32(define, offset, value);                                       \
  }
DEF_FIELDU(parms, 16)
DEF_FIELDU(tokens, 20)
DEF_FIELDU(next, 24)
DEF_FIELDU(hashnext, 28)
#undef DEF_FIELDU

bool qa_ui_precomp_define_fixed(qa_ui_precomp_define *define) {
  return (qa_ui_precomp_define_flags(define) & 1) != 0;
}

typedef struct parm_search {
  const char *name;
  int64_t index;
  int64_t found;
} parm_search;

static bool parm_visit(void *ctx, qa_ui_precomp_token *token) {
  parm_search *search = ctx;
  char text[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  qa_ui_source_token_string(token->token, text);
  if (strcmp(text, search->name) == 0) {
    search->found = search->index;
    return false;
  }
  search->index++;
  return true;
}

int64_t qa_ui_precomp_define_parameter_index(qa_ui_precomp_define *define,
                                             const char *name) {
  parm_search search = {name, 0, -1};
  qa_ui_precomp_memory_chain(define->owner,
                             qa_ui_precomp_define_parms(define), parm_visit,
                             &search);
  return search.found;
}

/* ---- memory owner ---- */

qa_ui_precomp_memory *qa_ui_precomp_memory_create(
    const qa_ui_script_memory *memory) {
  qa_ui_precomp_memory *owner = calloc(1, sizeof *owner);
  if (owner == NULL) return NULL;
  owner->memory = memory;
  owner->next_token = 1;
  owner->next_define = 1;
  return owner;
}

static bool grow_tokens(qa_ui_precomp_memory *owner, size_t need) {
  if (need <= owner->token_cap) return true;
  size_t cap = owner->token_cap == 0 ? 8 : owner->token_cap;
  while (cap < need) cap *= 2;
  qa_ui_precomp_token **grown = realloc(owner->tokens, cap * sizeof *grown);
  if (grown == NULL) return false;
  for (size_t index = owner->token_cap; index < cap; index++)
    grown[index] = NULL;
  owner->tokens = grown;
  owner->token_cap = cap;
  return true;
}
static bool grow_defines(qa_ui_precomp_memory *owner, size_t need) {
  if (need <= owner->define_cap) return true;
  size_t cap = owner->define_cap == 0 ? 8 : owner->define_cap;
  while (cap < need) cap *= 2;
  qa_ui_precomp_define **grown =
      realloc(owner->defines, cap * sizeof *grown);
  if (grown == NULL) return false;
  for (size_t index = owner->define_cap; index < cap; index++)
    grown[index] = NULL;
  owner->defines = grown;
  owner->define_cap = cap;
  return true;
}

void qa_ui_precomp_memory_destroy(qa_ui_precomp_memory *memory) {
  if (memory == NULL) return;
  for (size_t index = 0; index < memory->token_count; index++) {
    qa_ui_precomp_token *token = memory->tokens[index];
    if (token != NULL) {
      qa_ui_script_memory_free(memory->memory, &token->allocation);
      token->owns_allocation_bytes = false;
      qa_ui_precomp_token_destroy(token);
    }
  }
  for (size_t index = 0; index < memory->define_count; index++) {
    qa_ui_precomp_define *define = memory->defines[index];
    if (define != NULL) {
      qa_ui_script_memory_free(memory->memory, &define->allocation);
      free(define);
    }
  }
  free(memory->tokens);
  free(memory->defines);
  free(memory);
}

size_t qa_ui_precomp_memory_token_count(const qa_ui_precomp_memory *memory) {
  return memory->live_tokens;
}

static bool owner_allocate(qa_ui_precomp_memory *owner, size_t size,
                           qa_ui_script_allocation *out) {
  return qa_ui_script_memory_allocate(owner->memory, size, false, out);
}

qa_ui_precomp_token *qa_ui_precomp_memory_copy_token(
    qa_ui_precomp_memory *memory, qa_ui_precomp_token *token) {
  qa_ui_script_allocation allocation;
  if (!owner_allocate(memory, QA_UI_SOURCE_TOKEN_BYTES, &allocation))
    qa_range_error("precomp token allocation failed");
  qa_ui_precomp_token *copied =
      qa_ui_precomp_token_create(memory->next_token++, allocation);
  if (copied == NULL) qa_range_error("precomp token allocation failed");
  qa_ui_precomp_token_copy_from(copied, token);
  qa_ui_source_token_set_next(copied->token, 0);
  if (!grow_tokens(memory, (size_t)copied->id))
    qa_range_error("precomp token allocation failed");
  memory->tokens[copied->id - 1] = copied;
  if ((size_t)copied->id > memory->token_count)
    memory->token_count = (size_t)copied->id;
  memory->live_tokens++;
  return copied;
}

qa_ui_precomp_token *qa_ui_precomp_memory_token(qa_ui_precomp_memory *memory,
                                                int64_t id) {
  if (id < 1 || id > (int64_t)memory->token_count
      || memory->tokens[id - 1] == NULL)
    qa_range_error("source token pointer does not identify a live token");
  return memory->tokens[id - 1];
}

void qa_ui_precomp_memory_chain(qa_ui_precomp_memory *memory, int64_t head,
                                qa_ui_precomp_chain_fn visit, void *ctx) {
  int64_t remaining = (int64_t)memory->live_tokens + 1;
  for (int64_t id = head; id != 0;) {
    if (--remaining == 0)
      qa_range_error("source token chain contains a cycle");
    qa_ui_precomp_token *token = qa_ui_precomp_memory_token(memory, id);
    int64_t next = qa_ui_source_token_next(token->token);
    if (!visit(ctx, token)) return;
    id = next;
  }
}

void qa_ui_precomp_memory_free_token(qa_ui_precomp_memory *memory,
                                     qa_ui_precomp_token *token) {
  qa_ui_script_memory_free(memory->memory, &token->allocation);
  token->owns_allocation_bytes = false;
  if (token->id >= 1 && token->id <= (int64_t)memory->token_count
      && memory->tokens[token->id - 1] == token) {
    memory->tokens[token->id - 1] = NULL;
    memory->live_tokens--;
  }
  qa_ui_precomp_token_destroy(token);
}

void qa_ui_precomp_memory_free_tokens(qa_ui_precomp_memory *memory,
                                      int64_t head) {
  for (int64_t id = head; id != 0;) {
    qa_ui_precomp_token *token = qa_ui_precomp_memory_token(memory, id);
    id = qa_ui_source_token_next(token->token);
    qa_ui_precomp_memory_free_token(memory, token);
  }
}

static qa_ui_precomp_define *create_define(qa_ui_precomp_memory *owner,
                                           const char *name,
                                           size_t name_length, bool clear) {
  qa_ui_script_allocation allocation;
  if (!owner_allocate(owner, QA_UI_SOURCE_DEFINE_BYTES + name_length + 1,
                      &allocation))
    qa_range_error("precomp define allocation failed");
  uint8_t *bytes = allocation.bytes;
  if (clear) memset(bytes, 0, QA_UI_SOURCE_DEFINE_BYTES);
  uint32_t start = QA_UI_SOURCE_DEFINE_BYTES;
  memcpy(bytes, &start, sizeof start);
  if (QA_UI_SOURCE_DEFINE_BYTES + name_length >= allocation.length)
    qa_range_error("copied define name exceeds its reached allocation");
  memcpy(bytes + QA_UI_SOURCE_DEFINE_BYTES, name, name_length);
  bytes[QA_UI_SOURCE_DEFINE_BYTES + name_length] = 0;
  qa_ui_precomp_define *define = calloc(1, sizeof *define);
  if (define == NULL) qa_range_error("precomp define allocation failed");
  define->id = owner->next_define++;
  define->allocation = allocation;
  define->owner = owner;
  if (!grow_defines(owner, (size_t)define->id))
    qa_range_error("precomp define allocation failed");
  owner->defines[define->id - 1] = define;
  if ((size_t)define->id > owner->define_count)
    owner->define_count = (size_t)define->id;
  return define;
}

qa_ui_precomp_define *qa_ui_precomp_memory_allocate_define(
    qa_ui_precomp_memory *memory, const char *name, size_t name_length,
    bool clear) {
  return create_define(memory, name, name_length, clear);
}

qa_ui_precomp_define *qa_ui_precomp_memory_define(
    qa_ui_precomp_memory *memory, int64_t id) {
  if (id < 1 || id > (int64_t)memory->define_count
      || memory->defines[id - 1] == NULL)
    qa_range_error("source define pointer does not identify a live definition");
  return memory->defines[id - 1];
}

typedef struct copy_chain {
  qa_ui_precomp_memory *owner;
  qa_ui_precomp_token *last;
  uint32_t *head_field;
  qa_ui_precomp_define *copied;
  bool is_parms;
} copy_chain;

static bool copy_chain_visit(void *ctx, qa_ui_precomp_token *token) {
  copy_chain *chain = ctx;
  qa_ui_precomp_token *next =
      qa_ui_precomp_memory_copy_token(chain->owner, token);
  if (chain->last == NULL) {
    if (chain->is_parms)
      qa_ui_precomp_define_set_parms(chain->copied, (uint32_t)next->id);
    else qa_ui_precomp_define_set_tokens(chain->copied, (uint32_t)next->id);
  } else {
    qa_ui_source_token_set_next(chain->last->token, (uint32_t)next->id);
  }
  chain->last = next;
  return true;
}

qa_ui_precomp_define *qa_ui_precomp_memory_copy_define(
    qa_ui_precomp_memory *memory, qa_ui_precomp_define *define) {
  char *name = qa_ui_precomp_define_name(define);
  if (name == NULL) qa_range_error("precomp define allocation failed");
  qa_ui_precomp_define *copied =
      create_define(memory, name, strlen(name), false);
  free(name);
  qa_ui_precomp_define_set_flags(copied, qa_ui_precomp_define_flags(define));
  qa_ui_precomp_define_set_builtin(copied,
                                   qa_ui_precomp_define_builtin(define));
  qa_ui_precomp_define_set_numparms(copied,
                                    qa_ui_precomp_define_numparms(define));
  qa_ui_precomp_define_set_next(copied, 0);
  qa_ui_precomp_define_set_hashnext(copied, 0);
  qa_ui_precomp_define_set_tokens(copied, 0);
  copy_chain tokens = {memory, NULL, NULL, copied, false};
  qa_ui_precomp_memory_chain(define->owner,
                             qa_ui_precomp_define_tokens(define),
                             copy_chain_visit, &tokens);
  qa_ui_precomp_define_set_parms(copied, 0);
  copy_chain parms = {memory, NULL, NULL, copied, true};
  qa_ui_precomp_memory_chain(define->owner,
                             qa_ui_precomp_define_parms(define),
                             copy_chain_visit, &parms);
  return copied;
}

void qa_ui_precomp_memory_free_define(qa_ui_precomp_memory *memory,
                                      qa_ui_precomp_define *define) {
  qa_ui_precomp_memory_free_tokens(memory,
                                   qa_ui_precomp_define_parms(define));
  qa_ui_precomp_memory_free_tokens(memory,
                                   qa_ui_precomp_define_tokens(define));
  qa_ui_script_memory_free(memory->memory, &define->allocation);
  if (define->id >= 1 && define->id <= (int64_t)memory->define_count
      && memory->defines[define->id - 1] == define)
    memory->defines[define->id - 1] = NULL;
  free(define);
}

qa_save_value *qa_ui_precomp_memory_capture_save_state(
    qa_ui_precomp_memory *memory,
    const qa_ui_script_memory_capture *capture) {
  qa_save_value *object = qa_save_value_object();
  qa_save_value *tokens = qa_save_value_array();
  qa_save_value *defines = qa_save_value_array();
  if (object == NULL || tokens == NULL || defines == NULL) goto oom;
  if (!qa_save_value_set(object, "nextToken",
                         qa_save_value_number((double)memory->next_token)))
    goto oom;
  if (!qa_save_value_set(object, "nextDefine",
                         qa_save_value_number((double)memory->next_define)))
    goto oom;
  for (size_t index = 0; index < memory->token_count; index++) {
    qa_ui_precomp_token *token = memory->tokens[index];
    if (token == NULL) continue;
    qa_save_value *entry = qa_save_value_object();
    qa_save_value *state = qa_ui_precomp_token_capture_save_state(token);
    if (entry == NULL || state == NULL) {
      qa_save_value_free(entry);
      qa_save_value_free(state);
      goto oom;
    }
    bool ok = qa_save_value_set(entry, "id",
                                qa_save_value_number((double)token->id))
              && qa_save_value_set(
                     entry, "allocation",
                     qa_save_value_number((double)capture->reference(
                         capture->ctx, &token->allocation)))
              && qa_save_value_set(entry, "state", state);
    if (!ok) {
      qa_save_value_free(entry);
      goto oom;
    }
    if (!qa_save_value_push(tokens, entry)) {
      qa_save_value_free(entry);
      goto oom;
    }
  }
  if (!qa_save_value_set(object, "tokens", tokens)) goto oom;
  tokens = NULL;
  for (size_t index = 0; index < memory->define_count; index++) {
    qa_ui_precomp_define *define = memory->defines[index];
    if (define == NULL) continue;
    qa_save_value *entry = qa_save_value_object();
    if (entry == NULL) goto oom;
    bool ok = qa_save_value_set(entry, "id",
                                qa_save_value_number((double)define->id))
              && qa_save_value_set(
                     entry, "allocation",
                     qa_save_value_number((double)capture->reference(
                         capture->ctx, &define->allocation)));
    if (!ok || !qa_save_value_push(defines, entry)) {
      qa_save_value_free(entry);
      goto oom;
    }
  }
  if (!qa_save_value_set(object, "defines", defines)) goto oom;
  return object;
oom:
  qa_save_value_free(object);
  qa_save_value_free(tokens);
  qa_save_value_free(defines);
  return NULL;
}

void qa_ui_precomp_memory_restore_save_state(
    qa_ui_precomp_memory *memory, const qa_save_value *value,
    const qa_ui_script_memory_restore *restore) {
  qa_save_reader reader, field, entry;
  if (!qa_save_reader_init(&reader, value, "script.heap"))
    qa_range_error("precomp memory restore allocation failed");
  if (!qa_save_reader_field(&reader, "nextToken", &field))
    qa_range_error("precomp memory restore allocation failed");
  memory->next_token = (int64_t)qa_save_reader_integer_min(&field, 1);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(&reader, "nextDefine", &field))
    qa_range_error("precomp memory restore allocation failed");
  memory->next_define = (int64_t)qa_save_reader_integer_min(&field, 1);
  qa_save_reader_destroy(&field);
  /* TS clears both maps (identities only; allocations stay owned by
   * the checkpoint image). C frees the stale handles. */
  for (size_t index = 0; index < memory->token_count; index++) {
    if (memory->tokens[index] != NULL) {
      memory->tokens[index]->owns_allocation_bytes = false;
      qa_ui_precomp_token_destroy(memory->tokens[index]);
      memory->tokens[index] = NULL;
    }
  }
  for (size_t index = 0; index < memory->define_count; index++) {
    free(memory->defines[index]);
    memory->defines[index] = NULL;
  }
  memory->token_count = 0;
  memory->define_count = 0;
  memory->live_tokens = 0;
  if (!qa_save_reader_field(&reader, "tokens", &field))
    qa_range_error("precomp memory restore allocation failed");
  size_t count = qa_save_reader_list_count(&field);
  for (size_t index = 0; index < count; index++) {
    if (!qa_save_reader_item(&field, index, &entry))
      qa_range_error("precomp memory restore allocation failed");
    qa_save_reader cell;
    if (!qa_save_reader_field(&entry, "id", &cell))
      qa_range_error("precomp memory restore allocation failed");
    int64_t id = (int64_t)qa_save_reader_integer_min(&cell, 1);
    qa_save_reader_destroy(&cell);
    if (id >= memory->next_token
        || (id <= (int64_t)memory->token_count
            && memory->tokens[id - 1] != NULL))
      qa_save_reader_fail(&entry, "invalid token identity");
    if (!qa_save_reader_field(&entry, "allocation", &cell))
      qa_range_error("precomp memory restore allocation failed");
    qa_ui_script_allocation allocation = restore->allocation(
        restore->ctx, (int64_t)qa_save_reader_integer_min(&cell, 0));
    qa_save_reader_destroy(&cell);
    qa_ui_precomp_token *token = qa_ui_precomp_token_create(id, allocation);
    if (token == NULL)
      qa_range_error("precomp memory restore allocation failed");
    if (!qa_save_reader_field(&entry, "state", &cell))
      qa_range_error("precomp memory restore allocation failed");
    qa_ui_precomp_token_restore_save_state(token, cell.value, true);
    qa_save_reader_destroy(&cell);
    if (!grow_tokens(memory, (size_t)id))
      qa_range_error("precomp memory restore allocation failed");
    memory->tokens[id - 1] = token;
    if ((size_t)id > memory->token_count) memory->token_count = (size_t)id;
    memory->live_tokens++;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(&reader, "defines", &field))
    qa_range_error("precomp memory restore allocation failed");
  count = qa_save_reader_list_count(&field);
  for (size_t index = 0; index < count; index++) {
    if (!qa_save_reader_item(&field, index, &entry))
      qa_range_error("precomp memory restore allocation failed");
    qa_save_reader cell;
    if (!qa_save_reader_field(&entry, "id", &cell))
      qa_range_error("precomp memory restore allocation failed");
    int64_t id = (int64_t)qa_save_reader_integer_min(&cell, 1);
    qa_save_reader_destroy(&cell);
    if (id >= memory->next_define
        || (id <= (int64_t)memory->define_count
            && memory->defines[id - 1] != NULL))
      qa_save_reader_fail(&entry, "invalid define identity");
    if (!qa_save_reader_field(&entry, "allocation", &cell))
      qa_range_error("precomp memory restore allocation failed");
    qa_ui_script_allocation allocation = restore->allocation(
        restore->ctx, (int64_t)qa_save_reader_integer_min(&cell, 0));
    qa_save_reader_destroy(&cell);
    if (allocation.length < QA_UI_SOURCE_DEFINE_BYTES + 1)
      qa_save_reader_fail(&entry, "invalid define extent");
    qa_ui_precomp_define *define = calloc(1, sizeof *define);
    if (define == NULL)
      qa_range_error("precomp memory restore allocation failed");
    define->id = id;
    define->allocation = allocation;
    define->owner = memory;
    if (!grow_defines(memory, (size_t)id))
      qa_range_error("precomp memory restore allocation failed");
    memory->defines[id - 1] = define;
    if ((size_t)id > memory->define_count)
      memory->define_count = (size_t)id;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&reader);
}