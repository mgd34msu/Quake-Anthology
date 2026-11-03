/* Port of src/content/q3/presentation/server-commands.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/server_commands.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/numeric.h"
#include "qa/core/info_string.h"
#include "qa/foundation/support.h"

static const char *const SOUND_NAMES[] = {"countPrepareSound", "countPrepareTeamSound", "countFightSound",
                                          "talkSound", "voteNow", "votePassed", "voteFailed"};
const char *qa_q3_pres_client_server_command_sound_name(qa_q3_pres_client_server_command_sound sound) {
  if (sound < 0 || sound >= QA_Q3_PRES_CLIENT_SERVER_COMMAND_SOUND_COUNT) return "";
  return SOUND_NAMES[sound];
}

static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double truncated = trunc(fmod(fabs(value), 4294967296.0));
  uint32_t bits = qa_js_to_uint32(truncated);
  if (value < 0) bits = ~bits + 1u;
  int32_t result;
  memcpy(&result, &bits, sizeof result);
  return result;
}
static char *xstrdup(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}
#define STR(text) ((text) != NULL ? (text) : "")
static void slice_copy(char *out, size_t out_size, const char *value, size_t length) {
  size_t take = strlen(value);
  if (take > length) take = length;
  if (take >= out_size) take = out_size - 1;
  memcpy(out, value, take);
  out[take] = '\0';
}
static bool fold_equal(const char *a, const char *b) {
  for (;;) {
    unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
    if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
    if (ca != cb) return false;
    if (ca == 0) return true;
    a++;
    b++;
  }
}
static bool fold_starts_with(const char *text, const char *prefix) {
  while (*prefix != '\0') {
    unsigned char ca = (unsigned char)*text, cb = (unsigned char)*prefix;
    if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
    if (ca != cb) return false;
    text++;
    prefix++;
  }
  return true;
}

typedef struct voice_chat {
  char id[64];
  double num_sounds;
  qa_pcm_sound *sounds[64];
  char chats[64][64];
} voice_chat;
typedef struct voice_chat_list {
  char name[64];
  qa_q3_player_gender gender;
  double num_voice_chats;
  voice_chat voice_chats[64];
} voice_chat_list;
typedef struct head_voice { char headmodel[64]; double voice_chat_num; } head_voice;
typedef struct buffered_voice {
  double client_num;
  qa_pcm_sound *snd;
  bool voice_only;
  char *cmd, *message; /* owned */
} buffered_voice;

struct qa_q3_pres_client_server_command_runtime {
  qa_q3_pres_client_server_command_host host;
  bool closed;
  voice_chat_list *voice_lists; /* [8] */
  head_voice head_voices[64];
  buffered_voice voices[32];
  char *config_scratch; /* owned copy of the last config() result */
};
typedef qa_q3_pres_client_server_command_runtime runtime_t;

static void open_check(runtime_t *runtime) {
  if (runtime->closed) qa_range_error("Cgame command runtime is closed");
}
static void at_check(double index, double length) {
  if (index != trunc(index) || index < 0 || index >= length) {
    if (index == trunc(index) && fabs(index) < 1e21) {
      qa_range_error("Source array index %lld outside %lld", (long long)index, (long long)length);
    }
    qa_range_error("Source array index %s outside %s", QA_JS_NUMBER(index), QA_JS_NUMBER(length));
  }
}

bool qa_q3_pres_client_server_command_runtime_create(runtime_t **out,
    qa_q3_pres_client_server_command_host host) {
  if (host.state->product != host.static_state->product) {
    qa_range_error("Cgame state products differ");
  }
  for (int i = 0; i < 64; i++) {
    if (host.client_info(host.clients_ctx, i) != &host.static_state->client_info[i]) {
      qa_range_error("ClientInfoStore must use canonical cgs slots");
    }
  }
  runtime_t *runtime = calloc(1, sizeof *runtime);
  if (runtime == NULL) return false;
  runtime->voice_lists = calloc(8, sizeof *runtime->voice_lists);
  if (runtime->voice_lists == NULL) {
    free(runtime);
    return false;
  }
  runtime->host = host;
  *out = runtime;
  return true;
}
void qa_q3_pres_client_server_command_runtime_destroy(runtime_t *runtime) {
  if (runtime == NULL) return;
  for (int i = 0; i < 32; i++) {
    free(runtime->voices[i].cmd);
    free(runtime->voices[i].message);
  }
  free(runtime->voice_lists);
  free(runtime->config_scratch);
  free(runtime);
}
bool qa_q3_pres_client_server_command_runtime_dispose(runtime_t *runtime) {
  runtime->closed = true;
  return runtime->host.reset_clients(runtime->host.clients_ctx);
}

static bool cvar_snapshot(runtime_t *runtime, const char *name, qa_cvar_snapshot *out) {
  return runtime->host.read_vm_cvar(runtime->host.ctx, name, out);
}
static const char *config(runtime_t *runtime, double index) {
  if (index != trunc(index) || index < 0 || index >= 1024) {
    qa_range_error("CG_ConfigString: bad index");
  }
  const char *value = STR(runtime->host.config_string(runtime->host.ctx, index));
  if (strlen(value) >= 16000) qa_range_error("Configstring exceeds MAX_GAMESTATE_CHARS");
  char *copy = xstrdup(value);
  if (copy == NULL) qa_range_error("out of memory");
  free(runtime->config_scratch);
  runtime->config_scratch = copy;
  return copy;
}
static void local_sound(runtime_t *runtime, qa_q3_pres_client_server_command_sound name) {
  runtime->host.start_local_sound(runtime->host.ctx, runtime->host.sound(runtime->host.ctx, name), 7);
}
static void local_sound_channel(runtime_t *runtime, qa_q3_pres_client_server_command_sound name, double channel) {
  runtime->host.start_local_sound(runtime->host.ctx, runtime->host.sound(runtime->host.ctx, name), channel);
}

static const char *argv_at(const char *const *values, size_t count, double index, char *buffer) {
  if (index != trunc(index) || index < 0 || index >= (double)count) {
    buffer[0] = '\0';
    return buffer;
  }
  slice_copy(buffer, 1024, values[(size_t)index], 1023);
  return buffer;
}
static int32_t argv_integer(const char *const *values, size_t count, double index) {
  char buffer[1024];
  return qa_game_atoi(argv_at(values, count, index, buffer));
}
/* commandCopy: validate + truncate into an owned token array. */
static bool command_copy(const char *const *values, size_t count, char ***out) {
  if (count > 1024) qa_range_error("Server command exceeds MAX_STRING_TOKENS");
  char **copies = calloc(count == 0 ? 1 : count, sizeof *copies);
  if (copies == NULL) return false;
  for (size_t i = 0; i < count; i++) {
    copies[i] = malloc(1024);
    if (copies[i] == NULL) {
      for (size_t j = 0; j < i; j++) free(copies[j]);
      free(copies);
      return false;
    }
    slice_copy(copies[i], 1024, STR(values[i]), 1023);
  }
  *out = copies;
  return true;
}
static void free_tokens(char **tokens, size_t count) {
  if (tokens == NULL) return;
  for (size_t i = 0; i < count; i++) free(tokens[i]);
  free(tokens);
}

