/* Port of src/content/q1/base/player.ts. GPL-2.0-or-later. */
#include "qa/content/q1/base/player.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/base/map_entities.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

#define V3(x, y, z) ((qa_vec3){(x), (y), (z)})

typedef struct animation { double first; double count; const char *end; } animation;
static const animation DEATHS[] = {
  {50, 11, "dead"}, {61, 9, "dead"}, {70, 15, "dead"},
  {85, 9, "dead"}, {94, 9, "dead"}, {94, 9, "dead"}
};

struct qa_q1_character_actor {
  qa_q1b_game *game;
  qa_owned_actor actor;
  qa_q1_character_options options;
  qa_q1_character_input input;
  const char *life;
  char model[128];
  double model_frame;
  bool has_animation; animation animation;
  double animation_frame;
  bool attack_animation;
  double walk_frame;
  const char *locomotion; /* NULL | "stand" | "run" */
  double next_animation;
  double pain_until;
  qa_vec3 view_offset;
  double last_fall_speed;
  double air_finished;
  double drown_damage;
  double hazard_at;
  bool in_water;
};

/* TS WeakMap of per-game character tables. */
typedef struct char_entry { qa_owned_actor actor; qa_q1_character_actor *character; } char_entry;
typedef struct char_table {
  qa_q1b_game *game;
  char_entry *entries; size_t count; size_t capacity;
} char_table;
static char_table TABLES[32];
static size_t TABLE_COUNT;
static void table_on_release(void *ctx, qa_owned_actor actor) {
  char_table *table = ctx;
  for (size_t i = 0; i < table->count; i++) {
    if (qa_q1b_owned_equal(table->entries[i].actor, actor)) {
      memmove(&table->entries[i], &table->entries[i + 1], (table->count - i - 1) * sizeof *table->entries);
      table->count--;
      return;
    }
  }
}
static char_table *character_table(qa_q1b_game *game) {
  for (size_t i = 0; i < TABLE_COUNT; i++) if (TABLES[i].game == game) return &TABLES[i];
  if (TABLE_COUNT >= 32) qa_range_error("out of memory");
  char_table *table = &TABLES[TABLE_COUNT++];
  memset(table, 0, sizeof *table);
  table->game = game;
  game->host->actors_on_release(game->host->ctx, table_on_release, table);
  return table;
}
static qa_q1_character_actor *table_get(qa_q1b_game *game, qa_owned_actor actor) {
  char_table *table = character_table(game);
  for (size_t i = 0; i < table->count; i++)
    if (qa_q1b_owned_equal(table->entries[i].actor, actor)) return table->entries[i].character;
  return NULL;
}
static void table_set(qa_q1b_game *game, qa_owned_actor actor, qa_q1_character_actor *character) {
  char_table *table = character_table(game);
  for (size_t i = 0; i < table->count; i++) {
    if (qa_q1b_owned_equal(table->entries[i].actor, actor)) { table->entries[i].character = character; return; }
  }
  if (table->count == table->capacity) {
    size_t capacity = table->capacity == 0 ? 8 : table->capacity * 2;
    char_entry *grown = realloc(table->entries, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    table->entries = grown;
    table->capacity = capacity;
  }
  table->entries[table->count].actor = actor;
  table->entries[table->count].character = character;
  table->count++;
}

static void death_bubbles_action(void *ctx, qa_q1b_game *game, qa_q1b_actor *timer) {
  (void)ctx;
  qa_owned_actor owner;
  bool have_owner = timer->owner.has &&
                    game->host->actors_resolve_owned(game->host->ctx, timer->owner.id, &owner);
  if (!have_owner) { game->remove(game, timer); return; }
  qa_q1_character_actor *character = table_get(game, owner);
  double level;
  if (character != NULL) level = character->input.water_level;
  else {
    qa_q1b_player_state *player = game->player(game, owner.id);
    level = player != NULL ? player->water_level : 0;
  }
  if (level != 3) return;
  qa_q1b_body body;
  if (!game->host->bodies_read(game->host->ctx, owner.id, &body)) { game->remove(game, timer); return; }
  (void)qa_q1_spawn_bubble(game, qa_add3(body.origin, V3(0, 0, 24)), NULL, false);
  timer->count--;
  if (timer->count <= 0) game->remove(game, timer);
  else game->schedule(game, timer, 0.1, game->named.action(game->named.ctx, timer, "base:death_bubbles"));
}
void qa_q1_register_character_callbacks(qa_q1b_game *game) {
  qa_q1b_handlers handlers = {.action = death_bubbles_action};
  game->named.register_named(game->named.ctx, "base:death_bubbles", &handlers);
}

static const qa_q1_character_input DEFAULT_INPUT = {false, false, false, false, 0, "empty", false, false};
static void copy_text(char *dst, size_t size, const char *src) {
  size_t length = strlen(src);
  if (length >= size) length = size - 1;
  memcpy(dst, src, length);
  dst[length] = '\0';
}
qa_q1_character_actor *qa_q1_character_actor_create(qa_q1b_game *game, qa_owned_actor actor,
                                                    const qa_q1_character_options *options) {
  qa_q1_character_actor *character = malloc(sizeof *character);
  if (character == NULL) return NULL;
  memset(character, 0, sizeof *character);
  character->game = game;
  character->actor = actor;
  if (options != NULL) character->options = *options;
  character->input = DEFAULT_INPUT;
  character->life = "alive";
  copy_text(character->model, sizeof character->model, "progs/player.mdl");
  character->model_frame = 12;
  character->view_offset = V3(0, 0, 22);
  character->air_finished = 12;
  character->drown_damage = 2;
  game->host->actors_assert_owned(game->host->ctx, actor);
  character->air_finished = game->time + 12;
  table_set(game, actor, character);
  return character;
}
void qa_q1_character_actor_destroy(qa_q1_character_actor *character) { free(character); }
int qa_q1_character_actor_water_level(const qa_q1_character_actor *character) {
  return character->input.water_level;
}
const char *qa_q1_character_actor_life(const qa_q1_character_actor *character) { return character->life; }
qa_vec3 qa_q1_character_actor_view_offset(const qa_q1_character_actor *character) {
  return character->view_offset;
}

static bool set_owned(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_set(object, key, child)) { qa_save_value_free(child); return false; }
  return true;
}
static qa_save_value *vector_value(qa_vec3 vector) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  bool ok = set_owned(object, "x", qa_save_value_number(vector.x));
  ok = ok && set_owned(object, "y", qa_save_value_number(vector.y));
  ok = ok && set_owned(object, "z", qa_save_value_number(vector.z));
  if (!ok) { qa_save_value_free(object); return NULL; }
  return object;
}
uint8_t *qa_q1_character_actor_capture(qa_q1_character_actor *character, size_t *out_length) {
  qa_save_value *object = qa_save_value_object();
  qa_save_value *input = qa_save_value_object();
  if (object == NULL || input == NULL) { qa_save_value_free(object); qa_save_value_free(input); return NULL; }
  bool ok = set_owned(input, "axePose", qa_save_value_boolean(character->input.axe_pose));
  ok = ok && set_owned(input, "attack", qa_save_value_boolean(character->input.attack));
  ok = ok && set_owned(input, "jump", qa_save_value_boolean(character->input.jump));
  ok = ok && set_owned(input, "use", qa_save_value_boolean(character->input.use));
  ok = ok && set_owned(input, "waterLevel", qa_save_value_number(character->input.water_level));
  ok = ok && set_owned(input, "waterType", qa_save_value_string(character->input.water_type));
  ok = ok && set_owned(input, "invisible", qa_save_value_boolean(character->input.invisible));
  ok = ok && set_owned(input, "invulnerable", qa_save_value_boolean(character->input.invulnerable));
  qa_save_value *animation_value = qa_save_value_null();
  if (character->has_animation) {
    animation_value = qa_save_value_object();
    ok = ok && animation_value != NULL;
    ok = ok && set_owned(animation_value, "first", qa_save_value_number(character->animation.first));
    ok = ok && set_owned(animation_value, "count", qa_save_value_number(character->animation.count));
    ok = ok && set_owned(animation_value, "end", qa_save_value_string(character->animation.end));
  }
  qa_save_value *locomotion = character->locomotion != NULL
                                  ? qa_save_value_string(character->locomotion) : qa_save_value_null();
  ok = ok && set_owned(object, "version", qa_save_value_number(3));
  ok = ok && set_owned(object, "input", input);
  ok = ok && set_owned(object, "life", qa_save_value_string(character->life));
  ok = ok && set_owned(object, "model", qa_save_value_string(character->model));
  ok = ok && set_owned(object, "modelFrame", qa_save_value_number(character->model_frame));
  ok = ok && set_owned(object, "animation", animation_value);
  ok = ok && set_owned(object, "animationFrame", qa_save_value_number(character->animation_frame));
  ok = ok && set_owned(object, "attackAnimation", qa_save_value_boolean(character->attack_animation));
  ok = ok && set_owned(object, "walkFrame", qa_save_value_number(character->walk_frame));
  ok = ok && set_owned(object, "locomotion", locomotion);
  ok = ok && set_owned(object, "nextAnimation", qa_save_value_number(character->next_animation));
  ok = ok && set_owned(object, "painUntil", qa_save_value_number(character->pain_until));
  ok = ok && set_owned(object, "viewOffset", vector_value(character->view_offset));
  ok = ok && set_owned(object, "lastFallSpeed", qa_save_value_number(character->last_fall_speed));
  ok = ok && set_owned(object, "airFinished", qa_save_value_number(character->air_finished));
  ok = ok && set_owned(object, "drownDamage", qa_save_value_number(character->drown_damage));
  ok = ok && set_owned(object, "hazardAt", qa_save_value_number(character->hazard_at));
  ok = ok && set_owned(object, "inWater", qa_save_value_boolean(character->in_water));
  if (!ok) { qa_save_value_free(object); return NULL; }
  uint8_t *bytes = qa_encode_checkpoint_value(object, out_length);
  qa_save_value_free(object);
  return bytes;
}
static void must_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("out of memory");
}
static bool field_boolean(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  must_field(reader, name, &field);
  bool value = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static double field_number(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  must_field(reader, name, &field);
  double value = qa_save_reader_number(&field);
  qa_save_reader_destroy(&field);
  return value;
}
void qa_q1_character_actor_restore(qa_q1_character_actor *character, const uint8_t *bytes, size_t length) {
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, decoded, "q1:character")) qa_range_error("out of memory");
  qa_save_reader field, input;
  must_field(&reader, "version", &field);
  static const double VERSIONS[] = {1, 2, 3};
  double version = qa_save_reader_choice_number(&field, VERSIONS, 3);
  qa_save_reader_destroy(&field);
  must_field(&reader, "input", &input);
  character->input.axe_pose = field_boolean(&input, "axePose");
  character->input.attack = field_boolean(&input, "attack");
  character->input.jump = field_boolean(&input, "jump");
  character->input.use = field_boolean(&input, "use");
  must_field(&input, "waterLevel", &field);
  static const double LEVELS[] = {0, 1, 2, 3};
  character->input.water_level = (int)qa_save_reader_choice_number(&field, LEVELS, 4);
  qa_save_reader_destroy(&field);
  must_field(&input, "waterType", &field);
  static const char *const WATER[] = {"empty", "water", "slime", "lava"};
  character->input.water_type = qa_save_reader_choice_string(&field, WATER, 4);
  qa_save_reader_destroy(&field);
  character->input.invisible = field_boolean(&input, "invisible");
  character->input.invulnerable = field_boolean(&input, "invulnerable");
  qa_save_reader_destroy(&input);
  must_field(&reader, "life", &field);
  static const char *const LIVES[] = {"alive", "dying", "dead", "respawnable"};
  character->life = qa_save_reader_choice_string(&field, LIVES, 4);
  qa_save_reader_destroy(&field);
  must_field(&reader, "model", &field);
  copy_text(character->model, sizeof character->model, qa_save_reader_string(&field));
  qa_save_reader_destroy(&field);
  character->model_frame = field_number(&reader, "modelFrame");
  must_field(&reader, "animation", &field);
  if (qa_save_reader_is_null(&field)) {
    character->has_animation = false;
  } else {
    qa_save_reader inner;
    character->has_animation = true;
    must_field(&field, "first", &inner);
    character->animation.first = qa_save_reader_integer_min(&inner, 0);
    qa_save_reader_destroy(&inner);
    must_field(&field, "count", &inner);
    character->animation.count = qa_save_reader_integer_min(&inner, 1);
    qa_save_reader_destroy(&inner);
    must_field(&field, "end", &inner);
    static const char *const ENDS[] = {"locomotion", "dead"};
    character->animation.end = qa_save_reader_choice_string(&inner, ENDS, 2);
    qa_save_reader_destroy(&inner);
  }
  qa_save_reader_destroy(&field);
  character->animation_frame = field_number(&reader, "animationFrame");
  character->attack_animation = field_boolean(&reader, "attackAnimation");
  character->walk_frame = field_number(&reader, "walkFrame");
  if (version == 1) {
    qa_q1b_body body;
    bool have = character->game->host->bodies_read(character->game->host->ctx, character->actor.id, &body);
    character->locomotion = !have ? NULL : (body.velocity.x != 0 || body.velocity.y != 0 ? "run" : "stand");
  } else {
    must_field(&reader, "locomotion", &field);
    if (qa_save_reader_is_null(&field)) character->locomotion = NULL;
    else {
      static const char *const MODES[] = {"stand", "run"};
      character->locomotion = qa_save_reader_choice_string(&field, MODES, 2);
    }
    qa_save_reader_destroy(&field);
  }
  character->next_animation = field_number(&reader, "nextAnimation");
  character->pain_until = field_number(&reader, "painUntil");
  must_field(&reader, "viewOffset", &field);
  character->view_offset = V3(field_number(&field, "x"), field_number(&field, "y"), field_number(&field, "z"));
  qa_save_reader_destroy(&field);
  character->last_fall_speed = field_number(&reader, "lastFallSpeed");
  character->air_finished = field_number(&reader, "airFinished");
  character->drown_damage = field_number(&reader, "drownDamage");
  character->hazard_at = field_number(&reader, "hazardAt");
  character->in_water = field_boolean(&reader, "inWater");
  qa_save_reader_destroy(&reader);
  qa_save_value_free(decoded);
}

