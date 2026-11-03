/*
 * Port of src/bots/behavior/library/chat.ts (botlib/be_ai_chat.c).
 *
 * Byte semantics are documented in chat_data.h: every length is a byte count
 * that equals the TS UTF-16 count for all text TS accepts. Latin-1 bytes are
 * the C form of TS byte-valued text; UTF-8 input counts extra bytes against
 * the 256/32/64 limits, and the TS >0xff RangeError has no char counterpart.
 *
 * qa_bot_chat_library_init is the constructor default state; bind installs the
 * TS constructor's reader, host, options and memory, and must precede use. The
 * library struct must not move while bound or after init. Option strings and
 * the reader's globals are borrowed. Strings returned as `char *` are owned by
 * the caller (free); `const char *` returns are borrowed.
 *
 * TS retirement exceptions: a source callback that retires the running setup or
 * load (ChatSetupRetired) unwinds with longjmp to that operation, which returns
 * false. A reader abandoned that way stays undisposed as in TS (see
 * qa_chat_data_parser_destroy).
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_CHAT_H
#define QA_BOTS_BEHAVIOR_LIBRARY_CHAT_H
#include "qa/bots/behavior/library/call_steps.h"
#include "qa/bots/behavior/library/chat_data.h"
#include "qa/bots/behavior/library/memory.h"
#include "qa/bots/behavior/library/script_sources.h"
#include "qa/bots/behavior/library/weights.h"
#include "qa/persistence/value.h"
#include "qa/ui/common/legacy/script/preprocessor.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum qa_chat_gender {
  QA_CHAT_GENDERLESS = 0,
  QA_CHAT_FEMALE = 1,
  QA_CHAT_MALE = 2
} qa_chat_gender;
typedef enum qa_chat_destination {
  QA_CHAT_ALL = 0,
  QA_CHAT_TEAM = 1,
  QA_CHAT_TELL = 2
} qa_chat_destination;
/* TS ChatCapture; offset is the signed-char source value when present. */
typedef struct qa_chat_capture {
  bool present;
  int64_t offset, length;
} qa_chat_capture;
/* TS ChatMatch. */
typedef struct qa_chat_match {
  char text[QA_CHAT_MESSAGE_SIZE];
  int32_t type, subtype;
  qa_chat_capture variables[8];
} qa_chat_match;
typedef struct qa_console_chat_message {
  int32_t handle;
  double time;
  int32_t type;
  char message[QA_CHAT_MESSAGE_SIZE + 1];
} qa_console_chat_message;
typedef enum qa_chat_severity {
  QA_CHAT_SEVERITY_INFO,
  QA_CHAT_SEVERITY_WARNING,
  QA_CHAT_SEVERITY_ERROR,
  QA_CHAT_SEVERITY_FATAL
} qa_chat_severity;
typedef enum qa_chat_diagnostic_code {
  QA_CHAT_DIAG_LOADED,
  QA_CHAT_DIAG_EMPTY_REPLIES,
  QA_CHAT_DIAG_LOAD_ERROR,
  QA_CHAT_DIAG_PARSE_WARNING,
  QA_CHAT_DIAG_INVALID_HANDLE,
  QA_CHAT_DIAG_INVALID_VARIABLE,
  QA_CHAT_DIAG_MESSAGE_HEAP_FULL,
  QA_CHAT_DIAG_CACHE_FULL,
  QA_CHAT_DIAG_EXPANSION_ERROR,
  QA_CHAT_DIAG_EXPANSION_LIMIT,
  QA_CHAT_DIAG_TEST_OUTPUT,
  QA_CHAT_DIAG_PRINT_FRAGMENT,
  QA_CHAT_DIAG_MISSING_RANDOM,
  QA_CHAT_DIAG_INTEGRITY_ERROR
} qa_chat_diagnostic_code;
/* TS ChatDiagnostic; has_location false is location null. Owned by the library. */
typedef struct qa_chat_diagnostic {
  qa_chat_severity severity;
  qa_chat_diagnostic_code code;
  char *source;
  char *message;
  bool has_location;
  qa_ui_script_location location;
} qa_chat_diagnostic;
const char *qa_chat_severity_name(qa_chat_severity severity);
const char *qa_chat_diagnostic_code_name(qa_chat_diagnostic_code code);