/* COM_ParseExt byte semantics (signed-byte comparisons). */
typedef struct byte_tokenizer { const char *text; size_t length, offset; } byte_tokenizer;
static int tokenizer_byte(const byte_tokenizer *tokenizer) {
  if (tokenizer->offset >= tokenizer->length) return 0;
  unsigned char value = (unsigned char)tokenizer->text[tokenizer->offset];
  return value >= 128 ? (int)value - 256 : (int)value;
}
static bool tokenizer_starts(const byte_tokenizer *tokenizer, const char *prefix) {
  size_t length = strlen(prefix);
  return tokenizer->offset + length <= tokenizer->length &&
         memcmp(tokenizer->text + tokenizer->offset, prefix, length) == 0;
}
static void tokenizer_next(byte_tokenizer *tokenizer, bool allow_line_breaks, char *out, size_t out_size) {
  bool newline = false;
  for (;;) {
    while (tokenizer_byte(tokenizer) <= 32) {
      if (tokenizer_byte(tokenizer) == 0) {
        out[0] = '\0';
        return;
      }
      if (tokenizer_byte(tokenizer) == 10) newline = true;
      tokenizer->offset++;
    }
    if (newline && !allow_line_breaks) {
      out[0] = '\0';
      return;
    }
    if (tokenizer_starts(tokenizer, "//")) {
      tokenizer->offset += 2;
      while (tokenizer_byte(tokenizer) != 0 && tokenizer_byte(tokenizer) != 10) tokenizer->offset++;
    } else if (tokenizer_starts(tokenizer, "/*")) {
      tokenizer->offset += 2;
      while (tokenizer_byte(tokenizer) != 0 && !tokenizer_starts(tokenizer, "*/")) tokenizer->offset++;
      if (tokenizer_byte(tokenizer) != 0) tokenizer->offset += 2;
    } else {
      break;
    }
  }
  if (tokenizer_byte(tokenizer) == 34) {
    size_t start = ++tokenizer->offset;
    while (tokenizer_byte(tokenizer) != 0 && tokenizer_byte(tokenizer) != 34) tokenizer->offset++;
    size_t length = tokenizer->offset - start;
    if (length >= 1024) qa_range_error("Quoted source token overflows MAX_TOKEN_CHARS");
    if (length >= out_size) length = out_size - 1;
    memcpy(out, tokenizer->text + start, length);
    out[length] = '\0';
    if (tokenizer_byte(tokenizer) == 34) tokenizer->offset++;
    return;
  }
  size_t start = tokenizer->offset;
  while (tokenizer_byte(tokenizer) > 32) tokenizer->offset++;
  size_t length = tokenizer->offset - start;
  if (length >= 1024) {
    out[0] = '\0';
    return;
  }
  if (length >= out_size) length = out_size - 1;
  memcpy(out, tokenizer->text + start, length);
  out[length] = '\0';
}

static qa_q3_base_game_type valid_game_type(int32_t value) {
  switch (value) {
    case QA_Q3_BASE_GT_FFA:
    case QA_Q3_BASE_GT_TOURNAMENT:
    case QA_Q3_BASE_GT_SINGLE_PLAYER:
    case QA_Q3_BASE_GT_TEAM:
    case QA_Q3_BASE_GT_CTF:
    case QA_Q3_BASE_GT_1FCTF:
    case QA_Q3_BASE_GT_OBELISK:
    case QA_Q3_BASE_GT_HARVESTER: return (qa_q3_base_game_type)value;
    default: return (qa_q3_base_game_type)value; /* TS returns any other value unchanged */
  }
}
static double order_task(const char *command) {
  if (fold_equal(command, "getflag") || fold_equal(command, "offense")) return 1;
  if (fold_equal(command, "defend") || fold_equal(command, "defendflag")) return 2;
  if (fold_equal(command, "patrol")) return 3;
  if (fold_equal(command, "followme")) return 4;
  if (fold_equal(command, "returnflag")) return 5;
  if (fold_equal(command, "followflagcarrier")) return 6;
  if (fold_equal(command, "camp")) return 7;
  return -1;
}

