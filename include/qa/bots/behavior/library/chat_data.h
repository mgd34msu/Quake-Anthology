/*
 * Port of src/bots/behavior/library/chat-data.ts (botlib/be_ai_chat.c).
 *
 * Byte semantics: TS chat strings hold one byte per UTF-16 unit and reject
 * units above 0xff with a RangeError. C strings are those bytes, so every
 * length here equals the TS `.length` for all text TS accepts. Script files
 * decode one unit per byte, so file text always agrees. A host string holding
 * non-ASCII text must be passed as the bytes TS would hold (Latin-1, not UTF-8):
 * UTF-8 input counts more bytes than TS has units, and the >0xff rejection has
 * no C counterpart because a char cannot express it.
 *
 * Exceptions: RangeError and uncaught Error abort through qa_range_error. A
 * failure TS marks with parser.failedWith (language error, empty pieces,
 * initial chat not found) makes the parser operation return false, leaving
 * qa_chat_data_parser_failure set, for the TS try/catch sites.
 *
 * Pointers: TS typed views over Release32 records become accessors over the
 * same bytes. A graph cell (qa_chat_cell) is the TS ChatGraphBlock subclass
 * family; initial chat records are addressed by their uint32 pointer words.
 * Cells stay valid until their graph's last reference is released.
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_CHAT_DATA_H
#define QA_BOTS_BEHAVIOR_LIBRARY_CHAT_DATA_H
#include "qa/bots/behavior/library/memory.h"
#include "qa/persistence/value.h"
#include "qa/ui/common/legacy/script/preprocessor.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QA_CHAT_ESCAPE "\x01"
#define QA_CHAT_MESSAGE_SIZE 256

/* TS ChatTextSource | null: all fields NULL is null, text is a plain string,
 * fn is the live reader. maximum_bytes -1 is the TS undefined argument. The
 * returned text is borrowed until the next call. */
typedef const char *(*qa_chat_text_fn)(void *ctx, int64_t maximum_bytes);
typedef struct qa_chat_text_source {
  const char *text;
  qa_chat_text_fn fn;
  void *ctx;
} qa_chat_text_source;
qa_chat_text_source qa_chat_text(const char *text);
qa_chat_text_source qa_chat_text_live(qa_chat_text_fn fn, void *ctx);
bool qa_chat_text_is_null(qa_chat_text_source source);
const char *qa_chat_text_read(qa_chat_text_source source, int64_t maximum_bytes);

/* TS chatTextEquals; a NULL input is TS null. */
bool qa_chat_text_equals(const char *expected, const char *input, bool case_sensitive);
bool qa_chat_text_equals_source(const char *expected, qa_chat_text_source input,
                                bool case_sensitive);

/* TS plain snapshot shapes. All strings and arrays are owned. */
typedef struct qa_chat_message {
  char *text;
  double time;
} qa_chat_message;
typedef struct qa_chat_type {
  char *name;
  qa_chat_message *messages;
  size_t message_count;
} qa_chat_type;
typedef struct qa_initial_chat {
  qa_chat_type *types;
  size_t type_count;
} qa_initial_chat;
void qa_initial_chat_destroy(qa_initial_chat *chat);
typedef struct qa_random_chat_list {
  char *name;
  char **messages;
  size_t message_count;
} qa_random_chat_list;
typedef struct qa_synonym {
  char *text;
  double weight;
} qa_synonym;
typedef struct qa_synonym_group {
  uint32_t context;
  qa_synonym *entries;
  size_t entry_count;
  double total_weight;
} qa_synonym_group;
typedef enum qa_match_piece_kind { QA_MATCH_PIECE_VARIABLE, QA_MATCH_PIECE_STRING } qa_match_piece_kind;
typedef struct qa_match_piece {
  qa_match_piece_kind kind;
  int32_t index;
  char **alternatives;
  size_t alternative_count;
} qa_match_piece;
typedef struct qa_match_template {
  uint32_t context;
  int32_t type, subtype;
  qa_match_piece *pieces;
  size_t piece_count;
} qa_match_template;
void qa_match_template_destroy(qa_match_template *match);
typedef enum qa_reply_test_kind {
  QA_REPLY_TEST_NAME,
  QA_REPLY_TEST_GENDER,
  QA_REPLY_TEST_BOTNAMES,
  QA_REPLY_TEST_STRING,
  QA_REPLY_TEST_MATCH
} qa_reply_test_kind;
typedef enum qa_reply_key_mode { QA_REPLY_KEY_ANY, QA_REPLY_KEY_AND, QA_REPLY_KEY_NOT } qa_reply_key_mode;
typedef struct qa_reply_key {
  qa_reply_key_mode mode;
  qa_reply_test_kind kind;
  int32_t gender;
  char *text; /* botnames names / string text */
  qa_match_piece *pieces;
  size_t piece_count;
} qa_reply_key;
typedef struct qa_reply_chat {
  qa_reply_key *keys;
  size_t key_count;
  double priority;
  qa_chat_message *messages;
  size_t message_count;
} qa_reply_chat;
void qa_reply_chat_destroy(qa_reply_chat *reply);

