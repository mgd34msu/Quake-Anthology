/* Port of src/contracts/source-match.ts (runtime helpers). GPL-2.0-or-later. */
#include "qa/contracts/source_match.h"
#include "qa/network/common/js_number.h"

#include "qa/foundation/support.h"
#include <math.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TS throws plain Error in this module (not RangeError); the C port
 * aborts printing the identical message under the Error class name. */
static noreturn void qa_error(const char *message) {
  qa_raise("Error", 0, 0, "%s", message);
}

static bool string_equal(const char *left, const char *right) {
  if (left == NULL || right == NULL) return left == right;
  return strcmp(left, right) == 0;
}

/* JS String(number) for the interpolated team value. */
static void format_number(double value, char *buffer, size_t size) {
  qa_net_js_number(value, buffer, size);
}

const char *qa_source_team(const qa_source_team_value *values, size_t count, double value) {
  for (size_t i = 0; i < count; i++) {
    if (values[i].value == value) return values[i].team;
  }
  char number[40];
  format_number(value, number, sizeof number);
  char message[256];
  (void)snprintf(message, sizeof message,
                 "Original team value %s has no declared shared identity", number);
  qa_error(message);
}

double qa_original_team(const qa_source_team_value *values, size_t count, const char *team) {
  for (size_t i = 0; i < count; i++) {
    if (string_equal(values[i].team, team)) return values[i].value;
  }
  char message[1024];
  (void)snprintf(message, sizeof message,
                 "Shared team %s has no declared original value",
                 team != NULL ? team : "unassigned");
  qa_error(message);
}

void qa_validate_source_match_field(qa_source_match_field field) {
  if (field.is_score) return;
  bool invalid = field.value_count == 0;
  for (size_t i = 0; i < field.value_count && !invalid; i++) {
    const qa_source_team_value *entry = &field.values[i];
    if (!isfinite(entry->value) || (entry->team != NULL && entry->team[0] == '\0')) {
      invalid = true;
      break;
    }
    for (size_t j = i + 1; j < field.value_count; j++) {
      /* TS Set sizes: duplicate numbers or duplicate teams
       * (including two null teams) shrink the set. */
      if (field.values[j].value == entry->value ||
          string_equal(field.values[j].team, entry->team)) {
        invalid = true;
        break;
      }
    }
  }
  if (invalid) {
    qa_error("Team projection requires distinct original values and shared identities");
  }
}

double qa_read_source_match_field(const qa_source_match_services *match,
                                  qa_actor_id actor, qa_source_match_field field) {
  if (match == NULL) {
    qa_error("Component match fields require destination match services");
  }
  qa_source_match_player_result player =
      match->player((void *)match->ctx, actor);
  if (field.is_score) {
    return player.has_value ? player.value.score(player.value.ctx) : 0;
  }
  const char *team = player.has_value ? player.value.team(player.value.ctx) : NULL;
  return qa_original_team(field.values, field.value_count, team);
}

void qa_write_source_match_field(const qa_source_match_services *match,
                                 qa_actor_id actor, qa_source_match_field field,
                                 double value) {
  /* TS match?.player(actor) then a null check: both an absent match
   * and an absent player raise this same error. */
  if (match == NULL) {
    qa_error("Component match mutation requires an admitted destination player");
  }
  qa_source_match_player_result player =
      match->player((void *)match->ctx, actor);
  if (!player.has_value) {
    qa_error("Component match mutation requires an admitted destination player");
  }
  if (field.is_score) {
    player.value.set_score(player.value.ctx, value);
  } else {
    player.value.set_team(player.value.ctx,
                          qa_source_team(field.values, field.value_count, value));
  }
}

qa_string_view_list qa_source_team_command(qa_source_primary_match profile,
                                           const char *team) {
  for (size_t i = 0; i < profile.team_count; i++) {
    if (string_equal(profile.teams[i].alias.team, team)) {
      return profile.teams[i].arguments;
    }
  }
  char message[1024];
  (void)snprintf(message, sizeof message,
                 "The original source has no declared command for team %s",
                 team != NULL ? team : "unassigned");
  qa_error(message);
}

double qa_source_score(double value) {
  if (!(value == trunc(value)) || value < -2147483648.0 || value > 2147483647.0) {
    qa_error("Original score requires an int32 value");
  }
  return value;
}