bool qa_q3_pres_client_server_command_runtime_parse_server_info(runtime_t *runtime) {
  open_check(runtime);
  const char *info = config(runtime, 0);
  qa_q3_pres_client_game_static_state *state = runtime->host.static_state;
  char *value = NULL;
  bool ok = true;
  value = qa_info_value_for_key_bounded(info, "g_gametype", 8192);
  state->game_type = valid_game_type(qa_game_atoi(STR(value)));
  free(value);
  char game_type_text[32];
  snprintf(game_type_text, sizeof game_type_text, "%d", (int)state->game_type);
  runtime->host.set_cvar(runtime->host.ctx, "g_gametype", game_type_text);
  value = qa_info_value_for_key_bounded(info, "dmflags", 8192);
  state->dm_flags = qa_game_atoi(STR(value));
  free(value);
  value = qa_info_value_for_key_bounded(info, "teamflags", 8192);
  state->team_flags = qa_game_atoi(STR(value));
  free(value);
  value = qa_info_value_for_key_bounded(info, "fraglimit", 8192);
  state->fraglimit = qa_game_atoi(STR(value));
  free(value);
  value = qa_info_value_for_key_bounded(info, "capturelimit", 8192);
  state->capturelimit = qa_game_atoi(STR(value));
  free(value);
  value = qa_info_value_for_key_bounded(info, "timelimit", 8192);
  state->timelimit = qa_game_atoi(STR(value));
  free(value);
  value = qa_info_value_for_key_bounded(info, "sv_maxclients", 8192);
  state->maxclients = qa_game_atoi(STR(value));
  free(value);
  value = qa_info_value_for_key_bounded(info, "mapname", 8192);
  {
    char mapname[128];
    snprintf(mapname, sizeof mapname, "maps/%.100s.bsp", STR(value));
    char sliced[64];
    slice_copy(sliced, sizeof sliced, mapname, 63);
    ok = qa_q3_pres_set_string(&state->mapname, sliced);
  }
  free(value);
  value = qa_info_value_for_key_bounded(info, "g_redTeam", 8192);
  {
    char sliced[64];
    slice_copy(sliced, sizeof sliced, STR(value), 63);
    ok = ok && qa_q3_pres_set_string(&state->red_team, sliced);
  }
  free(value);
  runtime->host.set_cvar(runtime->host.ctx, "g_redTeam", STR(state->red_team));
  value = qa_info_value_for_key_bounded(info, "g_blueTeam", 8192);
  {
    char sliced[64];
    slice_copy(sliced, sizeof sliced, STR(value), 63);
    ok = ok && qa_q3_pres_set_string(&state->blue_team, sliced);
  }
  free(value);
  runtime->host.set_cvar(runtime->host.ctx, "g_blueTeam", STR(state->blue_team));
  return ok;
}
static void flag_status(runtime_t *runtime) {
  const char *value = config(runtime, 23);
  qa_q3_pres_client_game_static_state *state = runtime->host.static_state;
  size_t length = strlen(value);
  if (state->game_type == QA_Q3_BASE_GT_CTF) {
    state->redflag = (length == 0 ? 0 : (unsigned char)value[0]) - 48;
    /* TS value.charCodeAt(1) is NaN for an empty string. */
    state->blueflag = (length == 1 ? 0 : length == 0 ? NAN : (double)(unsigned char)value[1]) - 48;
  } else if (runtime->host.state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK &&
             state->game_type == QA_Q3_BASE_GT_1FCTF) {
    state->flag_status = (length == 0 ? 0 : (unsigned char)value[0]) - 48;
  }
}
bool qa_q3_pres_client_server_command_runtime_set_config_values(runtime_t *runtime) {
  open_check(runtime);
  qa_q3_pres_client_game_static_state *state = runtime->host.static_state;
  state->scores1 = qa_game_atoi(config(runtime, 6));
  state->scores2 = qa_game_atoi(config(runtime, 7));
  state->level_start_time = qa_game_atoi(config(runtime, 21));
  flag_status(runtime);
  runtime->host.state->warmup = qa_game_atoi(config(runtime, 5));
  return true;
}
static void parse_warmup(runtime_t *runtime) {
  qa_q3_pres_client_game_state *state = runtime->host.state;
  int32_t warmup = qa_game_atoi(config(runtime, 5));
  state->warmup_count = -1;
  if (warmup > 0 && state->warmup <= 0) {
    local_sound(runtime, state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK &&
            runtime->host.static_state->game_type >= QA_Q3_BASE_GT_CTF &&
            runtime->host.static_state->game_type <= QA_Q3_BASE_GT_HARVESTER
        ? QA_Q3_PRES_COUNT_PREPARE_TEAM_SOUND : QA_Q3_PRES_COUNT_PREPARE_SOUND);
  }
  state->warmup = warmup;
}
static void parse_scores(runtime_t *runtime, const char *const *command, size_t count) {
  qa_q3_pres_client_game_state *state = runtime->host.state;
  int32_t num_scores = argv_integer(command, count, 1);
  state->num_scores = num_scores < 64 ? num_scores : 64;
  state->team_scores[0] = argv_integer(command, count, 2);
  state->team_scores[1] = argv_integer(command, count, 3);
  for (int i = 0; i < 64; i++) memset(&state->scores[i], 0, sizeof state->scores[i]);
  for (double i = 0; i < state->num_scores; i++) {
    double base = i * 14;
    int32_t client_number = argv_integer(command, count, base + 4);
    int32_t client = client_number < 0 || client_number >= 64 ? 0 : client_number;
    qa_q3_pres_client_info *info = runtime->host.client_info(runtime->host.clients_ctx, client);
    qa_q3_pres_client_score score;
    memset(&score, 0, sizeof score);
    score.client = client;
    score.score = argv_integer(command, count, base + 5);
    score.ping = argv_integer(command, count, base + 6);
    score.time = argv_integer(command, count, base + 7);
    score.score_flags = argv_integer(command, count, base + 8);
    score.accuracy = argv_integer(command, count, base + 10);
    score.impressive_count = argv_integer(command, count, base + 11);
    score.excellent_count = argv_integer(command, count, base + 12);
    score.guantlet_count = argv_integer(command, count, base + 13);
    score.defend_count = argv_integer(command, count, base + 14);
    score.assist_count = argv_integer(command, count, base + 15);
    score.perfect = argv_integer(command, count, base + 16);
    score.captures = argv_integer(command, count, base + 17);
    score.team = info->team;
    state->scores[(size_t)i] = score;
    info->score = score.score;
    info->powerups = argv_integer(command, count, base + 9);
  }
  if (state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    runtime->host.set_score_selection(runtime->host.ctx);
  }
}
static void parse_team_info(runtime_t *runtime, const char *const *command, size_t count) {
  /* TEAM_MAXOVERLAY is 32. A server chooses these numbers, so anything outside the tables is ignored. */
  int32_t team_count = argv_integer(command, count, 1);
  if (team_count > 32) team_count = 32;
  if (team_count < 0) team_count = 0;
  qa_q3_pres_client_game_state *state = runtime->host.state;
  state->num_sorted_team_players = team_count;
  for (int32_t i = 0; i < team_count; i++) {
    int32_t client = argv_integer(command, count, i * 6 + 2);
    if (client < 0 || client >= 64) { state->num_sorted_team_players = i; return; }
    qa_q3_pres_client_info *info = runtime->host.client_info(runtime->host.clients_ctx, client);
    state->sorted_team_players[i] = client;
    info->location = argv_integer(command, count, i * 6 + 3);
    info->health = argv_integer(command, count, i * 6 + 4);
    info->armor = argv_integer(command, count, i * 6 + 5);
    info->cur_weapon = argv_integer(command, count, i * 6 + 6);
    info->powerups = argv_integer(command, count, i * 6 + 7);
  }
}
bool qa_q3_pres_client_server_command_runtime_shader_state_changed(runtime_t *runtime) {
  open_check(runtime);
  const char *value = config(runtime, 24);
  size_t length = strlen(value);
  size_t offset = 0;
  while (offset < length) {
    const char *equals = strchr(value + offset, '=');
    if (equals == NULL) break;
    const char *colon = strchr(equals + 1, ':');
    if (colon == NULL) break;
    const char *end = strchr(colon + 1, '@');
    if (end == NULL) break;
    size_t original_length = (size_t)(equals - (value + offset));
    size_t replacement_length = (size_t)(colon - (equals + 1));
    size_t time_length = (size_t)(end - (colon + 1));
    if (original_length >= 64 || replacement_length >= 64 || time_length >= 16) {
      qa_range_error("Shader remap exceeds source scratch buffers");
    }
    char original[64], replacement[64], time_text[16];
    memcpy(original, value + offset, original_length);
    original[original_length] = '\0';
    memcpy(replacement, equals + 1, replacement_length);
    replacement[replacement_length] = '\0';
    memcpy(time_text, colon + 1, time_length);
    time_text[time_length] = '\0';
    runtime->host.remap_shader(runtime->host.ctx, original, replacement, time_text);
    open_check(runtime);
    offset = (size_t)(end - value) + 1;
  }
  return true;
}
bool qa_q3_pres_client_server_command_runtime_start_music(runtime_t *runtime) {
  open_check(runtime);
  const char *text = config(runtime, 2);
  byte_tokenizer tokenizer = {text, strlen(text), 0};
  char first[1024], second[1024], intro[64], loop[64];
  tokenizer_next(&tokenizer, true, first, sizeof first);
  tokenizer_next(&tokenizer, true, second, sizeof second);
  slice_copy(intro, sizeof intro, first, 63);
  slice_copy(loop, sizeof loop, second, 63);
  runtime->host.start_background_track(runtime->host.ctx, intro, loop);
  open_check(runtime);
  return true;
}
bool qa_q3_pres_client_server_command_runtime_build_spectator_string(runtime_t *runtime) {
  open_check(runtime);
  qa_q3_pres_client_game_state *state = runtime->host.state;
  if (!qa_q3_pres_set_string(&state->spectator_list, "")) return false;
  char list[1024] = "";
  for (int i = 0; i < 64; i++) {
    qa_q3_pres_client_info *info = runtime->host.client_info(runtime->host.clients_ctx, i);
    if (info->info_valid && info->team == QA_Q3_BASE_TEAM_SPECTATOR) {
      char next[1024];
      snprintf(next, sizeof next, "%.900s%.100s     ", list, STR(info->name));
      slice_copy(list, sizeof list, next, 1023);
    }
  }
  if (!qa_q3_pres_set_string(&state->spectator_list, list)) return false;
  if ((double)strlen(STR(state->spectator_list)) != state->spectator_len) {
    state->spectator_len = (double)strlen(STR(state->spectator_list));
    state->spectator_width = -1;
  }
  return true;
}