/* TS ChatDataMemory. A NULL allocation is the sizing pass: nothing is written
 * and stored values read back from the fixed copies. */
typedef struct qa_chat_data_memory {
  qa_bot_memory_allocation allocation;
  bool has_allocation;
  int64_t cursor;
} qa_chat_data_memory;
void qa_chat_data_memory_init(qa_chat_data_memory *m, const qa_bot_memory_allocation *allocation);
int64_t qa_chat_data_memory_byte_length(const qa_chat_data_memory *m);
int64_t qa_chat_data_memory_reserve(qa_chat_data_memory *m, int64_t size);

/* TS SynonymGroup[] with its live getters: values read from the allocation
 * when has_allocation, otherwise from the fixed copies. length >= 0 marks a
 * restored string whose terminator position was saved. */
typedef struct qa_stored_text {
  char *fixed;
  int64_t offset, length;
} qa_stored_text;
typedef struct qa_stored_synonym {
  qa_stored_text text;
  float fixed_weight;
  int64_t record;
} qa_stored_synonym;
typedef struct qa_stored_synonym_group {
  uint32_t fixed_context;
  float fixed_total_weight;
  int64_t record;
  qa_stored_synonym *entries;
  size_t entry_count;
} qa_stored_synonym_group;
typedef struct qa_stored_synonyms {
  qa_bot_memory_allocation allocation;
  bool has_allocation;
  qa_stored_synonym_group *groups;
  size_t count;
} qa_stored_synonyms;
void qa_stored_synonyms_destroy(qa_stored_synonyms *list);
uint32_t qa_stored_synonym_group_context(const qa_stored_synonyms *list, size_t group);
double qa_stored_synonym_group_total_weight(const qa_stored_synonyms *list, size_t group);
const char *qa_stored_synonym_text(const qa_stored_synonyms *list, size_t group, size_t entry);
double qa_stored_synonym_weight(const qa_stored_synonyms *list, size_t group, size_t entry);

/* TS StoredRandomChatList[]; messages keep the head-insertion order. */
typedef struct qa_stored_random_list {
  qa_stored_text name;
  int64_t record;
  uint32_t fixed_count;
  qa_stored_text *messages;
  size_t message_count;
} qa_stored_random_list;
typedef struct qa_stored_randoms {
  qa_bot_memory_allocation allocation;
  bool has_allocation;
  qa_stored_random_list *lists;
  size_t count;
} qa_stored_randoms;
void qa_stored_randoms_destroy(qa_stored_randoms *list);
const char *qa_stored_random_name(const qa_stored_randoms *list, size_t index);
uint32_t qa_stored_random_num_messages(const qa_stored_randoms *list, size_t index);
/* NULL is the TS undefined for an index outside the list. */
const char *qa_stored_random_message(const qa_stored_randoms *list, size_t index, int64_t message);

/* TS restoreSynonyms/restoreRandoms sequential layouts. */
typedef struct qa_saved_random_layout {
  int64_t name_bytes;
  int64_t *message_bytes;
  size_t message_count;
} qa_saved_random_layout;
void qa_restore_synonyms(qa_stored_synonyms *out, const qa_bot_memory_allocation *allocation,
                         const int64_t *const *layout, const size_t *layout_counts, size_t count);
void qa_restore_randoms(qa_stored_randoms *out, const qa_bot_memory_allocation *allocation,
                        const qa_saved_random_layout *layout, size_t count);

/* TS ChatInitial: Release32 records packed without padding. */
typedef struct qa_chat_initial qa_chat_initial;
qa_chat_initial *qa_chat_initial_create(const qa_bot_memory_allocation *allocation,
                                        uint32_t pointer);
