/* Port of src/app/bootstrap/simulation/rerelease-weapon-checkpoint.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/rerelease_weapon_checkpoint.h"
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/rerelease/native_weapon_declaration.h"
#include "qa/persistence/native_weapon.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"
#include "qa/world/gameplay/weapon_behaviors.h"
static qa_save_reader fld(const qa_save_reader *r, const char *n) {
  qa_save_reader o; if (!qa_save_reader_field(r, n, &o)) qa_save_reader_fail(r, "out of memory"); return o;
}
static qa_app_sim_rerelease_weapon_trajectory read_trajectory(const qa_save_reader *r) {
  qa_save_reader f; qa_app_sim_rerelease_weapon_trajectory out;
  f = fld(r, "origin"); out.origin = qa_read_vector(&f); qa_save_reader_destroy(&f);
  f = fld(r, "angles"); out.angles = qa_read_vector(&f); qa_save_reader_destroy(&f);
  f = fld(r, "velocity"); out.velocity = qa_read_vector(&f); qa_save_reader_destroy(&f);
  return out;
}
qa_app_sim_rerelease_weapon_behavior_checkpoint qa_app_sim_read_rerelease_weapon_behavior_checkpoint(
    const qa_save_reader *reader, qa_weapon_behavior_definition definition,
    qa_native_weapon_behavior_declaration expected_declaration,
    const qa_native_weapon_profiles *profiles) {
  qa_app_sim_rerelease_weapon_behavior_checkpoint out; memset(&out, 0, sizeof out);
  qa_save_reader f;
  f = fld(reader, "definition");
  qa_weapon_behavior_definition saved = qa_read_weapon_behavior_definition(&f, definition);
  qa_save_reader_destroy(&f);
  f = fld(reader, "declaration");
  qa_native_weapon_behavior_declaration declaration = qa_read_saved_native_weapon_declaration(&f, profiles, saved);
  qa_save_reader_destroy(&f);
  if (!qa_compat_q2_same_native_weapon_declaration(&declaration, &expected_declaration))
    qa_save_reader_fail(reader, "native trajectory checkpoint declaration changed");
  out.declaration = declaration; out.definition = saved;
  f = fld(reader, "version"); out.version = qa_save_reader_literal_number(&f, 1); qa_save_reader_destroy(&f);
  qa_save_reader map = fld(reader, "map");
  f = fld(&map, "map"); out.map = qa_save_reader_string(&f); qa_save_reader_destroy(&f);
  f = fld(&map, "entities"); out.entities = qa_save_reader_string(&f); qa_save_reader_destroy(&f);
  f = fld(&map, "spawnPoint"); out.spawn_point = qa_save_reader_string(&f); qa_save_reader_destroy(&f);
  qa_save_reader_destroy(&map);
  f = fld(reader, "time"); out.time = qa_save_reader_finite(&f); qa_save_reader_destroy(&f);
  f = fld(reader, "cvars"); out.cvars = qa_save_reader_bytes(&f, &out.cvars_length); qa_save_reader_destroy(&f);
  /* game/level RereleaseSourceSave trees stay borrowed in the reader's
   * tree through the opaque pointers (sibling native save slice). */
  out.game = NULL; out.level = NULL;
  f = fld(reader, "configstrings");
  out.configstring_count = qa_save_reader_list_count(&f);
  out.configstrings = out.configstring_count ? malloc(out.configstring_count * sizeof *out.configstrings) : NULL;
  for (size_t i = 0; i < out.configstring_count; i++) {
    qa_save_reader e, bf; qa_save_reader_item(&f, i, &e);
    bf = fld(&e, "index"); out.configstrings[i].index = qa_save_reader_integer_min(&bf, 0); qa_save_reader_destroy(&bf);
    bf = fld(&e, "value"); out.configstrings[i].value = qa_save_reader_string(&bf); qa_save_reader_destroy(&bf);
    qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&f);
  f = fld(reader, "retired");
  out.retired_count = qa_save_reader_list_count(&f);
  out.retired = out.retired_count ? malloc(out.retired_count * sizeof *out.retired) : NULL;
  for (size_t i = 0; i < out.retired_count; i++) {
    qa_save_reader e, bf; qa_save_reader_item(&f, i, &e);
    bf = fld(&e, "actor"); out.retired[i].actor = qa_read_saved_actor(&bf); qa_save_reader_destroy(&bf);
    bf = fld(&e, "trajectory"); out.retired[i].trajectory = read_trajectory(&bf); qa_save_reader_destroy(&bf);
    qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&f);
  f = fld(reader, "bindings");
  out.binding_count = qa_save_reader_list_count(&f);
  out.bindings = out.binding_count ? malloc(out.binding_count * sizeof *out.bindings) : NULL;
  static const char *const kinds[] = {"client", "target", "projectile"};
  for (size_t i = 0; i < out.binding_count; i++) {
    qa_save_reader e, bf; qa_save_reader_item(&f, i, &e);
    bf = fld(&e, "slot"); out.bindings[i].slot = qa_save_reader_integer_min(&bf, 1); qa_save_reader_destroy(&bf);
    bf = fld(&e, "actor"); out.bindings[i].actor = qa_read_saved_actor(&bf); qa_save_reader_destroy(&bf);
    bf = fld(&e, "kind"); out.bindings[i].kind = qa_save_reader_choice_string(&bf, kinds, 3); qa_save_reader_destroy(&bf);
    bf = fld(&e, "generation"); out.bindings[i].generation = qa_save_reader_integer_min(&bf, 0); qa_save_reader_destroy(&bf);
    qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&f);
  return out;
}