/* TS BotChatHost. client_command as in qa_bot_action_host; report may be NULL. */
typedef struct qa_bot_chat_host {
  qa_call_steps (*client_command)(void *ctx, int64_t client, const char *command);
  double (*time)(void *ctx);
  void (*report)(void *ctx, const qa_chat_diagnostic *diagnostic);
  void *ctx;
  qa_bot_random random;
} qa_bot_chat_host;

/* TS BotScriptReader. resolve_root false is undefined; on true, out->storage moves. */
typedef struct qa_bot_chat_script_reader {
  bool (*resolve_root)(void *ctx, const char *path, qa_ui_script_source *out);
  qa_ui_script_resolve_fn resolve;
  qa_ui_script_global_defines *globals;
  void *ctx;
  qa_ui_script_write_fn debug_eval; /* NULL: absent */
  void *debug_eval_ctx;
} qa_bot_chat_script_reader;
/* The BotScriptSources implementation of the reader interface. */
qa_bot_chat_script_reader qa_bot_chat_script_reader_from_sources(qa_bot_script_sources *sources);

/* TS BotChatOptions; a NULL member is the TS undefined. Every callback gets ctx. */
typedef struct qa_bot_chat_options {
  void *ctx;
  double (*debug_milliseconds)(void *ctx);
  void (*log_write)(void *ctx, const char *text);
  bool (*log_has_file)(void *ctx);              /* TS log.filePointer() !== null */
  void (*log_file_write)(void *ctx, const char *text); /* TS file.write */
  bool has_max_messages;
  double max_messages;
  double (*max_messages_fn)(void *ctx);
  const char *synonym_file, *random_file, *match_file, *reply_file;
  /* TS getter form of the four names (q3 library): NULL result is undefined. */
  const char *(*file_name_fn)(void *ctx, const char *option);
  bool (*no_chat)(void *ctx);
  bool (*reload_characters)(void *ctx);
  bool (*test_initial_chats)(void *ctx);
  bool (*test_reply_chats)(void *ctx);
  bool (*developer)(void *ctx);
} qa_bot_chat_options;

/* TS ChatConfigurationCounts. */
typedef struct qa_chat_configuration_counts {
  int64_t synonyms, random_lists, matches, replies;
} qa_chat_configuration_counts;

/* TS ChatBufferWrites; view(bytes).setUint8 becomes set_byte. */
typedef struct qa_chat_buffer_writes {
  void (*set_byte)(void *ctx, uint8_t *destination, size_t length, size_t index, uint8_t value);
  void (*copy)(void *ctx, uint8_t *destination, const uint8_t *source, size_t length);
  void (*clear)(void *ctx, uint8_t *destination, size_t length, size_t start);
  void *ctx;
} qa_chat_buffer_writes;
/* TS ChatMatchVariable. */
typedef struct qa_chat_match_variable {
  int64_t offset, length;
} qa_chat_match_variable;
/* TS ChatMatchBuffer; bytes NULL is the null string pointer, writes may be NULL. */
typedef struct qa_chat_match_buffer {
  uint8_t *bytes;
  size_t length;
  const qa_chat_buffer_writes *writes;
  int32_t type, subtype;
  qa_chat_match_variable variables[8];
} qa_chat_match_buffer;

/* TS stringContains; NULL or null sources return -1. */
int64_t qa_string_contains_bots_a(const char *text, const char *part, bool case_sensitive);
int64_t qa_chat_string_contains(qa_chat_text_source text, qa_chat_text_source part,
                                bool case_sensitive);
/* TS unifyWhiteSpaces: owned result. */
char *qa_unify_white_spaces_bots_a(const char *input);
/* TS unifyWhiteSpacesInPlace: length is the caller's live allocation span. */
void qa_unify_white_spaces_in_place_bots_a(uint8_t *bytes, size_t length,
                                           const qa_chat_buffer_writes *writes);

