/* Port of src/compat/q2/rerelease/native-weapon-declaration.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/native_weapon_declaration.h"
#include "qa/network/common/js_number.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/rerelease/layouts.h"
#include "qa/content/mounts/paths.h"

static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static void field_reader(const qa_save_reader *reader, const char *name, qa_save_reader *out) { (void)qa_save_reader_field(reader, name, out); }
static void item_reader(const qa_save_reader *reader, size_t index, qa_save_reader *out) { (void)qa_save_reader_item(reader, index, out); }
static void keys(const qa_save_reader *reader, const char *const *allowed, size_t count) {
  const qa_save_value *value = reader->value;
  if (!value || value->kind != QA_SAVE_OBJECT) qa_save_reader_fail(reader, "expected a declaration record");
  for (size_t i = 0; i < value->entry_count; i++) {
    bool found = false;
    for (size_t j = 0; j < count; j++) if (strcmp(value->entries[i].key, allowed[j]) == 0) found = true;
    if (!found) {
      qa_save_reader child; field_reader(reader, value->entries[i].key, &child);
      qa_save_reader_fail(&child, "unsupported declaration field");
    }
  }
}
static double uint_of(const qa_save_reader *reader, double minimum) {
  double value = qa_save_reader_integer_min(reader, minimum);
  if (value > 4294967295.0) qa_save_reader_fail(reader, "expected a uint32 byte offset or value");
  return value;
}
static double uint_field(const qa_save_reader *reader, const char *name, double minimum) {
  qa_save_reader child; field_reader(reader, name, &child);
  double value = uint_of(&child, minimum);
  qa_save_reader_destroy(&child);
  return value;
}
static const char *text_of(const qa_save_reader *reader, bool allow_empty) {
  const char *value = qa_save_reader_string(reader);
  if ((!allow_empty && value[0] == 0)) qa_save_reader_fail(reader, "expected non-NUL text");
  return value;
}
static char *text_field_dup(const qa_save_reader *reader, const char *name, bool allow_empty) {
  qa_save_reader child; field_reader(reader, name, &child);
  const char *value = text_of(&child, allow_empty);
  char *out = xstrdup(value);
  qa_save_reader_destroy(&child);
  return out;
}
typedef struct { double start, end; } used_range_t;
typedef struct { double byte_length; used_range_t used[16]; size_t used_count; qa_save_reader reader; } fields_t;
static void fields_init(fields_t *f, const qa_save_reader *reader) {
  f->reader = *reader; f->used_count = 0;
  f->byte_length = uint_field(reader, "byteLength", 1);
}
static double fields_field(fields_t *f, const char *name, double width) {
  qa_save_reader child; field_reader(&f->reader, name, &child);
  double start = uint_of(&child, 0), end = start + width;
  double alignment = width == 12 ? 4 : (width < 8 ? width : 8);
  if (end > f->byte_length || (int64_t)start % (int64_t)alignment != 0)
    qa_save_reader_fail(&child, "field exceeds record or violates its storage alignment");
  for (size_t i = 0; i < f->used_count; i++)
    if (start < f->used[i].end && f->used[i].start < end) qa_save_reader_fail(&child, "overlapping declared fields");
  qa_save_reader_destroy(&child);
  f->used[f->used_count++] = (used_range_t){start, end};
  return start;
}
static bool read_registration_layout(const qa_save_reader *reader, qa_native_weapon_registration_layout *out) {
  static const char *const allowed[] = {"byteLength", "name", "tag", "callback"};
  keys(reader, allowed, 4);
  fields_t record; fields_init(&record, reader);
  out->byte_length = record.byte_length;
  out->name = fields_field(&record, "name", 8);
  out->tag = fields_field(&record, "tag", 4);
  out->callback = fields_field(&record, "callback", 8);
  return true;
}
static void free_entry(qa_native_weapon_entry *e) {
  if (e->registration) { free((void *)e->registration->name); free((void *)e->registration); e->registration = NULL; }
}
static bool read_entry(const qa_save_reader *reader, qa_native_weapon_entry *out) {
  static const char *const allowed[] = {"rva", "registration"};
  static const char *const reg_allowed[] = {"rva", "name", "tag", "layout"};
  keys(reader, allowed, 2);
  qa_save_reader registered; field_reader(reader, "registration", &registered);
  bool is_null = qa_save_reader_is_null(&registered);
  if (!is_null) keys(&registered, reg_allowed, 4);
  out->rva = uint_field(reader, "rva", 1);
  out->registration = NULL;
  if (!is_null) {
    qa_native_weapon_registration *reg = calloc(1, sizeof *reg);
    if (!reg) { qa_save_reader_destroy(&registered); return false; }
    reg->rva = uint_field(&registered, "rva", 1);
    reg->name = text_field_dup(&registered, "name", false);
    reg->tag = uint_field(&registered, "tag", 0);
    qa_save_reader layout; field_reader(&registered, "layout", &layout);
    bool ok = read_registration_layout(&layout, &reg->layout);
    qa_save_reader_destroy(&layout);
    if (!ok || !reg->name) { free((void *)reg->name); free(reg); qa_save_reader_destroy(&registered); return false; }
    out->registration = reg;
  }
  qa_save_reader_destroy(&registered);
  return true;
}
static bool read_entry_field(const qa_save_reader *reader, const char *name, qa_native_weapon_entry *out) {
  qa_save_reader child; field_reader(reader, name, &child);
  bool ok = read_entry(&child, out);
  qa_save_reader_destroy(&child);
  return ok;
}
static void free_command(qa_native_weapon_command *cmd) {
  for (size_t i = 0; i < cmd->arguments.count; i++) free((void *)cmd->arguments.values[i]);
  free((void *)cmd->arguments.values); free((void *)cmd->tail);
  cmd->arguments.values = NULL; cmd->arguments.count = 0; cmd->tail = NULL;
}
static bool read_command(const qa_save_reader *reader, qa_native_weapon_command *out) {
  static const char *const allowed[] = {"arguments", "tail"};
  keys(reader, allowed, 2);
  qa_save_reader args; field_reader(reader, "arguments", &args);
  size_t n = qa_save_reader_list_count(&args);
  const char **values = calloc(n ? n : 1, sizeof *values);
  if (!values) { qa_save_reader_destroy(&args); return false; }
  for (size_t i = 0; i < n; i++) {
    qa_save_reader item; item_reader(&args, i, &item);
    values[i] = xstrdup(text_of(&item, true));
    qa_save_reader_destroy(&item);
    if (!values[i]) { for (size_t j = 0; j < i; j++) free((void *)values[j]); free(values); qa_save_reader_destroy(&args); return false; }
  }
  qa_save_reader_destroy(&args);
  if (n == 0 || values[0][0] == 0) {
    for (size_t i = 0; i < n; i++) { free((void *)values[i]); }
    free(values);
    qa_save_reader_fail(reader, "command requires a name");
  }
  out->arguments.values = values; out->arguments.count = n;
  out->tail = text_field_dup(reader, "tail", true);
  return out->tail != NULL;
}
static bool valid_cvar_name(const char *s) {
  if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
  for (size_t i = 1; s[i]; i++) if (!(isalnum((unsigned char)s[i]) || s[i] == '_')) return false;
  return true;
}
static bool read_cvars(const qa_save_reader *reader, qa_native_weapon_cvar **out, size_t *out_count) {
  static const char *const allowed[] = {"name", "value"};
  size_t n = qa_save_reader_list_count(reader);
  qa_native_weapon_cvar *list = calloc(n ? n : 1, sizeof *list);
  if (!list) return false;
  for (size_t i = 0; i < n; i++) {
    qa_save_reader item; item_reader(reader, i, &item);
    keys(&item, allowed, 2);
    char *name = text_field_dup(&item, "name", false);
    bool dup = false;
    if (name) for (size_t j = 0; j < i; j++) {
      const char *a = list[j].name, *b = name; bool same = true;
      size_t k = 0; while (a[k] && b[k]) { if (tolower((unsigned char)a[k]) != tolower((unsigned char)b[k])) same = false; k++; }
      if (same && a[k] == b[k]) dup = true;
    }
    if (!name || !valid_cvar_name(name) || dup) {
      if (name && (!valid_cvar_name(name) || dup)) { qa_save_reader_fail(&item, "invalid or duplicate cvar name"); }
      free(name); qa_save_reader_destroy(&item);
      for (size_t j = 0; j < i; j++) { free((void *)list[j].name); free((void *)list[j].value); } free(list);
      return false;
    }
    list[i].name = name;
    list[i].value = text_field_dup(&item, "value", true);
    qa_save_reader_destroy(&item);
    if (!list[i].value) { for (size_t j = 0; j <= i; j++) { free((void *)list[j].name); free((void *)list[j].value); } free(list); return false; }
  }
  *out = list; *out_count = n;
  return true;
}
static bool read_calls(const qa_save_reader *reader, qa_native_weapon_call_list *out) {
  static const char *const allowed[] = {"signature", "calls"};
  keys(reader, allowed, 2);
  qa_save_reader sig; field_reader(reader, "signature", &sig);
  (void)qa_save_reader_literal_string(&sig, "entity-void");
  qa_save_reader_destroy(&sig);
  qa_save_reader calls; field_reader(reader, "calls", &calls);
  size_t n = qa_save_reader_list_count(&calls);
  qa_native_weapon_entry *list = calloc(n ? n : 1, sizeof *list);
  if (!list) { qa_save_reader_destroy(&calls); return false; }
  for (size_t i = 0; i < n; i++) {
    qa_save_reader item; item_reader(&calls, i, &item);
    bool ok = read_entry(&item, &list[i]);
    qa_save_reader_destroy(&item);
    if (!ok) { for (size_t j = 0; j < i; j++) free_entry(&list[j]); free(list); qa_save_reader_destroy(&calls); return false; }
  }
  qa_save_reader_destroy(&calls);
  if (n == 0 || n > 64) {
    for (size_t i = 0; i < n; i++) { free_entry(&list[i]); }
    free(list);
    qa_save_reader_fail(reader, "expected 1\xe2\x80\x93"
                                 "64 source calls");
  }
  out->calls = list; out->call_count = n;
  return true;
}
static bool digest_valid(const char *s) {
  if (strncmp(s, "sha256:", 7) != 0) return false;
  for (int i = 0; i < 64; i++) if (!isxdigit((unsigned char)s[7 + i]) || (s[7 + i] >= 'A' && s[7 + i] <= 'F')) return false;
  return s[71] == 0;
}
static void free_call_list(qa_native_weapon_call_list *list) {
  for (size_t i = 0; i < list->call_count; i++) free_entry((qa_native_weapon_entry *)&list->calls[i]);
  free((void *)list->calls); list->calls = NULL; list->call_count = 0;
}
void qa_compat_q2_native_weapon_declaration_free(qa_native_weapon_behavior_declaration *d) {
  if (!d) return;
  free((void *)d->artifact_path); free((void *)d->artifact_digest);
  free((void *)d->id); free((void *)d->title);
  free_entry(&d->equipped_weapon.expected); free_entry(&d->allocate.entry); free_entry(&d->free.entry); free_entry(&d->projectile_touch);
  free_call_list(&d->equip); free_call_list(&d->launch);
  for (size_t i = 0; i < d->initialization_classes.count; i++) free((void *)d->initialization_classes.values[i]);
  free((void *)d->initialization_classes.values);
  for (size_t i = 0; i < d->equipment_count; i++) free_command((qa_native_weapon_command *)&d->equipment[i]);
  free((void *)d->equipment);
  free_command(&d->ammunition);
  for (size_t i = 0; i < d->initial_cvar_count; i++) { free((void *)d->initial_cvars[i].name); free((void *)d->initial_cvars[i].value); }
  free((void *)d->initial_cvars);
  for (size_t i = 0; i < d->provisioning_cvar_count; i++) { free((void *)d->provisioning_cvars[i].name); free((void *)d->provisioning_cvars[i].value); }
  free((void *)d->provisioning_cvars);
  memset(d, 0, sizeof *d);
}
#define ROOT_KEYS "version", "kind", "abi", "artifactPath", "artifactDigest", "id", "title", "role", "aspect", "entity", "client", \
  "equippedWeapon", "time", "think", "allocate", "free", "projectileTouch", "equip", "launch", "activateRva", "fireRva", \
  "initializationClasses", "equipment", "ammunition", "initialCvars", "provisioningCvars"
bool qa_compat_q2_read_native_weapon_declaration(const qa_save_value *value, const qa_module_identity *module,
                                                 qa_native_weapon_behavior_declaration *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "native-weapon-profile")) return false;
  static const char *const root_keys[] = {ROOT_KEYS};
  keys(&reader, root_keys, sizeof root_keys / sizeof root_keys[0]);
  qa_save_reader entity_r, client_r, weapon_r, time_r, think_r, alloc_r, free_r;
  field_reader(&reader, "entity", &entity_r); field_reader(&reader, "client", &client_r);
  field_reader(&reader, "equippedWeapon", &weapon_r); field_reader(&reader, "time", &time_r);
  field_reader(&reader, "think", &think_r); field_reader(&reader, "allocate", &alloc_r); field_reader(&reader, "free", &free_r);
  static const char *const entity_keys[] = {"byteLength", "origin", "angles", "velocity", "client", "owner", "viewHeight", "generation", "nextThink", "thinkCallback", "thinkRegistration", "touchCallback"};
  static const char *const client_keys[] = {"byteLength", "weapon", "viewAngles", "forward"};
  static const char *const weapon_keys[] = {"byteLength", "callback", "expected"};
  static const char *const time_keys[] = {"storage", "rva"};
  static const char *const think_keys[] = {"signature", "tag", "registration"};
  static const char *const entry_keys[] = {"signature", "entry"};
  keys(&entity_r, entity_keys, 12); keys(&client_r, client_keys, 4); keys(&weapon_r, weapon_keys, 3);
  keys(&time_r, time_keys, 2); keys(&think_r, think_keys, 3); keys(&alloc_r, entry_keys, 2); keys(&free_r, entry_keys, 2);
  bool ok = false;
  {
    qa_save_reader ap; field_reader(&reader, "artifactPath", &ap);
    const char *raw_path = text_of(&ap, false);
    out->artifact_path = qa_normalize_resource_path(raw_path);
    qa_save_reader_destroy(&ap);
    qa_save_reader dg; field_reader(&reader, "artifactDigest", &dg);
    const char *digest = qa_save_reader_string(&dg);
    if (!digest_valid(digest)) qa_save_reader_fail(&dg, "expected canonical SHA256 identity");
    out->artifact_digest = qa_create_content_digest(digest + 7);
    qa_save_reader_destroy(&dg);
  }
  if (!out->artifact_path || !out->artifact_digest) goto done;
  if (module && (strcmp(module->digest, out->artifact_digest) != 0 || strcmp(module->artifact_path, out->artifact_path) != 0))
    qa_save_reader_fail(&reader, "native profile artifact identity differs");
  {
    fields_t entity_f, client_f, weapon_f;
    fields_init(&entity_f, &entity_r); fields_init(&client_f, &client_r); fields_init(&weapon_f, &weapon_r);
    qa_save_reader equip_r, launch_r;
    field_reader(&reader, "equip", &equip_r); field_reader(&reader, "launch", &launch_r);
    bool calls_ok = read_calls(&equip_r, &out->equip) && read_calls(&launch_r, &out->launch);
    qa_save_reader_destroy(&equip_r); qa_save_reader_destroy(&launch_r);
    if (!calls_ok) goto done;
    qa_save_reader act; field_reader(&reader, "activateRva", &act);
    out->has_activate_rva = !qa_save_reader_is_null(&act);
    if (out->has_activate_rva) out->activate_rva = uint_of(&act, 1);
    qa_save_reader_destroy(&act);
    out->fire_rva = uint_field(&reader, "fireRva", 1);
    if (out->has_activate_rva) {
      bool found = false;
      for (size_t i = 0; i < out->equip.call_count; i++) if (out->equip.calls[i].rva == out->activate_rva) found = true;
      if (!found) qa_save_reader_fail(&reader, "activation must identify a declared equip call");
    }
    {
      bool found = false;
      for (size_t i = 0; i < out->launch.call_count; i++) if (out->launch.calls[i].rva == out->fire_rva) found = true;
      if (!found) qa_save_reader_fail(&reader, "fire must identify a declared launch call");
    }
    qa_save_reader classes; field_reader(&reader, "initializationClasses", &classes);
    size_t class_count = qa_save_reader_list_count(&classes);
    const char **class_names = calloc(class_count ? class_count : 1, sizeof *class_names);
    if (!class_names) { qa_save_reader_destroy(&classes); goto done; }
    for (size_t i = 0; i < class_count; i++) {
      qa_save_reader item; item_reader(&classes, i, &item);
      class_names[i] = xstrdup(text_of(&item, false));
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&classes);
    out->initialization_classes.values = class_names; out->initialization_classes.count = class_count;
    {
      size_t worldspawn = 0; bool unique = true;
      for (size_t i = 0; i < class_count; i++) {
        if (!class_names[i]) goto done;
        if (strcmp(class_names[i], "worldspawn") == 0) worldspawn++;
        for (size_t j = i + 1; j < class_count; j++) if (class_names[j] && strcmp(class_names[i], class_names[j]) == 0) unique = false;
      }
      if (worldspawn != 1 || !unique) qa_save_reader_fail(&reader, "initialization classes require one worldspawn and unique classes");
    }
    qa_save_reader version_r, kind_r, abi_r, aspect_r;
    field_reader(&reader, "version", &version_r); out->version = (int64_t)qa_save_reader_literal_number(&version_r, 1); qa_save_reader_destroy(&version_r);
    field_reader(&reader, "kind", &kind_r); out->kind = qa_save_reader_literal_string(&kind_r, "q2-api2023-trajectory"); qa_save_reader_destroy(&kind_r);
    field_reader(&reader, "abi", &abi_r); out->abi = qa_save_reader_literal_string(&abi_r, "windows-x86-64"); qa_save_reader_destroy(&abi_r);
    qa_save_reader id_r; field_reader(&reader, "id", &id_r); out->id = xstrdup(qa_namespaced(&id_r)); qa_save_reader_destroy(&id_r);
    out->title = text_field_dup(&reader, "title", false);
    static const char *const roles[] = {"rocket", "grenade", "nail", "bolt", "plasma", "energy", "grapple"};
    qa_save_reader role_r; field_reader(&reader, "role", &role_r);
    out->role = qa_save_reader_choice_string(&role_r, roles, 7);
    qa_save_reader_destroy(&role_r);
    field_reader(&reader, "aspect", &aspect_r); out->aspect = qa_save_reader_literal_string(&aspect_r, "trajectory"); qa_save_reader_destroy(&aspect_r);
    if (!out->id || !out->title) goto done;
    out->entity.byte_length = entity_f.byte_length;
    out->entity.origin = fields_field(&entity_f, "origin", 12); out->entity.angles = fields_field(&entity_f, "angles", 12);
    out->entity.velocity = fields_field(&entity_f, "velocity", 12); out->entity.client = fields_field(&entity_f, "client", 8);
    out->entity.owner = fields_field(&entity_f, "owner", 8); out->entity.view_height = fields_field(&entity_f, "viewHeight", 4);
    out->entity.generation = fields_field(&entity_f, "generation", 4); out->entity.next_think = fields_field(&entity_f, "nextThink", 8);
    out->entity.think_callback = fields_field(&entity_f, "thinkCallback", 8); out->entity.think_registration = fields_field(&entity_f, "thinkRegistration", 8);
    out->entity.touch_callback = fields_field(&entity_f, "touchCallback", 8);
    out->client.byte_length = client_f.byte_length;
    out->client.weapon = fields_field(&client_f, "weapon", 8); out->client.view_angles = fields_field(&client_f, "viewAngles", 12);
    out->client.forward = fields_field(&client_f, "forward", 12);
    out->equipped_weapon.byte_length = weapon_f.byte_length;
    out->equipped_weapon.callback = fields_field(&weapon_f, "callback", 8);
    if (!read_entry_field(&weapon_r, "expected", &out->equipped_weapon.expected)) goto done;
    qa_save_reader storage_r; field_reader(&time_r, "storage", &storage_r);
    (void)qa_save_reader_literal_string(&storage_r, "int64-milliseconds"); qa_save_reader_destroy(&storage_r);
    out->time.rva = uint_field(&time_r, "rva", 1);
    qa_save_reader tsig; field_reader(&think_r, "signature", &tsig);
    (void)qa_save_reader_literal_string(&tsig, "entity-void"); qa_save_reader_destroy(&tsig);
    out->think.tag = uint_field(&think_r, "tag", 0);
    qa_save_reader treg; field_reader(&think_r, "registration", &treg);
    if (!read_registration_layout(&treg, &out->think.registration)) { qa_save_reader_destroy(&treg); goto done; }
    qa_save_reader_destroy(&treg);
    qa_save_reader asig; field_reader(&alloc_r, "signature", &asig);
    (void)qa_save_reader_literal_string(&asig, "void-pointer"); qa_save_reader_destroy(&asig);
    if (!read_entry_field(&alloc_r, "entry", &out->allocate.entry)) goto done;
    qa_save_reader fsig; field_reader(&free_r, "signature", &fsig);
    (void)qa_save_reader_literal_string(&fsig, "entity-void"); qa_save_reader_destroy(&fsig);
    if (!read_entry_field(&free_r, "entry", &out->free.entry)) goto done;
    if (!read_entry_field(&reader, "projectileTouch", &out->projectile_touch)) goto done;
    qa_save_reader equip_list; field_reader(&reader, "equipment", &equip_list);
    size_t equip_count = qa_save_reader_list_count(&equip_list);
    qa_native_weapon_command *commands = calloc(equip_count ? equip_count : 1, sizeof *commands);
    if (!commands) { qa_save_reader_destroy(&equip_list); goto done; }
    out->equipment = commands; out->equipment_count = equip_count;
    bool commands_ok = true;
    for (size_t i = 0; i < equip_count; i++) {
      qa_save_reader item; item_reader(&equip_list, i, &item);
      if (!read_command(&item, &commands[i])) commands_ok = false;
      qa_save_reader_destroy(&item);
      if (!commands_ok) break;
    }
    qa_save_reader_destroy(&equip_list);
    if (!commands_ok) goto done;
    qa_save_reader ammo; field_reader(&reader, "ammunition", &ammo);
    bool ammo_ok = read_command(&ammo, &out->ammunition);
    qa_save_reader_destroy(&ammo);
    if (!ammo_ok) goto done;
    qa_save_reader ic, pc;
    field_reader(&reader, "initialCvars", &ic);
    bool ic_ok = read_cvars(&ic, (qa_native_weapon_cvar **)&out->initial_cvars, &out->initial_cvar_count);
    qa_save_reader_destroy(&ic);
    if (!ic_ok) goto done;
    field_reader(&reader, "provisioningCvars", &pc);
    bool pc_ok = read_cvars(&pc, (qa_native_weapon_cvar **)&out->provisioning_cvars, &out->provisioning_cvar_count);
    qa_save_reader_destroy(&pc);
    if (!pc_ok) goto done;
  }
  qa_compat_q2_rerelease_layouts_ensure();
  if (out->entity.byte_length < qa_compat_q2_rerelease_edict_layout.byte_length ||
      out->entity.origin != qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_edict_layout, "s.origin") ||
      out->entity.angles != qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_edict_layout, "s.angles") ||
      out->entity.client != qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_edict_layout, "client") ||
      out->entity.owner != qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_edict_layout, "owner")) {
    qa_save_reader ent; field_reader(&reader, "entity", &ent);
    qa_save_reader_fail(&ent, "declared fields differ from the public API2023 edict prefix");
  }
  ok = true;
done:
  qa_save_reader_destroy(&entity_r); qa_save_reader_destroy(&client_r); qa_save_reader_destroy(&weapon_r);
  qa_save_reader_destroy(&time_r); qa_save_reader_destroy(&think_r); qa_save_reader_destroy(&alloc_r); qa_save_reader_destroy(&free_r);
  qa_save_reader_destroy(&reader);
  if (!ok) qa_compat_q2_native_weapon_declaration_free(out);
  return ok;
}
/* --- serialize: build a save-value tree in the TS literal order --- */
static qa_save_value *sv_num(double v) { return qa_save_value_number(v); }
static qa_save_value *sv_str(const char *s) { return qa_save_value_string(s); }
static bool set_num(qa_save_value *o, const char *k, double v) { return qa_save_value_set(o, k, sv_num(v)); }
static bool set_str(qa_save_value *o, const char *k, const char *v) { return qa_save_value_set(o, k, sv_str(v)); }
static qa_save_value *build_layout(qa_native_weapon_registration_layout l) {
  qa_save_value *o = qa_save_value_object(); if (!o) return NULL;
  if (!set_num(o, "byteLength", l.byte_length) || !set_num(o, "name", l.name) || !set_num(o, "tag", l.tag) || !set_num(o, "callback", l.callback)) { qa_save_value_free(o); return NULL; }
  return o;
}
static qa_save_value *build_entry(qa_native_weapon_entry e) {
  qa_save_value *o = qa_save_value_object(); if (!o) return NULL;
  if (!set_num(o, "rva", e.rva)) { qa_save_value_free(o); return NULL; }
  if (!e.registration) { if (!qa_save_value_set(o, "registration", qa_save_value_null())) { qa_save_value_free(o); return NULL; } return o; }
  qa_save_value *r = qa_save_value_object(); if (!r) { qa_save_value_free(o); return NULL; }
  qa_save_value *layout = build_layout(e.registration->layout);
  if (!layout || !set_num(r, "rva", e.registration->rva) || !set_str(r, "name", e.registration->name) ||
      !set_num(r, "tag", e.registration->tag) || !qa_save_value_set(r, "layout", layout) ||
      !qa_save_value_set(o, "registration", r)) { qa_save_value_free(layout); qa_save_value_free(r); qa_save_value_free(o); return NULL; }
  return o;
}
static qa_save_value *build_calls(qa_native_weapon_call_list list) {
  qa_save_value *o = qa_save_value_object(); if (!o) return NULL;
  qa_save_value *calls = qa_save_value_array();
  if (!calls || !set_str(o, "signature", "entity-void")) { qa_save_value_free(calls); qa_save_value_free(o); return NULL; }
  for (size_t i = 0; i < list.call_count; i++) {
    qa_save_value *e = build_entry(list.calls[i]);
    if (!e || !qa_save_value_push(calls, e)) { qa_save_value_free(e); qa_save_value_free(calls); qa_save_value_free(o); return NULL; }
  }
  if (!qa_save_value_set(o, "calls", calls)) { qa_save_value_free(calls); qa_save_value_free(o); return NULL; }
  return o;
}
static qa_save_value *build_command(qa_native_weapon_command cmd) {
  qa_save_value *o = qa_save_value_object(); if (!o) return NULL;
  qa_save_value *args = qa_save_value_array();
  if (!args) { qa_save_value_free(o); return NULL; }
  for (size_t i = 0; i < cmd.arguments.count; i++)
    if (!qa_save_value_push(args, sv_str(cmd.arguments.values[i]))) { qa_save_value_free(args); qa_save_value_free(o); return NULL; }
  if (!qa_save_value_set(o, "arguments", args) || !set_str(o, "tail", cmd.tail)) { qa_save_value_free(args); qa_save_value_free(o); return NULL; }
  return o;
}
static qa_save_value *build_cvars(const qa_native_weapon_cvar *list, size_t count) {
  qa_save_value *a = qa_save_value_array(); if (!a) return NULL;
  for (size_t i = 0; i < count; i++) {
    qa_save_value *o = qa_save_value_object();
    if (!o || !set_str(o, "name", list[i].name) || !set_str(o, "value", list[i].value) || !qa_save_value_push(a, o)) { qa_save_value_free(o); qa_save_value_free(a); return NULL; }
  }
  return a;
}
static qa_save_value *build_declaration(const qa_native_weapon_behavior_declaration *d) {
  qa_save_value *o = qa_save_value_object(); if (!o) return NULL;
  qa_save_value *entity = qa_save_value_object(), *client = qa_save_value_object(), *weapon = qa_save_value_object();
  qa_save_value *time = qa_save_value_object(), *think = qa_save_value_object(), *alloc = qa_save_value_object(), *free_ = qa_save_value_object();
  qa_save_value *classes = qa_save_value_array(), *equipment = qa_save_value_array();
  bool ok = entity && client && weapon && time && think && alloc && free_ && classes && equipment;
  if (ok) ok = set_num(entity, "byteLength", d->entity.byte_length) && set_num(entity, "origin", d->entity.origin) && set_num(entity, "angles", d->entity.angles) &&
    set_num(entity, "velocity", d->entity.velocity) && set_num(entity, "client", d->entity.client) && set_num(entity, "owner", d->entity.owner) &&
    set_num(entity, "viewHeight", d->entity.view_height) && set_num(entity, "generation", d->entity.generation) && set_num(entity, "nextThink", d->entity.next_think) &&
    set_num(entity, "thinkCallback", d->entity.think_callback) && set_num(entity, "thinkRegistration", d->entity.think_registration) && set_num(entity, "touchCallback", d->entity.touch_callback);
  if (ok) ok = set_num(client, "byteLength", d->client.byte_length) && set_num(client, "weapon", d->client.weapon) &&
    set_num(client, "viewAngles", d->client.view_angles) && set_num(client, "forward", d->client.forward);
  qa_save_value *expected = ok ? build_entry(d->equipped_weapon.expected) : NULL;
  if (ok) ok = expected && set_num(weapon, "byteLength", d->equipped_weapon.byte_length) && set_num(weapon, "callback", d->equipped_weapon.callback) && qa_save_value_set(weapon, "expected", expected);
  if (!ok) qa_save_value_free(expected);
  if (ok) ok = set_str(time, "storage", "int64-milliseconds") && set_num(time, "rva", d->time.rva);
  qa_save_value *think_layout = ok ? build_layout(d->think.registration) : NULL;
  if (ok) ok = think_layout && set_str(think, "signature", "entity-void") && set_num(think, "tag", d->think.tag) && qa_save_value_set(think, "registration", think_layout);
  if (!ok) qa_save_value_free(think_layout);
  qa_save_value *alloc_entry = ok ? build_entry(d->allocate.entry) : NULL;
  if (ok) ok = alloc_entry && set_str(alloc, "signature", "void-pointer") && qa_save_value_set(alloc, "entry", alloc_entry);
  if (!ok) qa_save_value_free(alloc_entry);
  qa_save_value *free_entry = ok ? build_entry(d->free.entry) : NULL;
  if (ok) ok = free_entry && set_str(free_, "signature", "entity-void") && qa_save_value_set(free_, "entry", free_entry);
  if (!ok) qa_save_value_free(free_entry);
  qa_save_value *touch = ok ? build_entry(d->projectile_touch) : NULL;
  qa_save_value *equip_calls = ok ? build_calls(d->equip) : NULL;
  qa_save_value *launch_calls = ok ? build_calls(d->launch) : NULL;
  qa_save_value *ammo = ok ? build_command(d->ammunition) : NULL;
  qa_save_value *icvars = ok ? build_cvars(d->initial_cvars, d->initial_cvar_count) : NULL;
  qa_save_value *pcvars = ok ? build_cvars(d->provisioning_cvars, d->provisioning_cvar_count) : NULL;
  if (ok) ok = touch && equip_calls && launch_calls && ammo && icvars && pcvars;
  if (ok) for (size_t i = 0; i < d->initialization_classes.count && ok; i++) ok = qa_save_value_push(classes, sv_str(d->initialization_classes.values[i]));
  if (ok) for (size_t i = 0; i < d->equipment_count && ok; i++) { qa_save_value *cmd = build_command(d->equipment[i]); ok = cmd && qa_save_value_push(equipment, cmd); if (!ok) qa_save_value_free(cmd); }
  bool sets_started = ok;
  if (ok) ok = set_num(o, "version", (double)d->version) && set_str(o, "kind", d->kind) && set_str(o, "abi", d->abi) &&
    set_str(o, "artifactPath", d->artifact_path) && set_str(o, "artifactDigest", d->artifact_digest) && set_str(o, "id", d->id) &&
    set_str(o, "title", d->title) && set_str(o, "role", d->role) && set_str(o, "aspect", d->aspect) &&
    qa_save_value_set(o, "entity", entity) && qa_save_value_set(o, "client", client) && qa_save_value_set(o, "equippedWeapon", weapon) &&
    qa_save_value_set(o, "time", time) && qa_save_value_set(o, "think", think) && qa_save_value_set(o, "allocate", alloc) &&
    qa_save_value_set(o, "free", free_) && qa_save_value_set(o, "projectileTouch", touch) && qa_save_value_set(o, "equip", equip_calls) &&
    qa_save_value_set(o, "launch", launch_calls) &&
    (d->has_activate_rva ? set_num(o, "activateRva", d->activate_rva) : qa_save_value_set(o, "activateRva", qa_save_value_null())) &&
    set_num(o, "fireRva", d->fire_rva) && qa_save_value_set(o, "initializationClasses", classes) &&
    qa_save_value_set(o, "equipment", equipment) && qa_save_value_set(o, "ammunition", ammo) &&
    qa_save_value_set(o, "initialCvars", icvars) && qa_save_value_set(o, "provisioningCvars", pcvars);
  if (!ok) {
    /* OOM only: once sets into o began, o owns the adopted children;
     * unadopted children leak rather than double-free. */
    if (!sets_started) {
      qa_save_value_free(entity); qa_save_value_free(client); qa_save_value_free(weapon); qa_save_value_free(time);
      qa_save_value_free(think); qa_save_value_free(alloc); qa_save_value_free(free_); qa_save_value_free(classes);
      qa_save_value_free(equipment); qa_save_value_free(touch); qa_save_value_free(equip_calls); qa_save_value_free(launch_calls);
      qa_save_value_free(ammo); qa_save_value_free(icvars); qa_save_value_free(pcvars);
    }
    qa_save_value_free(o);
    return NULL;
  }
  return o;
}
typedef struct { char *buf; size_t len, cap; bool oom; } jbuf;
static void jput(jbuf *b, const char *text, size_t n) {
  if (b->oom) return;
  if (b->len + n + 1 > b->cap) { size_t cap = (b->len + n + 1) * 2; char *next = realloc(b->buf, cap); if (!next) { b->oom = true; return; } b->buf = next; b->cap = cap; }
  memcpy(b->buf + b->len, text, n); b->len += n; b->buf[b->len] = 0;
}
static void jstr(jbuf *b, const char *s) {
  jput(b, "\"", 1);
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    char tmp[8];
    switch (*p) {
      case '"': jput(b, "\\\"", 2); break;
      case '\\': jput(b, "\\\\", 2); break;
      case '\b': jput(b, "\\b", 2); break;
      case '\f': jput(b, "\\f", 2); break;
      case '\n': jput(b, "\\n", 2); break;
      case '\r': jput(b, "\\r", 2); break;
      case '\t': jput(b, "\\t", 2); break;
      default:
        if (*p < 0x20) { snprintf(tmp, sizeof tmp, "\\u%04x", *p); jput(b, tmp, strlen(tmp)); }
        else jput(b, (const char *)p, 1);
    }
  }
  jput(b, "\"", 1);
}
static void jvalue(jbuf *b, const qa_save_value *v) {
  char tmp[64];
  switch (v->kind) {
    case QA_SAVE_OBJECT:
      jput(b, "{", 1);
      for (size_t i = 0; i < v->entry_count; i++) { if (i) jput(b, ",", 1); jstr(b, v->entries[i].key); jput(b, ":", 1); jvalue(b, v->entries[i].value); }
      jput(b, "}", 1); break;
    case QA_SAVE_ARRAY:
      jput(b, "[", 1);
      for (size_t i = 0; i < v->item_count; i++) { if (i) jput(b, ",", 1); jvalue(b, v->items[i]); }
      jput(b, "]", 1); break;
    case QA_SAVE_STRING: jstr(b, v->text); break;
    case QA_SAVE_NUMBER:
      if (v->number >= -9007199254740991.0 && v->number <= 9007199254740991.0 && v->number == trunc(v->number)) snprintf(tmp, sizeof tmp, "%lld", (long long)v->number);
      else snprintf(tmp, sizeof tmp, "%s", QA_JS_NUMBER(v->number));
      jput(b, tmp, strlen(tmp)); break;
    case QA_SAVE_BOOLEAN: jput(b, v->boolean ? "true" : "false", v->boolean ? 4 : 5); break;
    default: jput(b, "null", 4); break;
  }
}
char *qa_compat_q2_serialize_native_weapon_declaration(const qa_native_weapon_behavior_declaration *value) {
  qa_save_value *tree = build_declaration(value);
  if (!tree) return NULL;
  qa_native_weapon_behavior_declaration parsed;
  if (!qa_compat_q2_read_native_weapon_declaration(tree, NULL, &parsed)) { qa_save_value_free(tree); return NULL; }
  qa_save_value_free(tree);
  tree = build_declaration(&parsed);
  qa_compat_q2_native_weapon_declaration_free(&parsed);
  if (!tree) return NULL;
  jbuf b = {NULL, 0, 0, false};
  jvalue(&b, tree);
  qa_save_value_free(tree);
  if (b.oom) { free(b.buf); return NULL; }
  return b.buf ? b.buf : xstrdup("");
}
bool qa_compat_q2_same_native_weapon_declaration(const qa_native_weapon_behavior_declaration *left,
                                                 const qa_native_weapon_behavior_declaration *right) {
  char *a = qa_compat_q2_serialize_native_weapon_declaration(left);
  char *b = qa_compat_q2_serialize_native_weapon_declaration(right);
  bool same = a && b && strcmp(a, b) == 0;
  free(a); free(b);
  return same;
}
static void fmt_double(char *out, size_t cap, double v) {
  qa_net_js_number(v, out, cap);
}
void qa_compat_q2_validate_native_weapon_image(const qa_native_weapon_behavior_declaration *profile,
                                               const char *image_abi_kind, qa_compat_q2_pe_file image) {
  if (strcmp(image_abi_kind, profile->abi) != 0) qa_range_error("Native weapon declaration ABI differs from its PE image");
  struct range { double rva, size; const char *access; };
  qa_native_weapon_entry entries[68 + 4];
  size_t count = 0;
  for (size_t i = 0; i < profile->equip.call_count && count < 72; i++) entries[count++] = profile->equip.calls[i];
  for (size_t i = 0; i < profile->launch.call_count && count < 72; i++) entries[count++] = profile->launch.calls[i];
  if (count < 72) entries[count++] = profile->allocate.entry;
  if (count < 72) entries[count++] = profile->free.entry;
  if (count < 72) entries[count++] = profile->projectile_touch;
  if (count < 72) entries[count++] = profile->equipped_weapon.expected;
  struct range ranges[72 + 73];
  size_t range_count = 0;
  for (size_t i = 0; i < count; i++) {
    ranges[range_count++] = (struct range){entries[i].rva, 1, "execute"};
    if (entries[i].registration) ranges[range_count++] = (struct range){entries[i].registration->rva, entries[i].registration->layout.byte_length, "read"};
  }
  ranges[range_count++] = (struct range){profile->time.rva, 8, "write"};
  for (size_t i = 0; i < range_count; i++) {
    double rva = ranges[i].rva, size = ranges[i].size;
    bool inside = false;
    for (size_t j = 0; j < image.section_count; j++) {
      const qa_compat_q2_pe_section *s = &image.sections[j];
      if (rva >= s->rva && rva + size <= s->rva + s->mapped_size && s->permissions && strstr(s->permissions, ranges[i].access)) inside = true;
    }
    if (rva + size > image.image_size || !inside) {
      char a[64], b[64]; fmt_double(a, sizeof a, rva); fmt_double(b, sizeof b, size);
      qa_range_error("Native weapon %s range is outside its PE section: %s+%s", ranges[i].access, a, b);
    }
  }
}


