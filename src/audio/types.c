/* Port of src/audio/types.ts. GPL-2.0-or-later. */
#include "qa/audio/types.h"

#include <stdio.h>
#include <string.h>

#include "qa/foundation/support.h"

const char *qa_sound_family_name(qa_sound_family family) {
  switch (family) {
    case QA_SOUND_FAMILY_Q1: return "q1";
    case QA_SOUND_FAMILY_Q2: return "q2";
    case QA_SOUND_FAMILY_Q3: return "q3";
  }
  return "q3";
}

bool qa_sound_family_parse(const char *text, qa_sound_family *out) {
  if (strcmp(text, "q1") == 0) { *out = QA_SOUND_FAMILY_Q1; return true; }
  if (strcmp(text, "q2") == 0) { *out = QA_SOUND_FAMILY_Q2; return true; }
  if (strcmp(text, "q3") == 0) { *out = QA_SOUND_FAMILY_Q3; return true; }
  return false;
}

qa_sound_channel_command qa_source_sound_channel(qa_sound_family family,
                                                 int64_t channel) {
  if (channel < 0 && !(family == QA_SOUND_FAMILY_Q1 && channel == -1))
    qa_range_error("Invalid source sound channel");
  qa_sound_channel_command command = {0};
  if (channel == -1) {
    command.kind = QA_SOUND_CHANNEL_REPLACE_ACTOR;
    return command;
  }
  if (channel == 0) {
    command.kind = QA_SOUND_CHANNEL_AUTO;
    return command;
  }
  static const char *const q3_names[] = {"local", "weapon", "voice", "item",
                                         "body", "local-sound", "announcer"};
  static const char *const q12_names[] = {"weapon", "voice", "item", "body"};
  command.kind = QA_SOUND_CHANNEL_CHANNEL;
  if (family == QA_SOUND_FAMILY_Q3 && channel - 1 < 7)
    snprintf(command.channel, sizeof command.channel, "%s", q3_names[channel - 1]);
  else if (family != QA_SOUND_FAMILY_Q3 && channel - 1 < 4)
    snprintf(command.channel, sizeof command.channel, "%s", q12_names[channel - 1]);
  else
    snprintf(command.channel, sizeof command.channel, "%s:extension:%lld",
             qa_sound_family_name(family), (long long)channel);
  return command;
}