typedef struct qa_chat_state qa_chat_state;
typedef struct qa_chat_cached qa_chat_cached;
typedef struct qa_chat_console_cell {
  qa_bot_memory_allocation allocation;
  int64_t offset;
  uint32_t pointer;
} qa_chat_console_cell;

typedef struct qa_bot_chat_library {
  qa_bot_chat_script_reader reader;
  qa_bot_chat_host host;
  qa_bot_chat_options options;
  qa_bot_memory *memory;
  qa_bot_memory own_memory;
  /* TS generation: object identity is the id; cache is shared across shutdown. */
  uint64_t generation_id;
  int64_t setup_revision;
  qa_chat_cached **cache;
  size_t cache_length, cache_capacity;
  qa_chat_state *states[65];
  qa_chat_state **retired_states; /* kept so a captured state compares safely */
  size_t retired_state_count;
  qa_chat_diagnostic *issues;
  size_t issue_count;
  qa_stored_synonyms synonyms;
  qa_stored_randoms randoms;
  bool has_synonyms_allocation, has_randoms_allocation;
  qa_bot_memory_allocation synonyms_allocation, randoms_allocation;
  qa_chat_cell *matches, *replies;
  bool has_console_heap;
  qa_bot_memory_allocation console_heap;
  uint32_t free_console_messages;
  qa_chat_console_cell *console_cells;
  size_t console_cell_count;
  qa_chat_initial **initial_chats;
  size_t initial_chat_count;
} qa_bot_chat_library;

void qa_bot_chat_library_init(qa_bot_chat_library *l);
void qa_bot_chat_library_destroy(qa_bot_chat_library *l);
/* options and memory may be NULL (TS defaults). */
void qa_bot_chat_library_bind(qa_bot_chat_library *l, qa_bot_chat_script_reader reader,
                              qa_bot_chat_host host, const qa_bot_chat_options *options,
                              qa_bot_memory *memory);
qa_save_value *qa_bot_chat_library_checkpoint(qa_bot_chat_library *l,
                                              qa_bot_memory_capture *capture);
void qa_bot_chat_library_restore(qa_bot_chat_library *l, const qa_save_value *value,
                                 qa_bot_memory_restored *restored);
const qa_chat_diagnostic *qa_bot_chat_library_diagnostics(const qa_bot_chat_library *l,
                                                          size_t *out_count);
qa_chat_configuration_counts qa_bot_chat_library_configuration_counts(const qa_bot_chat_library *l);
/* False is TS null. */
bool qa_bot_chat_library_dump_initial_chat(qa_bot_chat_library *l, int64_t handle,
                                           qa_initial_chat *out);
void qa_bot_chat_library_dump_synonym_list(qa_bot_chat_library *l);
void qa_bot_chat_library_dump_random_string_list(qa_bot_chat_library *l);
void qa_bot_chat_library_dump_match_templates(qa_bot_chat_library *l);
void qa_bot_chat_library_dump_reply_chat(qa_bot_chat_library *l);
void qa_bot_chat_library_log_initial_chat(qa_bot_chat_library *l, int64_t handle);
void qa_bot_chat_library_setup(qa_bot_chat_library *l);
void qa_bot_chat_library_shutdown(qa_bot_chat_library *l);
void qa_bot_chat_library_dispose_resources(qa_bot_chat_library *l);
void qa_bot_chat_library_reset(qa_bot_chat_library *l);
int64_t qa_bot_chat_library_allocate(qa_bot_chat_library *l);
void qa_bot_chat_library_free(qa_bot_chat_library *l, int64_t handle);
void qa_bot_chat_library_free_chat_file(qa_bot_chat_library *l, int64_t handle);
bool qa_bot_chat_library_load_chat_file(qa_bot_chat_library *l, int64_t handle,
                                        qa_chat_text_source path, qa_chat_text_source name);
void qa_bot_chat_library_queue_console_message(qa_bot_chat_library *l, int64_t handle, int64_t type,
                                               qa_chat_text_source input);
void qa_bot_chat_library_remove_console_message(qa_bot_chat_library *l, int64_t handle,
                                                int64_t message_handle);