void qa_chat_initial_destroy(qa_chat_initial *chat);
uint32_t qa_chat_initial_pointer(const qa_chat_initial *chat);
qa_bot_memory_allocation *qa_chat_initial_allocation(qa_chat_initial *chat);
qa_save_value *qa_chat_initial_checkpoint(qa_chat_initial *chat, qa_bot_memory_capture *capture);
qa_chat_initial *qa_chat_initial_restore(const qa_save_value *value,
                                         qa_bot_memory_restored *restored);
void qa_chat_initial_snapshot(qa_chat_initial *chat, qa_initial_chat *out);
/* Records are uint32 pointer words; 0 is null. *_at validation is applied by
 * every link read, with the TS RangeError on a bad word. */
uint32_t qa_chat_initial_first_type(qa_chat_initial *chat);
uint32_t qa_chat_initial_type_next(qa_chat_initial *chat, uint32_t type);
const char *qa_chat_initial_type_name(qa_chat_initial *chat, uint32_t type);
int32_t qa_chat_initial_type_num_messages(qa_chat_initial *chat, uint32_t type);
uint32_t qa_chat_initial_type_first_message(qa_chat_initial *chat, uint32_t type);
uint32_t qa_chat_initial_message_next(qa_chat_initial *chat, uint32_t message);
const char *qa_chat_initial_message_text(qa_chat_initial *chat, uint32_t message);
float qa_chat_initial_message_time(qa_chat_initial *chat, uint32_t message);
void qa_chat_initial_message_set_time(qa_chat_initial *chat, uint32_t message, double time);

/* TS ChatGraphMemory and its cell classes. */
typedef struct qa_chat_graph qa_chat_graph;
typedef enum qa_chat_cell_kind {
  QA_CHAT_CELL_STRING,
  QA_CHAT_CELL_MATCH_STRING,
  QA_CHAT_CELL_PIECE,
  QA_CHAT_CELL_TEMPLATE,
  QA_CHAT_CELL_KEY,
  QA_CHAT_CELL_MESSAGE,
  QA_CHAT_CELL_REPLY
} qa_chat_cell_kind;
typedef struct qa_chat_cell {
  qa_chat_graph *owner;
  qa_chat_cell_kind kind;
  qa_bot_memory_allocation allocation;
  uint32_t pointer;
  int64_t offset;
} qa_chat_cell;
qa_chat_graph *qa_chat_graph_retain(qa_chat_graph *graph);
void qa_chat_graph_release(qa_chat_graph *graph);

const char *qa_chat_match_string_text(const qa_chat_cell *value);
qa_chat_cell *qa_chat_match_string_next(const qa_chat_cell *value);

int32_t qa_chat_piece_type(const qa_chat_cell *piece);
qa_chat_cell *qa_chat_piece_first_string(const qa_chat_cell *piece);
int32_t qa_chat_piece_variable(const qa_chat_cell *piece);
qa_chat_cell *qa_chat_piece_next(const qa_chat_cell *piece);
void qa_chat_piece_snapshot(const qa_chat_cell *piece, qa_match_piece *out);
void qa_chat_piece_free(qa_chat_cell *piece);

uint32_t qa_chat_template_context(const qa_chat_cell *match);
int32_t qa_chat_template_type(const qa_chat_cell *match);
int32_t qa_chat_template_subtype(const qa_chat_cell *match);
qa_chat_cell *qa_chat_template_first(const qa_chat_cell *match);
qa_chat_cell *qa_chat_template_next(const qa_chat_cell *match);
void qa_chat_template_snapshot(const qa_chat_cell *match, qa_match_template *out);
void qa_chat_template_free(qa_chat_cell *match);
/* TS checkpointGraph/restoreGraph; the restored cell holds the graph reference
 * the caller releases through its owner field. */
qa_save_value *qa_chat_cell_checkpoint_graph(const qa_chat_cell *root,
                                             qa_bot_memory_capture *capture);
qa_chat_cell *qa_chat_template_restore_graph(const qa_save_value *value, qa_bot_memory *memory,
                                             qa_bot_memory_restored *restored);
qa_chat_cell *qa_chat_reply_restore_graph(const qa_save_value *value, qa_bot_memory *memory,
                                          qa_bot_memory_restored *restored);

int32_t qa_chat_key_flags(const qa_chat_cell *key);
const char *qa_chat_key_text(const qa_chat_cell *key);
qa_chat_cell *qa_chat_key_match(const qa_chat_cell *key);
qa_chat_cell *qa_chat_key_next(const qa_chat_cell *key);
void qa_chat_key_snapshot(const qa_chat_cell *key, qa_reply_key *out);

