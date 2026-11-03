/* Port of src/content/q1/foundation/text.ts. GPL-2.0-or-later. */
#include "qa/content/q1/foundation/text.h"
#include "qa/network/common/js_number.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/base/finales.h"
#include "qa/content/q1/base/messages.h"
#include "qa/content/q1/missionpacks/world/finale_text.h"

/* TS classicQ1Messages, in Map insertion order (generated from the TS map
 * under bun; the $qc_need_* and $qc_<color>_<kind> rows are its spreads). */
const qa_q1_message_entry QA_Q1_CLASSIC_MESSAGES[] = {
    {"$qc_found_secret", "You found a secret area!"},
    {"$qc_more_go", "There are more to go..."},
    {"$qc_three_more", "Only 3 more to go..."},
    {"$qc_two_more", "Only 2 more to go..."},
    {"$qc_one_more", "Only 1 more to go..."},
    {"$qc_sequence_completed", "Sequence completed!"},
    {"$qc_need_gold_key", "You need the gold key"},
    {"$qc_need_gold_runekey", "You need the gold runekey"},
    {"$qc_need_gold_keycard", "You need the gold keycard"},
    {"$qc_need_silver_key", "You need the silver key"},
    {"$qc_need_silver_runekey", "You need the silver runekey"},
    {"$qc_need_silver_keycard", "You need the silver keycard"},
    {"$qc_already_have_rune", "You already have a rune\n"},
    {"$qc_rune_resistance", "Earth Magic\n\nRESISTANCE"},
    {"$qc_rune_strength", "Black Magic\n\nSTRENGTH"},
    {"$qc_rune_haste", "Hell Magic\n\nHASTE"},
    {"$qc_rune_regeneration", "Edler Magic\n\nRegeneration"},
    {"$qc_color_games", "You were told you can't change teams.\nGo play color games somewhere else.\n"},
    {"$qc_cannot_change_teams", "You cannot change teams.\n"},
    {"$qc_suicided_too_much", "You have suicided too much already.\n"},
    {"$qc_out_lava_nails", "Out of Lava Nails\n"},
    {"$qc_ctf_disabled", "Capture the Flag is not enabled.\n"},
    {"$qc_flag_missing", "The flag is missing!\n"},
    {"$qc_flag_at_base", "The flag is at base!\n"},
    {"$qc_flag_lying_about", "The flag is lying about!\n"},
    {"$qc_you_have_flag", "You have the flag!\n"},
    {"$qc_flag_screwed_up", "The flag is screwed up!\n"},
    {"$qc_you_have_enemy_flag", "You have the enemy flag.\n"},
    {"$qc_flag_returned", "The flag has been returned!\n"},
    {"$qc_your_flag_returned_base", "Your flag has been returned to base!\n"},
    {"$qc_enemy_flag_returned_base", "Enemy flag has been returned to base!\n"},
    {"$qc_your_team_captured", "Your team captured the flag!\n"},
    {"$qc_your_flag_captured", "Your flag was captured!\n"},
    {"$qc_flag_taken", "The flag has been taken!\n"},
    {"$qc_your_flag_taken", "Your flag has been taken!\n"},
    {"$qc_enemy_killed_bonus", "Enemy flag carrier killed: {0} bonus frags\n"},
    {"$qc_enemy_killed_no_bonus", "Enemy flag carrier killed, no bonus\n"},
    {"$qc_has_token", "{0} has the tag token!\n"},
    {"$qc_lost_token", "{0} lost the tag token!\n"},
    {"$qc_got_token", "{0} got the tag token!\n"},
    {"$qc_got_quad", "You got the Quad Damage\n"},
    {"$qc_item_health", "You receive {0} health\n"},
    {"$qc_item_armor", "You got armor\n"},
    {"$qc_got_item", "You got {0}\n"},
    {"$qc_double_shotgun", "the Double-barrelled Shotgun"},
    {"$qc_nailgun", "the nailgun"},
    {"$qc_super_nailgun", "the Super Nailgun"},
    {"$qc_grenade_launcher", "the Grenade Launcher"},
    {"$qc_rocket_launcher", "the Rocket Launcher"},
    {"$qc_thunderbolt", "the Thunderbolt"},
    {"$qc_shells", "the shells"},
    {"$qc_nails", "the nails"},
    {"$qc_rockets", "the rockets"},
    {"$qc_cells", "the cells"},
    {"$qc_silver_key", "the silver key"},
    {"$qc_silver_runekey", "the silver runekey"},
    {"$qc_silver_keycard", "the silver keycard"},
    {"$qc_gold_key", "the gold key"},
    {"$qc_gold_runekey", "the gold runekey"},
    {"$qc_gold_keycard", "the gold keycard"},
    {"$qc_ring_fade", "Ring of Shadows magic is fading\n"},
    {"$qc_protection_fade", "Protection is almost burned out\n"},
    {"$qc_quad_fade", "Quad Damage is wearing off\n"},
    {"$qc_biosuit_fade", "Air supply in Biosuit expiring\n"},
    {"$qc_pentagram_of_protection", "the Pentagram of Protection"},
    {"$qc_ring_of_shadows", "the Ring of Shadows"},
    {"$qc_biosuit", "the Biosuit"},
    {"$qc_quad_damage", "the Quad Damage"},
};
const size_t QA_Q1_CLASSIC_MESSAGE_COUNT =
    sizeof QA_Q1_CLASSIC_MESSAGES / sizeof *QA_Q1_CLASSIC_MESSAGES;

