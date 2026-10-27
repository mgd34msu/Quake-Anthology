/* Port of src/compat/q2/rerelease/q2eaks-weapon-profile.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/q2eaks_weapon_profile.h"
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/rerelease/api.h"
#include "qa/compat/q2/rerelease/imports.h"
#include "qa/compat/q2/rerelease/native_weapon_declaration.h"

const qa_content_digest qa_compat_q2_q2eaks_weapon_digest = "sha256:b60b79f7fb6f115218681a9cbab8765267e34f72466975526df05ad288925dde";
static const qa_guest_value_layout float_layout = {false, "float32", {NULL, 0, 0, 0, NULL, 0}};
qa_compat_q2_guest_call_signature qa_compat_q2_q2eaks_weapon_think_signature(void) {
  const qa_guest_value_layout params[] = {qa_compat_q2_rr_pointer_layout};
  return qa_compat_q2_rerelease_signature(params, 1, true, qa_compat_q2_rr_pointer_layout);
}
qa_compat_q2_guest_call_signature qa_compat_q2_q2eaks_fire_rocket_signature(void) {
  const qa_guest_value_layout params[] = {qa_compat_q2_rr_pointer_layout, qa_compat_q2_rr_pointer_layout, qa_compat_q2_rr_pointer_layout,
                                          qa_compat_q2_rr_int_layout, qa_compat_q2_rr_int_layout, float_layout, qa_compat_q2_rr_int_layout};
  return qa_compat_q2_rerelease_signature(params, 7, false, qa_compat_q2_rr_pointer_layout);
}
static const qa_guest_field_layout projectile_fields[] = {
    {"s.origin", 4, "float32", 3}, {"s.angles", 16, "float32", 3}, {"client", 0x78, "pointer", 1},
    {"owner", 0x5b8, "pointer", 1}, {"velocity", 0x694, "float32", 3}, {"nextthink", 0x6d8, "int64", 1},
    {"think.value", 0x700, "pointer", 1}, {"think.list", 0x708, "pointer", 1}, {"touch.value", 0x710, "pointer", 1},
    {"touch.list", 0x718, "pointer", 1}, {"viewheight", 0x7a0, "int32", 1}};
const qa_guest_layout qa_compat_q2_q2eaks_projectile_layout = {"q2eaks-v0.21:observed-projectile-fields", 0x7a8, 8, 8, projectile_fields, 11};
static const qa_guest_field_layout client_fields[] = {
    {"pers.hand", 0xa50, "int32", 1}, {"pers.weapon", 0xbe8, "pointer", 1}, {"v_angle", 0x1998, "float32", 3}, {"v_forward", 0x19a4, "float32", 3}};
const qa_guest_layout qa_compat_q2_q2eaks_weapon_client_layout = {"q2eaks-v0.21:observed-weapon-client-fields", 0x19b0, 8, 8, client_fields, 4};

static qa_guest_address entry_at(const qa_compat_q2_memory *memory, qa_guest_address base, int64_t rva) {
  qa_guest_address address = qa_compat_q2_mem_offset(memory, base, rva);
  memory->check(memory->ctx, address, 1, "execute");
  return address;
}
static qa_guest_address registered_at(const qa_compat_q2_memory *memory, qa_guest_address base, int64_t rva, const char *name, uint32_t tag, int64_t expected_rva) {
  qa_guest_address record = qa_compat_q2_mem_offset(memory, base, rva);
  qa_guest_address text, callback;
  bool has_text = qa_compat_q2_mem_read_pointer(memory, record, &text);
  bool has_callback = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, record, 16), &callback);
  bool differs = !has_text || !has_callback;
  if (!differs) {
    char *actual = qa_compat_q2_read_guest_string(memory, text, (double)strlen(name) + 1);
    differs = strcmp(actual, name) != 0;
    free(actual);
  }
  if (!differs) differs = qa_compat_q2_mem_read_u32(memory, qa_compat_q2_mem_offset(memory, record, 8)) != tag;
  if (!differs) differs = callback.byte_offset != entry_at(memory, base, expected_rva).byte_offset;
  if (differs) qa_range_error("Q2Eaks typed source callback registration differs: %s", name);
  memory->check(memory->ctx, callback, 1, "execute");
  return callback;
}
qa_compat_q2_q2eaks_weapon_entries qa_compat_q2_q2eaks_get_weapon_entries(const qa_compat_q2_memory *memory, qa_guest_address image_base) {
  if (strcmp(memory->module.digest, qa_compat_q2_q2eaks_weapon_digest) != 0 || memory->pointer_bytes != 8)
    qa_range_error("Q2Eaks weapon profile requires the exact v0.21 Windows x64 artifact");
  qa_guest_address level_time = qa_compat_q2_mem_offset(memory, image_base, 0x2999c8);
  memory->check(memory->ctx, level_time, 8, "read");
  qa_compat_q2_q2eaks_weapon_entries out;
  out.weapon_run_think = entry_at(memory, image_base, 0xed420);
  out.rocket_launcher_fire = entry_at(memory, image_base, 0xef900);
  out.fire_rocket = entry_at(memory, image_base, 0x98310);
  out.spawn = entry_at(memory, image_base, 0x95010);
  out.freed = registered_at(memory, image_base, 0x21e0c8, "G_FreeEdict", 20, 0x95140);
  out.rocket_touch = registered_at(memory, image_base, 0x21e1e8, "rocket_touch", 21, 0x98060);
  out.level_time = level_time;
  return out;
}
static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static bool make_entry(qa_native_weapon_entry *out, double rva, bool has_reg, double reg_rva, const char *name, double tag) {
  out->rva = rva; out->registration = NULL;
  if (!has_reg) return true;
  qa_native_weapon_registration *reg = calloc(1, sizeof *reg);
  if (!reg) return false;
  reg->rva = reg_rva; reg->name = xstrdup(name); reg->tag = tag;
  reg->layout = (qa_native_weapon_registration_layout){24, 0, 8, 16};
  if (!reg->name) { free(reg); return false; }
  out->registration = reg;
  return true;
}
static bool make_command(qa_native_weapon_command *out, const char *first, const char *second) {
  const char **args = calloc(2, sizeof *args);
  if (!args) return false;
  args[0] = xstrdup(first); args[1] = xstrdup(second);
  out->tail = xstrdup(second);
  out->arguments.values = args; out->arguments.count = 2;
  return args[0] && args[1] && out->tail;
}
bool qa_compat_q2_q2eaks_weapon_declaration(const char *artifact_path, qa_native_weapon_behavior_declaration *out) {
  memset(out, 0, sizeof *out);
  out->version = 1; out->kind = "q2-api2023-trajectory"; out->abi = "windows-x86-64"; out->aspect = "trajectory"; out->role = "rocket";
  out->artifact_path = xstrdup(artifact_path);
  out->artifact_digest = xstrdup(qa_compat_q2_q2eaks_weapon_digest);
  out->id = xstrdup("native:rocket-trajectory"); out->title = xstrdup("Faster rockets");
  out->entity = (qa_native_weapon_entity_layout){0x7a8, 4, 16, 0x694, 0x78, 0x5b8, 0x7a0, 0x5c0, 0x6d8, 0x700, 0x708, 0x710};
  out->client = (qa_native_weapon_client_layout){0x19b0, 0xbe8, 0x1998, 0x19a4};
  out->equipped_weapon.byte_length = 0x30; out->equipped_weapon.callback = 0x28;
  out->time.rva = 0x2999c8;
  out->think.tag = 20; out->think.registration = (qa_native_weapon_registration_layout){24, 0, 8, 16};
  out->has_activate_rva = true; out->activate_rva = 0xed4d0; out->fire_rva = 0xef900;
  bool ok = out->artifact_path && out->artifact_digest && out->id && out->title;
  ok = ok && make_entry(&out->equipped_weapon.expected, 0xefaf0, false, 0, NULL, 0);
  ok = ok && make_entry(&out->allocate.entry, 0x95010, false, 0, NULL, 0);
  ok = ok && make_entry(&out->free.entry, 0x95140, true, 0x21e0c8, "G_FreeEdict", 20);
  ok = ok && make_entry(&out->projectile_touch, 0x98060, true, 0x21e1e8, "rocket_touch", 21);
  qa_native_weapon_entry *equip = calloc(1, sizeof *equip), *launch = calloc(2, sizeof *launch);
  if (!equip || !launch) { free(equip); free(launch); qa_compat_q2_native_weapon_declaration_free(out); return false; }
  out->equip.calls = equip; out->equip.call_count = 1;
  out->launch.calls = launch; out->launch.call_count = 2;
  ok = ok && make_entry(&equip[0], 0xed4d0, false, 0, NULL, 0);
  ok = ok && make_entry(&launch[0], 0xed420, false, 0, NULL, 0) && make_entry(&launch[1], 0xef900, false, 0, NULL, 0);
  static const char *const class_names[] = {"worldspawn", "info_player_start", "info_player_deathmatch", "info_player_coop",
                                            "info_player_team1", "info_player_team2", "info_player_intermission"};
  const char **classes = calloc(7, sizeof *classes);
  if (!classes) { qa_compat_q2_native_weapon_declaration_free(out); return false; }
  out->initialization_classes.values = classes; out->initialization_classes.count = 7;
  for (int i = 0; i < 7; i++) { classes[i] = xstrdup(class_names[i]); if (!classes[i]) ok = false; }
  qa_native_weapon_command *equipment = calloc(3, sizeof *equipment);
  if (!equipment) { qa_compat_q2_native_weapon_declaration_free(out); return false; }
  out->equipment = equipment; out->equipment_count = 3;
  ok = ok && make_command(&equipment[0], "give", "Rocket Launcher");
  ok = ok && make_command(&equipment[1], "give", "Rockets");
  ok = ok && make_command(&equipment[2], "use", "Rocket Launcher");
  ok = ok && make_command(&out->ammunition, "give", "Rockets");
  qa_native_weapon_cvar *initial = calloc(1, sizeof *initial), *provisioning = calloc(1, sizeof *provisioning);
  if (!initial || !provisioning) { free(initial); free(provisioning); qa_compat_q2_native_weapon_declaration_free(out); return false; }
  out->initial_cvars = initial; out->initial_cvar_count = 1;
  out->provisioning_cvars = provisioning; out->provisioning_cvar_count = 1;
  initial[0].name = xstrdup("g_faster_rockets"); initial[0].value = xstrdup("1");
  provisioning[0].name = xstrdup("cheats"); provisioning[0].value = xstrdup("1");
  ok = ok && initial[0].name && initial[0].value && provisioning[0].name && provisioning[0].value;
  if (!ok) qa_compat_q2_native_weapon_declaration_free(out);
  return ok;
}