static bool config_modified(runtime_t *runtime, const char *const *command, size_t count) {
  int32_t index = argv_integer(command, count, 1);
  runtime->host.refresh_game_state(runtime->host.ctx);
  const char *value = config(runtime, index);
  qa_q3_pres_client_game_static_state *state = runtime->host.static_state;
  switch (index) {
    case 0: return qa_q3_pres_client_server_command_runtime_parse_server_info(runtime);
    case 2: return qa_q3_pres_client_server_command_runtime_start_music(runtime);
    case 5: parse_warmup(runtime); return true;
    case 6: state->scores1 = qa_game_atoi(value); return true;
    case 7: state->scores2 = qa_game_atoi(value); return true;
    case 8: state->vote_time = qa_game_atoi(value); state->vote_modified = true; return true;
    case 9: {
      char sliced[1024];
      slice_copy(sliced, sizeof sliced, value, 1023);
      if (!qa_q3_pres_set_string(&state->vote_string, sliced)) return false;
      if (runtime->host.state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
        local_sound(runtime, QA_Q3_PRES_VOTE_NOW);
      }
      return true;
    }
    case 10: state->vote_yes = qa_game_atoi(value); state->vote_modified = true; return true;
    case 11: state->vote_no = qa_game_atoi(value); state->vote_modified = true; return true;
    case 21: state->level_start_time = qa_game_atoi(value); return true;
    case 22: runtime->host.state->intermission_started = qa_game_atoi(value) != 0; return true;
    case 23: flag_status(runtime); return true;
    case 24: return qa_q3_pres_client_server_command_runtime_shader_state_changed(runtime);
    default: break;
  }
  if (index >= 12 && index <= 19) {
    int slot = index % 2;
    if (index < 14) {
      state->team_vote_time[slot] = qa_game_atoi(value);
      state->team_vote_modified[slot] = true;
    } else if (index < 16) {
      if (strlen(value) >= 1024) qa_range_error("Team vote string exceeds source row buffer");
      if (!qa_q3_pres_set_string(&state->team_vote_string[slot], value)) return false;
      if (runtime->host.state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
        local_sound(runtime, QA_Q3_PRES_VOTE_NOW);
      }
    } else if (index < 18) {
      state->team_vote_yes[slot] = qa_game_atoi(value);
      state->team_vote_modified[slot] = true;
    } else {
      state->team_vote_no[slot] = qa_game_atoi(value);
      state->team_vote_modified[slot] = true;
    }
  } else if (index >= 32 && index < 288) {
    qa_q3_pres_scene_model model;
    if (!runtime->host.register_model(runtime->host.resources_ctx, value, &model)) return false;
    open_check(runtime);
    state->game_models[index - 32] = model;
  } else if (index >= 288 && index < 544) {
    if (value[0] != '*') {
      qa_pcm_sound *sound = NULL;
      if (!runtime->host.register_sound(runtime->host.ctx, value, false, &sound)) return false;
      open_check(runtime);
      state->game_sounds[index - 288] = sound;
    }
  } else if (index >= 544 && index < 608) {
    if (!runtime->host.new_client_info(runtime->host.clients_ctx, index - 544, value)) return false;
    open_check(runtime);
    return qa_q3_pres_client_server_command_runtime_build_spectator_string(runtime);
  }
  return true;
}

bool qa_q3_pres_client_server_command_runtime_add_to_team_chat(runtime_t *runtime, const char *input) {
  open_check(runtime);
  qa_cvar_snapshot height_cvar, time_cvar;
  if (!cvar_snapshot(runtime, "cg_teamChatHeight", &height_cvar)) return false;
  double height = height_cvar.integer_value < 8 ? height_cvar.integer_value : 8;
  qa_cvar_read_free(&height_cvar);
  if (!cvar_snapshot(runtime, "cg_teamChatTime", &time_cvar)) return false;
  double chat_time = time_cvar.integer_value;
  qa_cvar_read_free(&time_cvar);
  qa_q3_pres_client_game_static_state *state = runtime->host.static_state;
  if (height <= 0 || chat_time <= 0) {
    state->team_chat_pos = 0;
    state->team_last_chat_pos = 0;
    return true;
  }
  const char *text = input;
  size_t text_length = strlen(text);
  ptrdiff_t offset = 0;
  char line[512] = "";
  size_t line_length = 0;
  int visible = 0;
  ptrdiff_t last_space = -1;
  char color = '7';
  bool ok = true;
  while (offset < (ptrdiff_t)text_length && ok) {
    if (visible > 79) {
      if (last_space >= 0) {
        offset -= (ptrdiff_t)line_length - last_space;
        offset++;
        line_length = (size_t)last_space;
        line[line_length] = '\0';
      }
      if (line_length > 240) qa_range_error("Team chat exceeds source color-expanded line buffer");
      int32_t height_i = to_int32(height);
      int32_t slot = height_i == 0 ? 0 : to_int32(state->team_chat_pos) % height_i;
      if (slot < 0) slot += height_i;
      ok = qa_q3_pres_set_string(&state->team_chat_msgs[slot], line);
      state->team_chat_msg_times[slot] = runtime->host.state->time;
      state->team_chat_pos = to_int32(state->team_chat_pos + 1);
      line[0] = '^';
      line[1] = color;
      line[2] = '\0';
      line_length = 2;
      visible = 0;
      last_space = -1;
    }
    if (offset < 0) offset = 0;
    if (offset >= (ptrdiff_t)text_length) break;
    char character = text[offset];
    char next = offset + 1 < (ptrdiff_t)text_length ? text[offset + 1] : '\0';
    if (character == '^' && next != '\0' && next != '^') {
      if (line_length + 2 < sizeof line) {
        line[line_length++] = character;
        line[line_length++] = next;
        line[line_length] = '\0';
      }
      color = next;
      offset += 2;
      continue;
    }
    if (character == ' ') last_space = (ptrdiff_t)line_length;
    if (line_length + 1 < sizeof line) {
      line[line_length++] = character;
      line[line_length] = '\0';
    }
    offset++;
    visible++;
  }
  if (ok) {
    if (line_length > 240) qa_range_error("Team chat exceeds source color-expanded line buffer");
    int32_t height_i = to_int32(height);
    int32_t slot = height_i == 0 ? 0 : to_int32(state->team_chat_pos) % height_i;
    if (slot < 0) slot += height_i;
    ok = qa_q3_pres_set_string(&state->team_chat_msgs[slot], line);
    state->team_chat_msg_times[slot] = runtime->host.state->time;
    state->team_chat_pos = to_int32(state->team_chat_pos + 1);
  }
  if (state->team_chat_pos - state->team_last_chat_pos > height) {
    state->team_last_chat_pos = state->team_chat_pos - height;
  }
  return ok;
}