const char *qa_q1_classic_message(const char *key) {
  if (key == NULL) return NULL;
  for (size_t i = 0; i < QA_Q1_CLASSIC_MESSAGE_COUNT; i++)
    if (strcmp(QA_Q1_CLASSIC_MESSAGES[i].key, key) == 0)
      return QA_Q1_CLASSIC_MESSAGES[i].value;
  return NULL;
}

/* JS String(number) for the values message args carry. */
static void js_string_number(double value, char *out, size_t out_size) {
  qa_net_js_number(value, out, out_size);
}

static void append(char *out, size_t out_size, size_t *length, const char *text, size_t count) {
  if (out == NULL || out_size == 0) return;
  for (size_t i = 0; i < count; i++)
    if (*length + 1 < out_size) out[(*length)++] = text[i];
  out[*length] = '\0';
}

char *qa_q1_classic_text(const char *text, const qa_q1_msg_arg *args, size_t arg_count,
                         char *out, size_t out_size) {
  if (out != NULL && out_size > 0) out[0] = '\0';
  const char *format = qa_q1_classic_message(text);
  char *scratch = NULL;
  if (format == NULL) {
    /* args.map(String) for classicObituaryText. */
    const size_t shown = arg_count;
    char (*numbers)[40] = malloc((shown ? shown : 1) * sizeof *numbers);
    const char **strings = malloc((shown ? shown : 1) * sizeof *strings);
    scratch = malloc(32768);
    if (numbers == NULL || strings == NULL || scratch == NULL) {
      free(numbers);
      free(strings);
      free(scratch);
      return out;
    }
    for (size_t i = 0; i < shown; i++) {
      if (args[i].is_number) {
        js_string_number(args[i].number, numbers[i], sizeof numbers[i]);
        strings[i] = numbers[i];
      } else {
        strings[i] = args[i].text ? args[i].text : "";
      }
    }
    qa_q1_classic_obituary_text(text, strings, shown, scratch, 32768);
    free(numbers);
    free(strings);
    format = qa_q1_finale_text("classic", qa_q1mp_mission_finale_text("classic", scratch));
  }
  size_t length = 0;
  for (size_t i = 0; format[i] != '\0';) {
    if (format[i] == '{') {
      size_t j = i + 1;
      unsigned long index = 0;
      bool digits = false;
      while (format[j] >= '0' && format[j] <= '9') {
        digits = true;
        /* Number(index) never wraps; saturate so a huge index stays out of range. */
        if (index <= (ULONG_MAX - 9) / 10) index = index * 10 + (unsigned long)(format[j] - '0');
        else index = ULONG_MAX;
        j++;
      }
      if (digits && format[j] == '}') {
        if (index < arg_count) {
          char number[40];
          const char *argument;
          if (args[index].is_number) {
            js_string_number(args[index].number, number, sizeof number);
            argument = number;
          } else {
            argument = args[index].text ? args[index].text : "";
          }
          const char *mapped = qa_q1_classic_message(argument);
          const char *shown = mapped ? mapped : argument;
          append(out, out_size, &length, shown, strlen(shown));
        } else {
          append(out, out_size, &length, format + i, j - i + 1);
        }
        i = j + 1;
        continue;
      }
    }
    append(out, out_size, &length, format + i, 1);
    i++;
  }
  free(scratch);
  return out;
}

char *qa_q1_entity_string(const char *value, char *out, size_t out_size) {
  if (out != NULL && out_size > 0) out[0] = '\0';
  size_t length = 0;
  for (size_t i = 0; value[i] != '\0';) {
    if (value[i] == '\\' && value[i + 1] != '\0') {
      /* TS /\\(.)/gs over UTF-16 units. Entity text reaches TS through
       * BinaryReader.fixedByteString (one char per byte) and C keeps those same
       * bytes, so one byte here is one TS unit: a backslash before a byte of
       * 0x80 or more consumes exactly that byte, as TS consumes its char. */
      const char escaped = value[i + 1] == 'n' ? '\n' : '\\';
      append(out, out_size, &length, &escaped, 1);
      i += 2;
    } else {
      append(out, out_size, &length, value + i, 1);
      i++;
    }
  }
  return out;
}
