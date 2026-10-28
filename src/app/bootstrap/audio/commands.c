/* Port of src/app/bootstrap/audio/commands.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/audio/commands.h"
const char *const qa_app_application_audio_commands[] = {
  "snd_restart", "cd", "music", "soundinfo", "soundlist",
  "play", "stopsound", "s_info", "s_list", "s_stop"
};
const size_t qa_app_application_audio_command_count =
  sizeof qa_app_application_audio_commands / sizeof qa_app_application_audio_commands[0];
static const char *const cd_examples[] = {"cd loop 2", "cd pause", "cd resume", "cd stop", "cd remap 1 3", "cd info"};
static const char *const cd_values[] = {"play", "loop", "stop", "pause", "resume", "on", "off", "reset", "remap", "info"};
const qa_command_documentation qa_app_cd_command_documentation = {
  "Control the selected soundtrack: play or loop a numbered track, stop, pause, resume, enable, reset, remap or inspect it.",
  "cd <play|loop> <track> | cd <stop|pause|resume|on|off|reset|info> | cd remap [tracks...]",
  cd_examples, 6, cd_values, 10
};
static const char *const music_examples[] = {"music music/win", "music music/intro music/loop"};
const qa_command_documentation qa_app_music_command_documentation = {
  "Play a named soundtrack once, or an intro followed by a looping track, using the current content and music gain.",
  "music <intro> [loop]", music_examples, 2, NULL, 0
};