/* False is TS null. */
bool qa_bot_chat_library_next_console_message(qa_bot_chat_library *l, int64_t handle,
                                              qa_console_chat_message *out);
int64_t qa_bot_chat_library_num_console_messages(qa_bot_chat_library *l, int64_t handle);
/* has_client false is the TS omitted client. */
void qa_bot_chat_library_set_name(qa_bot_chat_library *l, int64_t handle, qa_chat_text_source name,
                                  bool has_client, int64_t client);
void qa_bot_chat_library_set_gender(qa_bot_chat_library *l, int64_t handle, int64_t gender);
int64_t qa_bot_chat_library_chat_length(qa_bot_chat_library *l, int64_t handle);
/* size is TS's optional argument; pass QA_CHAT_MESSAGE_SIZE for the default. */
char *qa_bot_chat_library_get_chat_message(qa_bot_chat_library *l, int64_t handle, int64_t size);
typedef void (*qa_chat_write_fn)(void *ctx, const char *text);
void qa_bot_chat_library_write_chat_message(qa_bot_chat_library *l, int64_t handle,
                                            qa_chat_write_fn write, void *ctx);
/* has_source_client false is the TS omitted sourceClient. */
void qa_bot_chat_library_enter_chat(qa_bot_chat_library *l, int64_t handle, int64_t client_to,
                                    int64_t send_to, bool has_source_client, int64_t source_client);
qa_call_steps qa_bot_chat_library_enter_chat_calls(qa_bot_chat_library *l, int64_t handle,
                                                   int64_t client_to, int64_t send_to,
                                                   bool has_source_client, int64_t source_client);
/* False is TS null. */
bool qa_bot_chat_library_find_match(qa_bot_chat_library *l, const char *input, int64_t context,
                                    qa_chat_match *out);
bool qa_bot_chat_library_find_match_into(qa_bot_chat_library *l, qa_chat_text_source input,
                                         int64_t context, qa_chat_match_buffer *buffer);
char *qa_bot_chat_library_match_variable(qa_bot_chat_library *l, const qa_chat_match *match,
                                         int64_t index, int64_t size);
typedef qa_chat_match_variable (*qa_chat_variable_at_fn)(void *ctx, int64_t index);
void qa_bot_chat_library_write_match_variable(qa_bot_chat_library *l, qa_chat_variable_at_fn at,
                                              int64_t index, int64_t size,
                                              void (*write)(void *ctx, int64_t offset, int64_t capacity),
                                              void (*clear)(void *ctx), void *ctx);
char *qa_bot_chat_library_replace_synonyms(qa_bot_chat_library *l, const char *input,
                                           int64_t context);
/* input re-resolves the live string for each synonym; *out_length receives its span. */
typedef uint8_t *(*qa_chat_bytes_fn)(void *ctx, size_t *out_length);
void qa_bot_chat_library_replace_synonyms_in_place(qa_bot_chat_library *l, qa_chat_bytes_fn input,
                                                   void *input_ctx, int64_t context,
                                                   const qa_chat_buffer_writes *writes);
char *qa_bot_chat_library_replace_weighted_synonyms(qa_bot_chat_library *l, const char *input,
                                                    int64_t context);
char *qa_bot_chat_library_replace_reply_synonyms(qa_bot_chat_library *l, const char *input,
                                                 int64_t context);
/* NULL is TS null; the text is borrowed from the stored random list. */
const char *qa_bot_chat_library_random_string(qa_bot_chat_library *l, const char *name);
int64_t qa_bot_chat_library_num_initial_chats(qa_bot_chat_library *l, int64_t handle,
                                              qa_chat_text_source name);
/* variables NULL is NO_VARIABLES; otherwise exactly eight sources, null allowed. */
void qa_bot_chat_library_initial_chat(qa_bot_chat_library *l, int64_t handle,
                                      qa_chat_text_source name, int64_t context,
                                      const qa_chat_text_source *variables);
bool qa_bot_chat_library_reply_chat(qa_bot_chat_library *l, int64_t handle,
                                    qa_chat_text_source input, int64_t message_context,
                                    int64_t variable_context,
                                    const qa_chat_text_source *variables);
#endif
