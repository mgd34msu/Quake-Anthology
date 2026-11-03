/*
 * Port of src/bots/behavior/library/character.ts (botlib/be_ai_char.c).
 * C accommodations: init selects defaults (owned BotMemory); the TS constructor
 * arguments go through qa_bot_character_library_configure. A TS
 * `string | (() => string)` character file is qa_bot_character_file (the read
 * form returns text borrowed until its next call). TS CharacterError has no
 * catcher in the codebase: it records the diagnostic and aborts. destroy
 * releases C bookkeeping and the owned memory only. GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_CHARACTER_H
#define QA_BOTS_BEHAVIOR_LIBRARY_CHARACTER_H
#include "qa/bots/behavior/library/memory.h"
#include "qa/bots/behavior/library/script_sources.h"
#include "qa/persistence/value.h"
#include "qa/ui/common/legacy/script/lexer.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum qa_characteristic {
  QA_CHAR_NAME = 0,
  QA_CHAR_GENDER = 1,
  QA_CHAR_ATTACK_SKILL = 2,
  QA_CHAR_WEAPONWEIGHTS = 3,
  QA_CHAR_VIEW_FACTOR = 4,
  QA_CHAR_VIEW_MAXCHANGE = 5,
  QA_CHAR_REACTIONTIME = 6,
  QA_CHAR_AIM_ACCURACY = 7,
  QA_CHAR_AIM_ACCURACY_MACHINEGUN = 8,
  QA_CHAR_AIM_ACCURACY_SHOTGUN = 9,
  QA_CHAR_AIM_ACCURACY_ROCKETLAUNCHER = 10,
  QA_CHAR_AIM_ACCURACY_GRENADELAUNCHER = 11,
  QA_CHAR_AIM_ACCURACY_LIGHTNING = 12,
  QA_CHAR_AIM_ACCURACY_PLASMAGUN = 13,
  QA_CHAR_AIM_ACCURACY_RAILGUN = 14,
  QA_CHAR_AIM_ACCURACY_BFG10K = 15,
  QA_CHAR_AIM_SKILL = 16,
  QA_CHAR_AIM_SKILL_ROCKETLAUNCHER = 17,
  QA_CHAR_AIM_SKILL_GRENADELAUNCHER = 18,
  QA_CHAR_AIM_SKILL_PLASMAGUN = 19,
  QA_CHAR_AIM_SKILL_BFG10K = 20,
  QA_CHAR_CHAT_FILE = 21,
  QA_CHAR_CHAT_NAME = 22,
  QA_CHAR_CHAT_CPM = 23,
  QA_CHAR_CHAT_INSULT = 24,
  QA_CHAR_CHAT_MISC = 25,
  QA_CHAR_CHAT_STARTENDLEVEL = 26,
  QA_CHAR_CHAT_ENTEREXITGAME = 27,
  QA_CHAR_CHAT_KILL = 28,
  QA_CHAR_CHAT_DEATH = 29,
  QA_CHAR_CHAT_ENEMYSUICIDE = 30,
  QA_CHAR_CHAT_HITTALKING = 31,
  QA_CHAR_CHAT_HITNODEATH = 32,
  QA_CHAR_CHAT_HITNOKILL = 33,
  QA_CHAR_CHAT_RANDOM = 34,
  QA_CHAR_CHAT_REPLY = 35,
  QA_CHAR_CROUCHER = 36,
  QA_CHAR_JUMPER = 37,
  QA_CHAR_WEAPONJUMPING = 38,
  QA_CHAR_GRAPPLE_USER = 39,
  QA_CHAR_ITEMWEIGHTS = 40,
  QA_CHAR_AGGRESSION = 41,
  QA_CHAR_SELFPRESERVATION = 42,
  QA_CHAR_VENGEFULNESS = 43,
  QA_CHAR_CAMPER = 44,
  QA_CHAR_EASY_FRAGGER = 45,
  QA_CHAR_ALERTNESS = 46,
  QA_CHAR_FIRETHROTTLE = 47,
  QA_CHAR_WALKER = 48,
  QA_CHAR_COUNT = 49
} qa_characteristic;
#define QA_MAX_BOT_CHARACTERS 64
typedef enum qa_character_severity {
  QA_CHARACTER_INFO,
  QA_CHARACTER_WARNING,
  QA_CHARACTER_ERROR,
  QA_CHARACTER_FATAL
} qa_character_severity;
typedef enum qa_character_code {
  QA_CHARACTER_LOADED,
  QA_CHARACTER_MISSING_SOURCE,
  QA_CHARACTER_MISSING_SKILL,
  QA_CHARACTER_FALLBACK,
  QA_CHARACTER_PARSE_ERROR,
  QA_CHARACTER_INVALID_INPUT,
  QA_CHARACTER_INVALID_HANDLE,
  QA_CHARACTER_INVALID_INDEX,
  QA_CHARACTER_UNINITIALIZED,
  QA_CHARACTER_WRONG_TYPE,
  QA_CHARACTER_INVALID_BOUNDS,
  QA_CHARACTER_UNSUPPORTED_LOG_FORMAT
} qa_character_code;
/** TS CharacterDiagnostic (owned strings; the library keeps them). */
typedef struct qa_character_diagnostic {
  qa_character_severity severity;
  qa_character_code code;
  char *source;
  char *message;
  bool has_location;
  qa_ui_script_location location;
} qa_character_diagnostic;
const char *qa_character_severity_name(qa_character_severity severity);
const char *qa_character_code_name(qa_character_code code);
/** TS BotCharacterLibraryOptions; NULL is the TS undefined field. */
typedef struct qa_bot_character_options {
  double (*debug_milliseconds)(void *ctx);
  bool (*debug_developer)(void *ctx);
  void *debug_ctx;
  qa_bot_memory *memory;
  void (*log_write)(void *ctx, const char *text);
  bool (*log_has_file)(void *ctx); /* TS filePointer() !== null */
  void *log_ctx;
  bool (*reload_characters)(void *ctx);
  void *reload_ctx;
  void (*report)(void *ctx, const qa_character_diagnostic *diagnostic);
  void *report_ctx;
} qa_bot_character_options;
typedef struct qa_bot_character_file {
  const char *text;
  const char *(*read)(void *ctx);
  void *ctx;
} qa_bot_character_file;
typedef struct qa_character_string_cell {
  bool live;
  qa_bot_memory_allocation allocation;
} qa_character_string_cell;
typedef struct qa_bot_character_library {
  bool used[QA_MAX_BOT_CHARACTERS + 1];
  qa_bot_memory_allocation profiles[QA_MAX_BOT_CHARACTERS + 1];
  qa_character_diagnostic *reported;
  size_t reported_count, reported_capacity;
  qa_bot_script_reader reader;
  bool has_reader;
  qa_bot_character_options options;
  int64_t generation;
  qa_bot_memory owned_memory;
  qa_bot_memory *memory;
  qa_character_string_cell *strings; /* indexed by pointer ID */
  size_t string_capacity;
  int64_t next_pointer;
} qa_bot_character_library;
void qa_bot_character_library_init(qa_bot_character_library *l);
void qa_bot_character_library_configure(qa_bot_character_library *l,
                                        const qa_bot_script_reader *reader,
                                        const qa_bot_character_options *options);