static bool cvar_integer(runtime_t *runtime, const char *name, double *out) {
  qa_cvar_snapshot snapshot;
  if (!cvar_snapshot(runtime, name, &snapshot)) return false;
  *out = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  return true;
}
static bool map_restart(runtime_t *runtime) {
  qa_q3_pres_client_game_state *state = runtime->host.state;
  double show_miss = 0;
  if (!cvar_integer(runtime, "cg_showmiss", &show_miss)) return false;
  if (show_miss != 0) runtime->host.print(runtime->host.ctx, "CG_MapRestart\n");
  runtime->host.clear_local_entities(runtime->host.ctx);
  runtime->host.clear_marks(runtime->host.ctx);
  runtime->host.clear_particles(runtime->host.ctx);
  open_check(runtime);
  state->fraglimit_warnings = 0;
  state->timelimit_warnings = 0;
  state->intermission_started = false;
  runtime->host.static_state->vote_time = 0;
  state->map_restart = true;
  if (!qa_q3_pres_client_server_command_runtime_start_music(runtime)) return false;
  runtime->host.clear_looping_sounds(runtime->host.ctx, true);
  if (state->warmup == 0) {
    local_sound(runtime, QA_Q3_PRES_COUNT_FIGHT_SOUND);
    runtime->host.center_print(runtime->host.ctx, "FIGHT!", 120, 64);
  }
  if (state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    double single_player = 0;
    if (!cvar_integer(runtime, "ui_singlePlayerActive", &single_player)) return false;
    if (single_player != 0) {
      char time_text[32];
      if (state->time == trunc(state->time) && fabs(state->time) < 1e21) {
        snprintf(time_text, sizeof time_text, "%lld", (long long)state->time);
      } else {
        snprintf(time_text, sizeof time_text, "%s", QA_JS_NUMBER(state->time));
      }
      runtime->host.set_cvar(runtime->host.ctx, "ui_matchStartTime", time_text);
      qa_cvar_snapshot demo_name, demo_flag;
      if (!cvar_snapshot(runtime, "ui_recordSPDemoName", &demo_name)) return false;
      if (!cvar_snapshot(runtime, "ui_recordSPDemo", &demo_flag)) {
        qa_cvar_read_free(&demo_name);
        return false;
      }
      if (demo_flag.integer_value != 0 && STR(demo_name.value)[0] != '\0') {
        char command[512];
        snprintf(command, sizeof command, "set g_synchronousclients 1 ; record %.400s \n",
            STR(demo_name.value));
        runtime->host.send_console_command(runtime->host.ctx, command);
      }
      qa_cvar_read_free(&demo_flag);
      qa_cvar_read_free(&demo_name);
    }
  }
  runtime->host.set_cvar(runtime->host.ctx, "cg_thirdPerson", "0");
  return true;
}

typedef struct reset_ctx { runtime_t *runtime; } reset_ctx;
static void reset_thunk(void *ctx, qa_q3_pres_client_entity *entity) {
  runtime_t *runtime = ((const reset_ctx *)ctx)->runtime;
  runtime->host.reset_player_entity(runtime->host.ctx, entity);
}
static bool load_deferred(runtime_t *runtime) {
  reset_ctx context = {runtime};
  if (!runtime->host.load_deferred_players(runtime->host.clients_ctx, reset_thunk, &context)) return false;
  open_check(runtime);
  return true;
}
static bool dispatch(runtime_t *runtime, const char *const *command, size_t count);

static bool voice_chat_local_impl(runtime_t *runtime, double mode, bool voice_only, double client_number,
    double color, const char *command);