static bool get_pose(qa_q1_character_actor *character, qa_q1_character_source_pose *out) {
  memset(out, 0, sizeof *out);
  if (character->options.source_pose == NULL) return false;
  return character->options.source_pose(character->options.ctx, out);
}
static bool presentation_only(qa_q1_character_actor *character) {
  return character->options.has_presentation_only && character->options.presentation_only;
}
qa_q1_character_presentation qa_q1_character_actor_presentation(qa_q1_character_actor *character) {
  qa_q1_character_source_pose pose;
  bool have_pose = strcmp(character->life, "alive") == 0 && get_pose(character, &pose);
  bool alive = strcmp(character->life, "alive") == 0;
  qa_q1_character_presentation result = {0};
  const char *model = !alive ? character->model
                      : character->input.invisible ? "progs/eyes.mdl"
                      : have_pose && pose.definition != NULL ? pose.definition->model : "progs/player.mdl";
  copy_text(result.model, sizeof result.model, model);
  result.frame = !alive ? character->model_frame
                 : character->input.invisible ? 0
                 : have_pose && pose.has_frame ? pose.frame : character->model_frame;
  result.view_offset = character->view_offset;
  result.life = character->life;
  result.solid = alive ? "slidebox" : "none";
  result.movement = alive ? "walk" : strcmp(character->model, "progs/h_player.mdl") == 0 ? "bounce" : "toss";
  result.weapon_visible = alive;
  return result;
}
static void character_sound(qa_q1_character_actor *character, const char *path, double attenuation) {
  if (presentation_only(character)) return;
  character->game->sound_owned(character->game, character->actor, path,
                               qa_q1b_channel_named("voice"), attenuation, 1);
}
static void animate(qa_q1_character_actor *character, animation anim, bool attack) {
  character->has_animation = true;
  character->animation = anim;
  character->attack_animation = attack;
  character->animation_frame = 0;
  character->model_frame = anim.first;
  character->next_animation = character->game->time + 0.1;
}
static void select_model(qa_q1_character_actor *character, bool have_pose,
                         const qa_q1_character_source_pose *pose) {
  if (strcmp(character->life, "alive") != 0) return;
  const char *model = have_pose && pose->definition != NULL ? pose->definition->model : "progs/player.mdl";
  if (strcmp(character->model, model) == 0) return;
  copy_text(character->model, sizeof character->model, model);
  character->has_animation = false;
  character->attack_animation = false;
  character->walk_frame = 0;
  character->locomotion = NULL;
  bool axe_pose = have_pose && pose->has_axe_pose ? pose->axe_pose : character->input.axe_pose;
  character->model_frame = have_pose && pose->definition != NULL ? pose->definition->stand.first
                           : axe_pose ? 17 : 12;
  character->next_animation = character->game->time;
}
qa_q1_character_presentation qa_q1_character_actor_frame(qa_q1_character_actor *character,
                                                         double seconds, qa_q1_character_input incoming) {
  qa_q1b_game *game = character->game;
  qa_q1b_game_set_time(game, seconds);
  qa_q1_character_source_pose pose;
  bool have_pose = get_pose(character, &pose);
  qa_q1_character_input input = incoming;
  if (have_pose && pose.has_axe_pose) input.axe_pose = pose.axe_pose;
  character->input = input;
  select_model(character, have_pose, &pose);
  qa_q1b_body body;
  bool have_body = game->host->bodies_read(game->host->ctx, character->actor.id, &body);
  bool source_attack = strcmp(character->life, "alive") == 0 && have_pose && pose.has_frame;
  if (source_attack) { character->has_animation = false; character->attack_animation = false; }
  bool deadish = strcmp(character->life, "dead") == 0 || strcmp(character->life, "respawnable") == 0;
  if (!presentation_only(character) && deadish && have_body && body.ground.has) {
    double length = qa_length3(body.velocity);
    double speed = length - 20;
    if (speed < 0) speed = 0;
    body.velocity = qa_scale3(qa_q1b_normalize(body.velocity), speed);
    game->host->bodies_write(game->host->ctx, character->actor, &body);
  }
  if (seconds >= character->next_animation && !source_attack) {
    character->next_animation = seconds + 0.1;
    if (character->has_animation) {
      animation anim = character->animation;
      character->animation_frame++;
      if (character->animation_frame < anim.count) {
        character->model_frame = anim.first + character->animation_frame;
      } else {
        character->has_animation = false;
        character->attack_animation = false;
        if (strcmp(anim.end, "dead") == 0) character->life = "dead";
      }
    }
    if (!character->has_animation && strcmp(character->life, "alive") == 0) {
      bool running = have_body && (body.velocity.x != 0 || body.velocity.y != 0);
      const char *locomotion = running ? "run" : "stand";
      if (character->locomotion == NULL || strcmp(character->locomotion, locomotion) != 0)
        character->walk_frame = 0;
      character->locomotion = locomotion;
      qa_q1_character_frame_range frames;
      if (have_pose && pose.definition != NULL) {
        frames = running ? pose.definition->run : pose.definition->stand;
      } else {
        frames.first = running ? (input.axe_pose ? 0 : 6) : (input.axe_pose ? 17 : 12);
        frames.count = running ? 6 : (input.axe_pose ? 12 : 5);
      }
      character->walk_frame = fmod(character->walk_frame, frames.count);
      character->model_frame = frames.first + character->walk_frame;
      character->walk_frame += 1;
    }
  }
  bool pressed = input.attack || input.jump || input.use;
  if (strcmp(character->life, "dead") == 0 && !pressed) character->life = "respawnable";
  else if (strcmp(character->life, "respawnable") == 0 && pressed) {
    if (character->options.request_respawn != NULL)
      character->options.request_respawn(character->options.ctx);
  }
  return qa_q1_character_actor_presentation(character);
}
void qa_q1_character_actor_attack(qa_q1_character_actor *character, qa_q1_base_character_attack attack) {
  if (strcmp(character->life, "alive") != 0) return;
  qa_q1_character_source_pose pose;
  bool have_pose = get_pose(character, &pose);
  select_model(character, have_pose, &pose);
  if ((have_pose && pose.definition != NULL) || (have_pose && pose.has_frame)) {
    character->has_animation = false;
    character->attack_animation = false;
    return;
  }
  if (strcmp(attack.kind, "axe") == 0) {
    animation anim = {119 + attack.variant * 6, 6, "locomotion"};
    animate(character, anim, true);
    return;
  }
  double first = strcmp(attack.kind, "shotgun") == 0 ? 113 : strcmp(attack.kind, "rocket") == 0 ? 107
                 : strcmp(attack.kind, "nail") == 0 ? 103 : 105;
  double count = strcmp(attack.kind, "nail") == 0 || strcmp(attack.kind, "lightning") == 0 ? 2 : 6;
  animation anim = {first, count, "locomotion"};
  animate(character, anim, true);
}
static void bubbles(qa_q1_character_actor *character, double count) {
  if (presentation_only(character)) return;
  qa_q1b_actor *timer = character->game->create(character->game, "death_bubbles");
  timer->owner = qa_q1b_some_actor(character->actor.id);
  timer->count = count;
  character->game->schedule(character->game, timer, 0.1,
      character->game->named.action(character->game->named.ctx, timer, "base:death_bubbles"));
}
void qa_q1_character_actor_pain(qa_q1_character_actor *character, qa_q1b_opt_actor attacker,
                                double damage, bool axe_hit) {
  (void)damage;
  qa_q1_character_source_pose pose;
  bool have_pose = get_pose(character, &pose);
  if (strcmp(character->life, "alive") != 0 || character->input.invisible ||
      character->attack_animation || (have_pose && pose.has_frame)) return;
  select_model(character, have_pose, &pose);
  qa_q1b_game *game = character->game;
  qa_q1_character_input input = character->input;
  if (attacker.has && strcmp(game->host->classname(game->host->ctx, attacker.id), "teledeath") == 0) {
    character_sound(character, "player/teledth1.wav", 0);
  } else if (input.water_level == 3 && strcmp(input.water_type, "water") == 0) {
    bubbles(character, 1);
    character_sound(character, game->host->random(game->host->ctx) > 0.5 ? "player/drown1.wav" : "player/drown2.wav", 1);
  } else if (strcmp(input.water_type, "slime") == 0 || strcmp(input.water_type, "lava") == 0) {
    character_sound(character, game->host->random(game->host->ctx) > 0.5 ? "player/lburn1.wav" : "player/lburn2.wav", 1);
  } else if (character->pain_until <= game->time) {
    character->pain_until = game->time + 0.5;
    if (axe_hit) {
      character_sound(character, "player/axhit1.wav", 1);
    } else {
      char path[64];
      (void)snprintf(path, sizeof path, "player/pain%d.wav",
                     (int)floor(game->host->random(game->host->ctx) * 5 + 1.5));
      character_sound(character, path, 1);
    }
  }
  qa_q1_character_frame_range frames;
  if (have_pose && pose.definition != NULL) frames = pose.definition->pain;
  else {
    bool axe_pose = have_pose && pose.has_axe_pose ? pose.axe_pose : input.axe_pose;
    frames.first = axe_pose ? 29 : 35;
    frames.count = 6;
  }
  animation anim = {frames.first, frames.count, "locomotion"};
  animate(character, anim, false);
}
void qa_q1_character_actor_die(qa_q1_character_actor *character, qa_q1b_opt_actor attacker) {
  if (strcmp(character->life, "alive") != 0) return;
  qa_q1b_game *game = character->game;
  qa_owned_actor actor = character->actor;
  qa_q1b_body body;
  if (!game->host->bodies_read(game->host->ctx, actor.id, &body)) return;
  qa_q1_character_source_pose pose;
  bool have_pose = get_pose(character, &pose);
  const qa_q1_character_definition *definition = have_pose ? pose.definition : NULL;
  character->life = "dying";
  copy_text(character->model, sizeof character->model,
            definition != NULL ? definition->model : "progs/player.mdl");
  character->view_offset = V3(0, 0, -8);
  double health = game->health(game, actor.id);
  if (!presentation_only(character)) {
    if (health < -99) {
      game->host->combat_set_health(game->host->ctx, actor, -99);
      health = game->health(game, actor.id);
    }
    game->host->combat_set_can_take_damage(game->host->ctx, actor, false);
    qa_q1b_player_state *player = game->player(game, actor.id);
    if (player != NULL) {
      const char *kinds[32];
      size_t kind_count = player->powerup_count < 32 ? player->powerup_count : 32;
      for (size_t i = 0; i < kind_count; i++) kinds[i] = player->powerups[i].name;
      for (size_t i = 0; i < kind_count; i++) game->host->powerup(game->host->ctx, actor, kinds[i], 0);
      qa_q1b_powerups_clear(player);
    }
    if (game->options.deathmatch != 0 || game->options.coop) {
      if (character->options.drop_inventory != NULL) character->options.drop_inventory(character->options.ctx);
    }
    qa_vec3 velocity = body.velocity;
    if (velocity.z < 10) velocity.z = velocity.z + game->host->random(game->host->ctx) * 300;
    body.velocity = velocity;
    body.ground = QA_Q1B_NO_ACTOR;
    game->host->bodies_write(game->host->ctx, actor, &body);
  }
  if (health < -40) {
    copy_text(character->model, sizeof character->model, "progs/h_player.mdl");
    character->model_frame = 0;
    character->has_animation = false;
    character->life = "dead";
    character->view_offset = V3(0, 0, 8);
    if (!presentation_only(character)) {
      double scale = health > -50 ? 0.7 : health > -200 ? 2 : 10;
      qa_q1b_body gibbed = body;
      gibbed.origin = qa_add3(body.origin, V3(0, 0, -24));
      gibbed.velocity = qa_scale3(V3(100 * (game->host->random(game->host->ctx) * 2 - 1),
                                     100 * (game->host->random(game->host->ctx) * 2 - 1),
                                     200 + 100 * game->host->random(game->host->ctx)), scale);
      gibbed.bounds = (qa_bounds){V3(-16, -16, 0), V3(16, 16, 56)};
      gibbed.ground = QA_Q1B_NO_ACTOR;
      game->host->bodies_write(game->host->ctx, actor, &gibbed);
      static const char *const GIBS[] = {"gib1", "gib2", "gib3"};
      for (size_t i = 0; i < 3; i++) (void)qa_q1_throw_gib(game, body.origin, GIBS[i], health);
    }
    const char *cause = attacker.has ? game->host->classname(game->host->ctx, attacker.id) : "";
    character_sound(character, strcmp(cause, "teledeath") == 0 || strcmp(cause, "teledeath2") == 0
                                   ? "player/teledth1.wav"
                                   : game->host->random(game->host->ctx) < 0.5 ? "player/gib.wav" : "player/udeath.wav", 0);
    return;
  }
  if (character->input.water_level == 3) {
    bubbles(character, 20);
    character_sound(character, "player/h2odeath.wav", 0);
  } else {
    char path[64];
    (void)snprintf(path, sizeof path, "player/death%d.wav",
                   (int)floor(game->host->random(game->host->ctx) * 4 + 1.5));
    character_sound(character, path, 0);
  }
  qa_q1b_body current;
  bool have_current = !presentation_only(character) &&
                      game->host->bodies_read(game->host->ctx, actor.id, &current);
  if (have_current) {
    current.angles = V3(0, current.angles.y, 0);
    game->host->bodies_write(game->host->ctx, actor, &current);
  }
  animation anim;
  if (definition != NULL) {
    anim.first = definition->death.first; anim.count = definition->death.count; anim.end = "dead";
  } else {
    bool axe_pose = have_pose && pose.has_axe_pose ? pose.axe_pose : character->input.axe_pose;
    if (axe_pose) {
      anim.first = 41; anim.count = 9; anim.end = "dead";
    } else {
      double index = floor(game->host->random(game->host->ctx) * 6);
      if (!(index >= 0 && index < 6)) qa_range_error("Player death animation selection outside source random range");
      anim = DEATHS[(size_t)index];
    }
  }
  animate(character, anim, false);
}
void qa_q1_character_actor_respawn(qa_q1_character_actor *character, const double *health) {
  character->life = "alive";
  copy_text(character->model, sizeof character->model, "progs/player.mdl");
  character->model_frame = 12;
  character->has_animation = false;
  character->attack_animation = false;
  character->walk_frame = 0;
  character->locomotion = NULL;
  character->pain_until = 0;
  character->view_offset = V3(0, 0, 22);
  character->air_finished = character->game->time + 12;
  character->drown_damage = 2;
  character->hazard_at = 0;
  character->in_water = false;
  character->last_fall_speed = 0;
  qa_q1_character_source_pose pose;
  bool have_pose = get_pose(character, &pose);
  select_model(character, have_pose, &pose);
  if (!presentation_only(character)) {
    character->game->host->combat_set_can_take_damage(character->game->host->ctx, character->actor, true);
    if (health != NULL)
      character->game->host->combat_set_health(character->game->host->ctx, character->actor, *health);
  }
}
void qa_q1_character_actor_set_suicide_frame(qa_q1_character_actor *character) {
  character->model_frame = 60;
  character->life = "dead";
  character->has_animation = false;
  character->attack_animation = false;
  character->next_animation = INFINITY;
}
void qa_q1_character_actor_post_move(qa_q1_character_actor *character) {
  if (strcmp(character->life, "alive") != 0) return;
  qa_q1b_game *game = character->game;
  qa_owned_actor actor = character->actor;
  qa_q1b_body body;
  if (!game->host->bodies_read(game->host->ctx, actor.id, &body)) return;
  if (character->last_fall_speed < -300 && body.ground.has && game->health(game, actor.id) > 0) {
    if (strcmp(character->input.water_type, "water") == 0) {
      qa_q1b_game_sound_owned_ch(game, actor, "player/h2ojump.wav", qa_q1b_channel_named("body"));
    } else if (character->last_fall_speed < -650 &&
               (!character->options.has_fall_damage_allowed ||
                character->options.fall_damage_allowed(character->options.ctx))) {
      qa_actor_id world_id = game->world != NULL ? game->world->actor.id : actor.id;
      qa_q1b_opt_actor attacker = game->world != NULL ? qa_q1b_some_actor(world_id) : QA_Q1B_NO_ACTOR;
      (void)game->damage(game, actor.id, world_id, attacker, 5, NULL, "direct", "falling", NULL);
      character_sound(character, "player/land2.wav", 1);
    } else {
      character_sound(character, "player/land.wav", 1);
    }
    character->last_fall_speed = 0;
  }
  if (!body.ground.has) character->last_fall_speed = body.velocity.z;
}
void qa_q1_character_actor_environment(qa_q1_character_actor *character, double seconds,
                                       bool suit, bool noclip) {
  if (strcmp(character->life, "alive") != 0 || noclip) return;
  qa_q1b_game *game = character->game;
  qa_owned_actor actor = character->actor;
  qa_q1_character_input input = character->input;
  qa_q1b_game_set_time(game, seconds);
  qa_q1b_player_state *player = game->player(game, actor.id);
  bool lava_suit = (player != NULL ? qa_q1b_powerups_get(player, "mg3:lavasuit") : 0) > seconds;
  if (input.water_level != 3) {
    if (character->air_finished < seconds) character_sound(character, "player/gasp2.wav", 1);
    else if (character->air_finished < seconds + 9) character_sound(character, "player/gasp1.wav", 1);
    character->air_finished = seconds + 12;
    character->drown_damage = 2;
  } else if (suit || lava_suit) {
    character->air_finished = seconds + 12;
  } else if (character->air_finished < seconds && character->pain_until < seconds) {
    character->drown_damage += 2;
    if (character->drown_damage > 15) character->drown_damage = 10;
    qa_actor_id world_id = game->world != NULL ? game->world->actor.id : actor.id;
    qa_q1b_opt_actor attacker = game->world != NULL ? qa_q1b_some_actor(world_id) : QA_Q1B_NO_ACTOR;
    (void)game->damage(game, actor.id, world_id, attacker, character->drown_damage, NULL, "direct", "drown", NULL);
    character->pain_until = seconds + 1;
  }
  if (input.water_level == 0) {
    if (character->in_water) qa_q1b_game_sound_owned_ch(game, actor, "misc/outwater.wav", qa_q1b_channel_named("body"));
    character->in_water = false;
    return;
  }
  qa_actor_id world_id = game->world != NULL ? game->world->actor.id : actor.id;
  qa_q1b_opt_actor attacker = game->world != NULL ? qa_q1b_some_actor(world_id) : QA_Q1B_NO_ACTOR;
  if (character->hazard_at < seconds && strcmp(input.water_type, "lava") == 0 && !lava_suit) {
    character->hazard_at = seconds + (suit ? 1 : 0.2);
    (void)game->damage(game, actor.id, world_id, attacker, 10 * input.water_level, NULL, "direct", "lava", NULL);
  } else if (character->hazard_at < seconds && strcmp(input.water_type, "slime") == 0 && !suit && !lava_suit) {
    character->hazard_at = seconds + 1;
    (void)game->damage(game, actor.id, world_id, attacker, 4 * input.water_level, NULL, "direct", "slime", NULL);
  }
  if (!character->in_water) {
    qa_q1b_game_sound_owned_ch(game, actor,
        strcmp(input.water_type, "lava") == 0 ? "player/inlava.wav"
        : strcmp(input.water_type, "slime") == 0 ? "player/slimbrn2.wav" : "player/inh2o.wav",
        qa_q1b_channel_named("body"));
    character->in_water = true;
    character->hazard_at = 0;
  }
}