const char *qa_chat_reply_message_text(const qa_chat_cell *message);
float qa_chat_reply_message_time(const qa_chat_cell *message);
void qa_chat_reply_message_set_time(const qa_chat_cell *message, double time);
qa_chat_cell *qa_chat_reply_message_next(const qa_chat_cell *message);

qa_chat_cell *qa_chat_reply_keys(const qa_chat_cell *reply);
float qa_chat_reply_priority(const qa_chat_cell *reply);
int32_t qa_chat_reply_num_messages(const qa_chat_cell *reply);
qa_chat_cell *qa_chat_reply_first_message(const qa_chat_cell *reply);
qa_chat_cell *qa_chat_reply_next(const qa_chat_cell *reply);
void qa_chat_reply_snapshot(const qa_chat_cell *reply, qa_reply_chat *out);
void qa_chat_reply_free(qa_chat_cell *reply);

/* TS ChatDataParser. Takes ownership of the source reader. memory NULL is the
 * default sizing `new ChatDataMemory()`; bot_memory NULL is a private
 * `new BotMemory()` that lives as long as the parser's graph. */
typedef struct qa_chat_data_parser qa_chat_data_parser;
typedef enum qa_chat_parser_failure {
  QA_CHAT_PARSER_OK,
  QA_CHAT_PARSER_LANGUAGE_ERROR,
  QA_CHAT_PARSER_EMPTY_PIECES,
  QA_CHAT_PARSER_INITIAL_NOT_FOUND
} qa_chat_parser_failure;
qa_chat_data_parser *qa_chat_data_parser_create(qa_ui_script_source_reader *source,
                                                const char *path, qa_chat_data_memory *memory,
                                                qa_bot_memory *bot_memory,
                                                qa_ui_script_report_fn report, void *report_ctx);
/* Frees the C objects. A source still undisposed stays that way, as TS leaves
 * it after a retired or empty-pieces exit: its script allocations remain live
 * in bot memory, and its reader record is abandoned (it cannot be destroyed
 * without freeing them). Dispose first to reclaim everything. */
void qa_chat_data_parser_destroy(qa_chat_data_parser *parser);
void qa_chat_data_parser_dispose(qa_chat_data_parser *parser);
void qa_chat_data_parser_free_graphs(qa_chat_data_parser *parser);
void qa_chat_data_parser_free_after_error(qa_chat_data_parser *parser);
qa_chat_parser_failure qa_chat_data_parser_failure(const qa_chat_data_parser *parser);
/* TS failure.message, and InitialChatNotFoundError.chatName when not found. */
const char *qa_chat_data_parser_failure_message(const qa_chat_data_parser *parser);
const char *qa_chat_data_parser_failure_chat_name(const qa_chat_data_parser *parser);
/* TS diagnostics getter: source diagnostics followed by the parser's own. */
size_t qa_chat_data_parser_diagnostic_count(const qa_chat_data_parser *parser);
const qa_ui_script_diagnostic *qa_chat_data_parser_diagnostic(const qa_chat_data_parser *parser,
                                                              size_t index);
/* Each returns false on a parser failure (TS throw, see the header comment). */
bool qa_chat_data_parser_synonyms(qa_chat_data_parser *parser, qa_stored_synonyms *out);
bool qa_chat_data_parser_randoms(qa_chat_data_parser *parser, qa_stored_randoms *out);
/* TS matches()/replies(): snapshots; a failure also frees after error. */
bool qa_chat_data_parser_matches(qa_chat_data_parser *parser, qa_match_template **out,
                                 size_t *out_count);
bool qa_chat_data_parser_replies(qa_chat_data_parser *parser, qa_reply_chat **out,
                                 size_t *out_count);
/* *out receives the head cell or NULL; its graph is shared with the parser, so
 * retain out->owner to keep it past the parser. */
bool qa_chat_data_parser_match_templates(qa_chat_data_parser *parser, qa_chat_cell **out);
bool qa_chat_data_parser_reply_chats(qa_chat_data_parser *parser, qa_chat_cell **out);
/* TS initial(name, previouslyFound): snapshot overload. */
bool qa_chat_data_parser_initial(qa_chat_data_parser *parser, qa_chat_text_source name,
                                 bool previously_found, qa_initial_chat *out);
/* TS initial(name, previouslyFound, stored). */
bool qa_chat_data_parser_initial_into(qa_chat_data_parser *parser, qa_chat_text_source name,
                                      bool previously_found, qa_chat_initial *stored);
#endif