static bool dispatch(runtime_t *runtime, const char *const *command, size_t count) {
  char name_buffer[1024];
  const char *name = argv_at(command, count, 0, name_buffer);
  qa_q3_pres_client_game_state *state = runtime->host.state;
  char arg1[1024];
  if (name[0] == '\0') return true;
  if (strcmp(name, "cp") == 0) {
    runtime->host.center_print(runtime->host.ctx, argv_at(command, count, 1, arg1),
        state->product == QA_Q3_BASE_PRODUCT_BASEQ3 ? 143 : 144, 16);
    return true;
  }
  if (strcmp(name, "cs") == 0) return config_modified(runtime, command, count);
  if (strcmp(name, "print") == 0) {
    const char *value = argv_at(command, count, 1, arg1);
    runtime->host.print(runtime->host.ctx, value);
    if (state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
      if (fold_starts_with(value, "vote failed") || fold_starts_with(value, "team vote failed")) {
        local_sound(runtime, QA_Q3_PRES_VOTE_FAILED);
      } else if (fold_starts_with(value, "vote passed") || fold_starts_with(value, "team vote passed")) {
        local_sound(runtime, QA_Q3_PRES_VOTE_PASSED);
      }
    }
    return true;
  }
  if (strcmp(name, "chat") == 0 || strcmp(name, "tchat") == 0) {
    bool team_chat = strcmp(name, "tchat") == 0;
    if (!team_chat) {
      double chats_only = 0;
      if (!cvar_integer(runtime, "cg_teamChatsOnly", &chats_only)) return false;
      if (chats_only != 0) return true;
    }
    local_sound_channel(runtime, QA_Q3_PRES_TALK_SOUND, 6);
    const char *raw = argv_at(command, count, 1, arg1);
    char value[160];
    size_t length = strlen(raw);
    if (length > 149) length = 149;
    size_t written = 0;
    for (size_t i = 0; i < length; i++) {
      if ((unsigned char)raw[i] != 0x19) value[written++] = raw[i];
    }
    value[written] = '\0';
    if (team_chat && !qa_q3_pres_client_server_command_runtime_add_to_team_chat(runtime, value)) return false;
    char line[192];
    snprintf(line, sizeof line, "%.160s\n", value);
    runtime->host.print(runtime->host.ctx, line);
    return true;
  }
  if (strcmp(name, "vchat") == 0 || strcmp(name, "vtchat") == 0 || strcmp(name, "vtell") == 0) {
    if (state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
      char id[1024];
      const char *voice_id = argv_at(command, count, 4, id);
      double no_taunt = 0;
      if (!cvar_integer(runtime, "cg_noTaunt", &no_taunt)) return false;
      const char *taunts[] = {"kill_insult", "taunt", "death_insult", "kill_gauntlet", "praise"};
      bool filtered = false;
      if (no_taunt != 0) {
        for (size_t i = 0; i < 5; i++) if (strcmp(voice_id, taunts[i]) == 0) filtered = true;
      }
      if (!filtered) {
        double mode = strcmp(name, "vchat") == 0 ? 0 : strcmp(name, "vtchat") == 0 ? 1 : 2;
        return voice_chat_local_impl(runtime, mode, argv_integer(command, count, 1) != 0,
            argv_integer(command, count, 2), argv_integer(command, count, 3), voice_id);
      }
    }
    return true;
  }
  if (strcmp(name, "scores") == 0) {
    parse_scores(runtime, command, count);
    return true;
  }
  if (strcmp(name, "tinfo") == 0) {
    parse_team_info(runtime, command, count);
    return true;
  }
  if (strcmp(name, "map_restart") == 0) return map_restart(runtime);
  if (strcmp(name, "loaddefered") == 0) return load_deferred(runtime);
  if (strcmp(name, "clientLevelShot") == 0) {
    state->level_shot = true;
    return true;
  }
  char remaining[1024];
  slice_copy(remaining, sizeof remaining, name, 1023);
  if (fold_equal(name, "remapshader") && count == 4) {
    /* Q3_VM argv pointers alias one static buffer: only argv(3) survives. */
    const char *last = argv_at(command, count, 3, arg1);
    slice_copy(remaining, sizeof remaining, last, 1023);
    runtime->host.remap_shader(runtime->host.ctx, remaining, remaining, remaining);
    open_check(runtime);
    if (strcmp(remaining, "loaddefered") == 0) return load_deferred(runtime);
    if (strcmp(remaining, "clientLevelShot") == 0) {
      state->level_shot = true;
      return true;
    }
  }
  char line[1100];
  snprintf(line, sizeof line, "Unknown client game command: %.1000s\n", remaining);
  runtime->host.print(runtime->host.ctx, line);
  return true;
}

bool qa_q3_pres_client_server_command_runtime_execute_new_server_commands(runtime_t *runtime,
    double latest_sequence) {
  if (latest_sequence != trunc(latest_sequence) || latest_sequence < 0 || latest_sequence > 2147483647.0) {
    qa_range_error("Invalid reliable command sequence");
  }
  open_check(runtime);
  while (runtime->host.static_state->server_command_sequence < latest_sequence) {
    double sequence = runtime->host.static_state->server_command_sequence + 1;
    runtime->host.static_state->server_command_sequence = sequence;
    char **tokens = NULL;
    size_t token_count = 0;
    bool found = runtime->host.get_server_command(runtime->host.ctx, sequence, &tokens, &token_count);
    open_check(runtime);
    if (found) {
      char **owned = NULL;
      bool copied = command_copy((const char *const *)tokens, token_count, &owned);
      free_tokens(tokens, token_count);
      if (!copied) return false;
      bool ok = dispatch(runtime, (const char *const *)owned, token_count);
      free_tokens(owned, token_count);
      if (!ok) return false;
    }
  }
  return true;
}
bool qa_q3_pres_client_server_command_runtime_execute_command(runtime_t *runtime,
    const char *const *command, size_t command_count) {
  char **owned = NULL;
  if (!command_copy(command, command_count, &owned)) return false;
  open_check(runtime);
  bool ok = dispatch(runtime, (const char *const *)owned, command_count);
  free_tokens(owned, command_count);
  return ok;
}

/* Voice file text: caller frees. *out NULL is the TS null return. */
typedef enum voice_file_status { VOICE_FILE_OK, VOICE_FILE_MISSING, VOICE_FILE_OOM } voice_file_status;
static voice_file_status voice_file(runtime_t *runtime, const char *filename, bool missing_warning,
    char **out) {
  open_check(runtime);
  *out = NULL;
  if (!runtime->host.assets.base.has(runtime->host.assets.base.ctx, filename)) {
    if (missing_warning) {
      char message[320];
      snprintf(message, sizeof message, "^1voice chat file not found: %.250s\n", filename);
      runtime->host.print(runtime->host.ctx, message);
    }
    return VOICE_FILE_MISSING;
  }
  uint8_t *data = NULL;
  size_t length = 0;
  if (!runtime->host.assets.base.read(runtime->host.assets.base.ctx, filename, &data, &length)) {
    return VOICE_FILE_OOM;
  }
  open_check(runtime);
  if (length >= 16384) {
    char message[384];
    snprintf(message, sizeof message, "^1voice chat file too large: %.250s is %zu, max allowed is 16384",
        filename, length);
    runtime->host.print(runtime->host.ctx, message);
    free(data);
    return VOICE_FILE_MISSING;
  }
  char *text = malloc(length + 1);
  if (text == NULL) {
    free(data);
    return VOICE_FILE_OOM;
  }
  memcpy(text, data, length);
  text[length] = '\0';
  free(data);
  *out = text;
  return VOICE_FILE_OK;
}