void qa_bot_character_library_destroy(qa_bot_character_library *l);
const qa_character_diagnostic *qa_bot_character_diagnostics(const qa_bot_character_library *l,
                                                            size_t *out_count);
int64_t qa_bot_character_load(qa_bot_character_library *l, const char *file, double skill);
int64_t qa_bot_character_load_file(qa_bot_character_library *l, qa_bot_character_file file,
                                   double skill);
void qa_bot_character_dump(qa_bot_character_library *l, int64_t handle);
void qa_bot_character_free(qa_bot_character_library *l, int64_t handle);
void qa_bot_character_shutdown(qa_bot_character_library *l);
float qa_bot_character_float(qa_bot_character_library *l, int64_t handle, int64_t index);
float qa_bot_character_bounded_float(qa_bot_character_library *l, int64_t handle, int64_t index,
                                     double minimum, double maximum);
int32_t qa_bot_character_integer(qa_bot_character_library *l, int64_t handle, int64_t index);
int32_t qa_bot_character_bounded_integer(qa_bot_character_library *l, int64_t handle, int64_t index,
                                         double minimum, double maximum);
/* Borrowed; "" when the characteristic is absent or not a string. */
const char *qa_bot_character_string(qa_bot_character_library *l, int64_t handle, int64_t index);
void qa_bot_character_write_string(qa_bot_character_library *l, int64_t handle, int64_t index,
                                   void (*write)(void *ctx, const char *text), void *ctx);
qa_save_value *qa_bot_character_checkpoint(qa_bot_character_library *l,
                                           qa_bot_memory_capture *memory);
void qa_bot_character_restore(qa_bot_character_library *l, const qa_save_value *value,
                              qa_bot_memory_restored *memory);
#endif
