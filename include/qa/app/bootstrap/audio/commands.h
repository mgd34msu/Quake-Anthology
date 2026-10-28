/* Port of src/app/bootstrap/audio/commands.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_AUDIO_COMMANDS_H
#define QA_APP_BOOTSTRAP_AUDIO_COMMANDS_H
#include <stddef.h>
#include "qa/core/commands/documentation.h"
/* TS applicationAudioCommands. */
extern const char *const qa_app_application_audio_commands[];
extern const size_t qa_app_application_audio_command_count;
/* TS cdCommandDocumentation / musicCommandDocumentation. */
extern const qa_command_documentation qa_app_cd_command_documentation;
extern const qa_command_documentation qa_app_music_command_documentation;
#endif