bool qa_q3_pres_client_server_command_runtime_parse_voice_chats(runtime_t *runtime, const char *filename,
    double list_index, double maximum_chats) {
  open_check(runtime);
  if (maximum_chats != trunc(maximum_chats) || maximum_chats < 1 || maximum_chats > 64) {
    qa_range_error("Invalid voice chat count");
  }
  double compressed_value = 0;
  if (!cvar_integer(runtime, "com_buildScript", &compressed_value)) return false;
  bool compressed = compressed_value == 0;
  at_check(list_index, 8);
  voice_chat_list *list = &runtime->voice_lists[(size_t)list_index];
  char *text = NULL;
  voice_file_status status = voice_file(runtime, filename, true, &text);
  if (status == VOICE_FILE_OOM) return false;
  if (status == VOICE_FILE_MISSING) return false;
  slice_copy(list->name, sizeof list->name, filename, 63);
  for (int32_t i = 0; i < maximum_chats; i++) list->voice_chats[i].id[0] = '\0';
  byte_tokenizer tokenizer = {text, strlen(text), 0};
  char token[1024];
  tokenizer_next(&tokenizer, true, token, sizeof token);
  if (token[0] == '\0') {
    free(text);
    return true;
  }
  qa_q3_player_gender gender;
  if (fold_equal(token, "male")) gender = QA_Q3_GENDER_MALE;
  else if (fold_equal(token, "female")) gender = QA_Q3_GENDER_FEMALE;
  else if (fold_equal(token, "neuter")) gender = QA_Q3_GENDER_NEUTER;
  else {
    char message[384];
    snprintf(message, sizeof message, "^1expected gender not found in voice chat file: %.250s\n", filename);
    runtime->host.print(runtime->host.ctx, message);
    free(text);
    return false;
  }
  list->gender = gender;
  list->num_voice_chats = 0;
  bool result = true;
  for (;;) {
    tokenizer_next(&tokenizer, true, token, sizeof token);
    if (token[0] == '\0') break;
    at_check(list->num_voice_chats, 64);
    voice_chat *chat = &list->voice_chats[(size_t)list->num_voice_chats];
    slice_copy(chat->id, sizeof chat->id, token, 63);
    tokenizer_next(&tokenizer, true, token, sizeof token);
    if (strcmp(token, "{") != 0) {
      char message[512];
      snprintf(message, sizeof message, "^1expected { found %.200s in voice chat file: %.250s\n",
          token, filename);
      runtime->host.print(runtime->host.ctx, message);
      result = false;
      break;
    }
    chat->num_sounds = 0;
    bool done = false;
    for (;;) {
      char path[1024];
      tokenizer_next(&tokenizer, true, path, sizeof path);
      if (path[0] == '\0') { done = true; break; }
      if (strcmp(path, "}") == 0) break;
      qa_pcm_sound *sound = NULL;
      if (!runtime->host.register_sound(runtime->host.ctx, path, compressed, &sound)) {
        free(text);
        return false;
      }
      open_check(runtime);
      at_check(chat->num_sounds, 64);
      chat->sounds[(size_t)chat->num_sounds] = sound;
      char message[1024];
      tokenizer_next(&tokenizer, true, message, sizeof message);
      if (message[0] == '\0') { done = true; break; }
      slice_copy(chat->chats[(size_t)chat->num_sounds], 64, message, 63);
      if (sound != NULL) chat->num_sounds++;
      if (chat->num_sounds >= 64) break;
    }
    if (done) break;
    list->num_voice_chats++;
    if (list->num_voice_chats >= maximum_chats) break;
  }
  free(text);
  return result;
}

bool qa_q3_pres_client_server_command_runtime_load_voice_chats(runtime_t *runtime) {
  open_check(runtime);
  double before = runtime->host.memory_remaining(runtime->host.ctx);
  const char *files[] = {"female1", "female2", "female3", "male1", "male2", "male3", "male4", "male5"};
  for (int i = 0; i < 8; i++) {
    char filename[80];
    snprintf(filename, sizeof filename, "scripts/%.40s.voice", files[i]);
    (void)qa_q3_pres_client_server_command_runtime_parse_voice_chats(runtime, filename, i, 64);
  }
  char message[96];
  double used = before - runtime->host.memory_remaining(runtime->host.ctx);
  if (used == trunc(used) && fabs(used) < 1e21) {
    snprintf(message, sizeof message, "voice chat memory size = %lld\n", (long long)used);
  } else {
    snprintf(message, sizeof message, "voice chat memory size = %s\n", QA_JS_NUMBER(used));
  }
  runtime->host.print(runtime->host.ctx, message);
  return true;
}

static int head_model_voice_chats(runtime_t *runtime, const char *filename) {
  if (!runtime->host.assets.base.has(runtime->host.assets.base.ctx, filename)) return -1;
  uint8_t *data = NULL;
  size_t length = 0;
  if (!runtime->host.assets.read_sync(runtime->host.assets.base.ctx, filename, &data, &length)) return -1;
  if (length >= 16384) {
    char message[384];
    snprintf(message, sizeof message, "^1voice chat file too large: %.250s is %zu, max allowed is 16384",
        filename, length);
    runtime->host.print(runtime->host.ctx, message);
    free(data);
    return -1;
  }
  char *text = malloc(length + 1);
  if (text == NULL) {
    free(data);
    return -1;
  }
  memcpy(text, data, length);
  text[length] = '\0';
  free(data);
  byte_tokenizer tokenizer = {text, length, 0};
  char token[1024];
  tokenizer_next(&tokenizer, true, token, sizeof token);
  free(text);
  if (token[0] == '\0') return -1;
  for (int i = 0; i < 8; i++) {
    if (fold_equal(runtime->voice_lists[i].name, token)) return i;
  }
  return -1;
}
static voice_chat_list *voice_list_at(runtime_t *runtime, double index) {
  at_check(index, 8);
  return &runtime->voice_lists[(size_t)index];
}
static voice_chat_list *voice_list_for_client(runtime_t *runtime, double client_num) {
  qa_q3_pres_client_info *info =
      runtime->host.client_info(runtime->host.clients_ctx, client_num < 0 || client_num >= 64 ? 0 : client_num);
  const char *model = STR(info->head_model_name);
  if (model[0] == '*') model++;
  char candidates[2][160];
  snprintf(candidates[0], sizeof candidates[0], "%.80s/%.80s", model, STR(info->head_skin_name));
  snprintf(candidates[1], sizeof candidates[1], "%.120s", model);
  char head[64] = "";
  for (int candidate_index = 0; candidate_index < 2; candidate_index++) {
    slice_copy(head, sizeof head, candidates[candidate_index], 63);
    for (int i = 0; i < 64; i++) {
      if (runtime->head_voices[i].headmodel[0] != '\0' &&
          fold_equal(runtime->head_voices[i].headmodel, head)) {
        return voice_list_at(runtime, runtime->head_voices[i].voice_chat_num);
      }
    }
    int free_index = -1;
    for (int i = 0; i < 64; i++) if (runtime->head_voices[i].headmodel[0] == '\0') { free_index = i; break; }
    if (free_index >= 0) {
      char filename[96];
      char combined[96];
      snprintf(combined, sizeof combined, "scripts/%.60s.vc", head);
      slice_copy(filename, sizeof filename, combined, 63);
      int index = head_model_voice_chats(runtime, filename);
      if (index >= 0) {
        slice_copy(runtime->head_voices[free_index].headmodel, 64, head, 63);
        runtime->head_voices[free_index].voice_chat_num = index;
        return voice_list_at(runtime, index);
      }
    }
  }
  const qa_q3_player_gender genders[2] = {info->gender, QA_Q3_GENDER_MALE};
  int gender_count = info->gender == QA_Q3_GENDER_MALE ? 1 : 2;
  for (int g = 0; g < gender_count; g++) {
    for (int i = 0; i < 8; i++) {
      if (runtime->voice_lists[i].name[0] != '\0' && runtime->voice_lists[i].gender == genders[g]) {
        for (int f = 0; f < 64; f++) {
          if (runtime->head_voices[f].headmodel[0] == '\0') {
            slice_copy(runtime->head_voices[f].headmodel, 64, head, 63);
            runtime->head_voices[f].voice_chat_num = i;
            break;
          }
        }
        return voice_list_at(runtime, i);
      }
    }
  }
  for (int f = 0; f < 64; f++) {
    if (runtime->head_voices[f].headmodel[0] == '\0') {
      slice_copy(runtime->head_voices[f].headmodel, 64, head, 63);
      runtime->head_voices[f].voice_chat_num = 0;
      break;
    }
  }
  return voice_list_at(runtime, 0);
}

