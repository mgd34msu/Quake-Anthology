/* Port of src/bots/behavior/library/character.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_CHARACTER_H
#define QA_BOTS_BEHAVIOR_LIBRARY_CHARACTER_H
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
typedef struct qa_bot_character_record {
  bool used;
  double values[49];
  bool is_string[49];
  char *strings[49];
  double skill;
} qa_bot_character_record;
typedef struct qa_bot_character_library {
  qa_bot_character_record records[QA_MAX_BOT_CHARACTERS + 1];
  void (*report)(void *ctx, const char *code, const char *message);
  void *report_ctx;
} qa_bot_character_library;
void qa_bot_character_library_init(qa_bot_character_library *l);
void qa_bot_character_library_destroy(qa_bot_character_library *l);
int64_t qa_bot_character_allocate(qa_bot_character_library *l);
void qa_bot_character_free(qa_bot_character_library *l, int64_t handle);
void qa_bot_character_set_float(qa_bot_character_library *l, int64_t h, int64_t index, double v);
void qa_bot_character_set_string(qa_bot_character_library *l, int64_t h, int64_t index,
                                 const char *v);
double qa_bot_character_get_float(qa_bot_character_library *l, int64_t h, int64_t index);
const char *qa_bot_character_get_string(qa_bot_character_library *l, int64_t h, int64_t index);
double qa_bot_character_bounded_float(qa_bot_character_library *l, int64_t h, int64_t index,
                                      double minv, double maxv);
double qa_bot_character_bounded_integer(qa_bot_character_library *l, int64_t h, int64_t index,
                                        double minv, double maxv);
#endif
