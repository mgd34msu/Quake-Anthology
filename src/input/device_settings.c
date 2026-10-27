/* Port of src/input/device-settings.ts. GPL-2.0-or-later. */
#include "qa/input/device_settings.h"

#include <string.h>

typedef struct midi_default {
  const char *name;
  const char *value;
} midi_default;

static const midi_default midi_defaults[] = {
    {"in_midi", "0"}, {"in_midiport", "1"}, {"in_midichannel", "1"},
    {"in_mididevice", "0"}, {"in_midiseat", "1"}};

typedef struct joystick_default {
  const char *name;
  const char *value;
  int flags;
} joystick_default;

#if defined(_WIN32)
#define QA_JOYSTICK_PROFILE_DEFAULT "windows"
#else
#define QA_JOYSTICK_PROFILE_DEFAULT "linux"
#endif

static const joystick_default joystick_defaults[] = {
    {"in_mouse", "1", QA_CVAR_FLAG_ARCHIVE},
    {"in_dgamouse", "1", QA_CVAR_FLAG_ARCHIVE},
    {"in_subframe", "1", QA_CVAR_FLAG_ARCHIVE},
    {"in_nograb", "0", QA_CVAR_FLAG_NONE},
    {"in_joystick", "0", QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_LATCH},
    {"in_debugjoystick", "0", QA_CVAR_FLAG_TEMPORARY},
    {"joy_threshold", "0.15", QA_CVAR_FLAG_ARCHIVE},
    {"in_joystickProfile", QA_JOYSTICK_PROFILE_DEFAULT,
     QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_LATCH},
    {"in_joyBallScale", "0.02", QA_CVAR_FLAG_ARCHIVE},
    {"in_joystickSeat", "1", QA_CVAR_FLAG_ARCHIVE}};

const char *const qa_input_device_cvar_names[QA_INPUT_DEVICE_CVAR_NAME_COUNT] = {
    "in_midi",        "in_midiport",   "in_midichannel", "in_mididevice",
    "in_midiseat",    "in_mouse",      "in_dgamouse",    "in_subframe",
    "in_nograb",      "in_joystick",   "in_debugjoystick", "joy_threshold",
    "in_joystickProfile", "in_joyBallScale", "in_joystickSeat"};

static void register_unless_present(qa_cvar_registry *cvars, const char *name,
                                    const char *value, double flags) {
  qa_cvar_read found;
  bool present = qa_cvar_registry_find(cvars, name, &found);
  if (present) qa_cvar_read_free(&found);
  if (!present || qa_cvar_registry_is_console_created(cvars, name)) {
    qa_cvar_snapshot snapshot;
    memset(&snapshot, 0, sizeof snapshot);
    (void)qa_cvar_registry_register(cvars, name, value, flags, &snapshot);
    qa_cvar_read_free(&snapshot);
  }
}

void qa_register_midi_settings(qa_cvar_registry *cvars) {
  for (size_t i = 0; i < sizeof midi_defaults / sizeof midi_defaults[0]; i++) {
    register_unless_present(cvars, midi_defaults[i].name, midi_defaults[i].value,
                            QA_CVAR_FLAG_ARCHIVE);
  }
}

void qa_register_source_input_settings(qa_cvar_registry *cvars) {
  qa_command_dialect dialect = qa_cvar_registry_dialect(cvars);
  for (size_t i = 0; i < sizeof joystick_defaults / sizeof joystick_defaults[0]; i++) {
    int flags = joystick_defaults[i].flags;
    double source_flags;
    if (dialect == QA_COMMAND_DIALECT_Q2_CLASSIC ||
        dialect == QA_COMMAND_DIALECT_Q2_RERELEASE) {
      source_flags = ((flags & QA_CVAR_FLAG_ARCHIVE) != 0 ? QA_Q2_CVAR_FLAG_ARCHIVE : 0) |
                     ((flags & QA_CVAR_FLAG_LATCH) != 0 ? QA_Q2_CVAR_FLAG_LATCH : 0);
    } else if (dialect == QA_COMMAND_DIALECT_Q3) {
      source_flags = flags;
    } else {
      source_flags = flags & QA_CVAR_FLAG_ARCHIVE;
    }
    register_unless_present(cvars, joystick_defaults[i].name,
                            joystick_defaults[i].value, source_flags);
  }
}

void qa_register_input_device_cvars(qa_cvar_registry *cvars) {
  qa_register_midi_settings(cvars);
  qa_register_source_input_settings(cvars);
}