static bool play_voice_chat(runtime_t *runtime, buffered_voice *voice) {
  qa_q3_pres_client_game_state *state = runtime->host.state;
  qa_q3_pres_client_game_static_state *cgs = runtime->host.static_state;
  if (state->intermission_started) return true;
  double no_voice_chats = 0;
  if (!cvar_integer(runtime, "cg_noVoiceChats", &no_voice_chats)) return false;
  if (no_voice_chats == 0) {
    runtime->host.start_local_sound(runtime->host.ctx, voice->snd, 3);
    if (state->snap == NULL) qa_range_error("Voice playback requires the current snapshot");
    if (voice->client_num != state->snap->player_state.client_num) {
      double order = order_task(STR(voice->cmd));
      if (order > 0) {
        cgs->accept_order_time = to_int32(state->time + 5000);
        if (!qa_q3_pres_set_string(&cgs->accept_voice, "")) return false;
        char sliced[32];
        slice_copy(sliced, sizeof sliced, STR(voice->cmd), 31);
        if (!qa_q3_pres_set_string(&cgs->accept_voice, sliced)) return false;
        cgs->accept_task = order;
        cgs->accept_leader = voice->client_num;
      }
      runtime->host.show_response_head(runtime->host.ctx);
      open_check(runtime);
    }
  }
  double no_voice_text = 0;
  if (!cvar_integer(runtime, "cg_noVoiceText", &no_voice_text)) return false;
  if (!voice->voice_only && no_voice_text == 0) {
    if (!qa_q3_pres_client_server_command_runtime_add_to_team_chat(runtime, STR(voice->message))) return false;
    char line[192];
    snprintf(line, sizeof line, "%.170s\n", STR(voice->message));
    runtime->host.print(runtime->host.ctx, line);
  }
  at_check(state->voice_chat_buffer_out, 32);
  runtime->voices[(size_t)state->voice_chat_buffer_out].snd = NULL;
  return true;
}
static bool add_buffered_voice_chat(runtime_t *runtime, buffered_voice voice) {
  qa_q3_pres_client_game_state *state = runtime->host.state;
  if (state->intermission_started) {
    free(voice.cmd);
    free(voice.message);
    return true;
  }
  at_check(state->voice_chat_buffer_in, 32);
  buffered_voice *slot = &runtime->voices[(size_t)state->voice_chat_buffer_in];
  free(slot->cmd);
  free(slot->message);
  *slot = voice;
  state->voice_chat_buffer_in = fmod(state->voice_chat_buffer_in + 1, 32);
  if (state->voice_chat_buffer_in == state->voice_chat_buffer_out) {
    at_check(state->voice_chat_buffer_out, 32);
    if (!play_voice_chat(runtime, &runtime->voices[(size_t)state->voice_chat_buffer_out])) return false;
    state->voice_chat_buffer_out = fmod(state->voice_chat_buffer_out + 1, 32);
  }
  return true;
}

static bool voice_chat_local_impl(runtime_t *runtime, double mode, bool voice_only, double client_number,
    double color, const char *command) {
  open_check(runtime);
  qa_q3_pres_client_game_state *state = runtime->host.state;
  if (state->product != QA_Q3_BASE_PRODUCT_MISSIONPACK || state->intermission_started) return true;
  double client_num = client_number < 0 || client_number >= 64 ? 0 : client_number;
  qa_q3_pres_client_info *info = runtime->host.client_info(runtime->host.clients_ctx, client_num);
  runtime->host.static_state->current_voice_client = client_num;
  voice_chat_list *list = voice_list_for_client(runtime, client_num);
  voice_chat *chat = NULL;
  int32_t chat_count = to_int32(list->num_voice_chats);
  if (chat_count > 64) chat_count = 64;
  if (chat_count < 0) chat_count = 0;
  for (int32_t i = 0; i < chat_count; i++) {
    if (fold_equal(list->voice_chats[i].id, command)) {
      chat = &list->voice_chats[i];
      break;
    }
  }
  if (chat == NULL) return true;
  double index = trunc((double)(float)((double)qa_game_random_random(runtime->host.random) * chat->num_sounds));
  at_check(index, 64);
  qa_pcm_sound *sound = chat->sounds[(size_t)index];
  const char *message = chat->chats[(size_t)index];
  if (mode != 1) {
    double chats_only = 0;
    if (!cvar_integer(runtime, "cg_teamChatsOnly", &chats_only)) return false;
    if (chats_only != 0) return true;
  }
  char name[160];
  if (mode == 2) snprintf(name, sizeof name, "[%.120s]", STR(info->name));
  else if (mode == 1) snprintf(name, sizeof name, "(%.120s)", STR(info->name));
  else snprintf(name, sizeof name, "%.120s", STR(info->name));
  char full_message[256];
  char color_char = (char)(to_int32(color) & 255);
  snprintf(full_message, sizeof full_message, "%.150s: ^%c%.80s", name,
      color_char != '\0' ? color_char : ' ', message);
  buffered_voice voice;
  memset(&voice, 0, sizeof voice);
  voice.client_num = client_num;
  voice.snd = sound;
  voice.voice_only = voice_only;
  char cmd_buffer[160];
  slice_copy(cmd_buffer, sizeof cmd_buffer, command, 149);
  voice.cmd = xstrdup(cmd_buffer);
  char message_buffer[160];
  slice_copy(message_buffer, sizeof message_buffer, full_message, 149);
  voice.message = xstrdup(message_buffer);
  if (voice.cmd == NULL || voice.message == NULL) {
    free(voice.cmd);
    free(voice.message);
    return false;
  }
  return add_buffered_voice_chat(runtime, voice);
}
bool qa_q3_pres_client_server_command_runtime_voice_chat_local(runtime_t *runtime, double mode,
    bool voice_only, double client_number, double color, const char *command) {
  return voice_chat_local_impl(runtime, mode, voice_only, client_number, color, command);
}
bool qa_q3_pres_client_server_command_runtime_play_buffered_voice_chats(runtime_t *runtime) {
  open_check(runtime);
  qa_q3_pres_client_game_state *state = runtime->host.state;
  if (state->product != QA_Q3_BASE_PRODUCT_MISSIONPACK || state->voice_chat_time >= state->time) return true;
  if (state->voice_chat_buffer_out != state->voice_chat_buffer_in) {
    at_check(state->voice_chat_buffer_out, 32);
    if (runtime->voices[(size_t)state->voice_chat_buffer_out].snd != NULL) {
      if (!play_voice_chat(runtime, &runtime->voices[(size_t)state->voice_chat_buffer_out])) return false;
      state->voice_chat_buffer_out = fmod(state->voice_chat_buffer_out + 1, 32);
      state->voice_chat_time = to_int32(state->time + 1000);
    }
  }
  return true;
}
