/* Port of src/content/mods/qvm-presentation.ts. GPL-2.0-or-later. */
#include "qa/content/mods/qvm_presentation.h"
#include "qa/content/mods/declaration.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/persistence/shared.h"

/* TS QVM_MAX_PRIVATE_ARGUMENT_WORDS (compat/qvm/image.ts). */
#define QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS 62

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static const char *fstr(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  const char *v = qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return v;
}
static const char *fchoice(const qa_save_reader *r, const char *name,
                           const char *const *choices, size_t n) {
  qa_save_reader f = field(r, name);
  const char *v = qa_save_reader_choice_string(&f, choices, n);
  qa_save_reader_destroy(&f);
  return v;
}
static double fint(const qa_save_reader *r, const char *name, double min) {
  qa_save_reader f = field(r, name);
  double v = qa_save_reader_integer_min(&f, min);
  qa_save_reader_destroy(&f);
  return v;
}
static bool fpresent(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  bool p = !qa_save_reader_is_undefined(&f);
  qa_save_reader_destroy(&f);
  return p;
}
static void *xalloc(const qa_save_reader *r, size_t count, size_t size) {
  void *p = count > 0 ? qa_mod_declaration_calloc(count, size) : NULL;
  if (count > 0 && p == NULL) qa_save_reader_fail(r, "out of memory");
  return p;
}
static qa_double_view read_int_list(const qa_save_reader *r, const char *name) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  double *vals = (double *)xalloc(r, count, sizeof *vals);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    vals[i] = qa_save_reader_integer_min(&entry, 0);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  qa_double_view view = {vals, count};
  return view;
}

/* Mirror of normalizeResourcePath (TS owner:
 * src/content/mounts/paths.ts). See callbacks.c. */
static char *normalize_resource_path(const char *path) {
  size_t n = strlen(path);
  char *out = (char *)qa_mod_declaration_malloc(n + 1);
  if (out == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i <= n; i++) out[i] = path[i] == '\\' ? '/' : path[i];
  bool bad = n == 0;
  if (n >= 2 && out[1] == ':' &&
      ((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z')))
    bad = true;
  size_t start = 0;
  for (size_t i = 0; i <= n && !bad; i++) {
    if (out[i] == '/' || out[i] == '\0') {
      size_t len = i - start;
      if (len == 0) bad = true;
      if (len == 1 && out[start] == '.') bad = true;
      if (len == 2 && out[start] == '.' && out[start + 1] == '.') bad = true;
      start = i + 1;
    }
  }
  if (bad) qa_range_error("Invalid relative resource path: %s", path);
  return out;
}

static double read_int32(const qa_save_reader *r) {
  double value = qa_save_reader_integer_min(r, -2147483648.0);
  if (value > 2147483647.0) qa_save_reader_fail(r, "Source scalar exceeds int32");
  return value;
}

static qa_qvm_presentation_argument read_argument(const qa_save_reader *r) {
  static const char *const kinds[] = {"int32", "float32", "address", "source"};
  static const char *const sources[] = {
      "player-state", "entity-state", "centity", "origin", "snapshot",
      "client-number", "time", "event", "parameter", "snapshot-number",
      "server-command-sequence"};
  qa_qvm_presentation_argument argument;
  memset(&argument, 0, sizeof argument);
  argument.kind = fchoice(r, "kind", kinds, 4);
  qa_save_reader value = field(r, "value");
  if (strcmp(argument.kind, "source") == 0) {
    argument.source_value =
        qa_save_reader_choice_string(&value, sources, sizeof sources / sizeof sources[0]);
  } else if (strcmp(argument.kind, "int32") == 0) {
    argument.number_value = read_int32(&value);
  } else if (strcmp(argument.kind, "address") == 0) {
    argument.number_value = qa_save_reader_integer_min(&value, 0);
  } else {
    double scalar = qa_save_reader_finite(&value);
    /* TS Math.fround staging: double -> float must stay finite. */
    if (!isfinite((double)(float)scalar))
      qa_save_reader_fail(&value, "Source scalar exceeds float32");
    argument.number_value = scalar;
  }
  qa_save_reader_destroy(&value);
  return argument;
}

static qa_qvm_presentation_call read_call(const qa_save_reader *r) {
  qa_qvm_presentation_call call;
  memset(&call, 0, sizeof call);
  qa_save_reader list = field(r, "arguments");
  size_t count = qa_save_reader_list_count(&list);
  qa_qvm_presentation_argument *rows =
      (qa_qvm_presentation_argument *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    rows[i] = read_argument(&entry);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  if (count > QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS)
    qa_save_reader_fail(r, "Source presentation call exceeds QVM argument ABI");
  call.arguments = rows;
  call.argument_count = count;
  call.entry = fint(r, "entry", 0);
  if (fpresent(r, "when")) {
    static const char *const whens[] = {"weapon-presented"};
    call.when = fchoice(r, "when", whens, 1);
  }
  return call;
}
static qa_qvm_presentation_call *read_calls(const qa_save_reader *r,
                                            const char *name, size_t *out_count) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_qvm_presentation_call *rows =
      (qa_qvm_presentation_call *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    rows[i] = read_call(&entry);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return rows;
}

static qa_qvm_presentation_program read_program(const qa_save_reader *r) {
  static const char *const profiles[] = {"q3-modern", "q3-1.16n-base"};
  qa_qvm_presentation_program program;
  memset(&program, 0, sizeof program);
  program.path = normalize_resource_path(fstr(r, "path"));
  qa_save_reader f = field(r, "digest");
  program.digest = qa_read_digest(&f);
  qa_save_reader_destroy(&f);
  program.abi_profile = fchoice(r, "abiProfile", profiles, 2);
  return program;
}

qa_qvm_mod_presentation_declaration *qa_read_qvm_mod_presentation_declaration(
    const qa_save_reader *reader) {
  static const char *const runtimes[] = {"qvm-player-events", "qvm-scene"};
  static const char *const hud_modes[] = {"overlay", "replace-status"};
  static const char *const parts[] = {"body", "lower", "upper", "head"};
  qa_qvm_mod_presentation_declaration *decl =
      (qa_qvm_mod_presentation_declaration *)qa_mod_declaration_calloc(1, sizeof *decl);
  if (decl == NULL) return NULL;
  qa_save_reader storage = field(reader, "storage");
  qa_save_reader centities = field(&storage, "centities");
  const char *runtime = fchoice(reader, "runtime", runtimes, 2);
  qa_qvm_presentation_base base;
  memset(&base, 0, sizeof base);
  {
    qa_save_reader f = field(reader, "version");
    base.version = (int64_t)qa_save_reader_literal_number(&f, 1);
    qa_save_reader_destroy(&f);
    f = field(reader, "gameplay");
    base.gameplay = read_program(&f);
    qa_save_reader_destroy(&f);
    f = field(reader, "cgame");
    base.cgame = read_program(&f);
    qa_save_reader_destroy(&f);
  }
  if (fpresent(reader, "hud")) {
    qa_save_reader hud = field(reader, "hud");
    qa_qvm_presentation_hud *row =
        (qa_qvm_presentation_hud *)xalloc(reader, 1, sizeof *row);
    row->mode = fchoice(&hud, "mode", hud_modes, 2);
    row->frame = read_calls(&hud, "frame", &row->frame_count);
    qa_save_reader_destroy(&hud);
    base.hud = row;
  }
  base.initialize = read_calls(reader, "initialize", &base.initialize_count);
  base.refresh = read_calls(reader, "refresh", &base.refresh_count);
  base.frame = read_calls(reader, "frame", &base.frame_count);
  double game_state = fint(&storage, "gameState", 0);
  qa_double_view time = read_int_list(&storage, "time");
  qa_double_view frame_time = read_int_list(&storage, "frameTime");
  qa_double_view view_origin = read_int_list(&storage, "viewOrigin");
  qa_double_view view_angles = {NULL, 0};
  qa_double_view view_axis = {NULL, 0};
  bool has_angles = fpresent(&storage, "viewAngles");
  bool has_axis = fpresent(&storage, "viewAxis");
  if (has_angles) view_angles = read_int_list(&storage, "viewAngles");
  if (has_axis) view_axis = read_int_list(&storage, "viewAxis");
  /* A kept list is non-NULL even when empty (qa_mod_declaration_present). */
  if (has_angles) view_angles.values = qa_mod_declaration_present(view_angles.values);
  if (has_axis) view_axis.values = qa_mod_declaration_present(view_axis.values);
  double cent_address = fint(&centities, "address", 0);
  double cent_stride = fint(&centities, "stride", 1);
  double cent_capacity = fint(&centities, "capacity", 1);
  double cent_state = fint(&centities, "state", 0);
  if (strcmp(runtime, "qvm-scene") == 0) {
    decl->is_scene = true;
    qa_qvm_scene_presentation *scene =
        (qa_qvm_scene_presentation *)xalloc(reader, 1, sizeof *scene);
    scene->base = base;
    scene->runtime = runtime;
    {
      qa_save_reader list = field(reader, "cvars");
      size_t count = qa_save_reader_list_count(&list);
      qa_qvm_presentation_cvar *rows =
          (qa_qvm_presentation_cvar *)xalloc(reader, count, sizeof *rows);
      for (size_t i = 0; i < count; i++) {
        qa_save_reader entry = item(&list, i);
        rows[i].name = fstr(&entry, "name");
        rows[i].value = fstr(&entry, "value");
        qa_save_reader_destroy(&entry);
      }
      qa_save_reader_destroy(&list);
      scene->cvars = rows;
      scene->cvar_count = count;
    }
    scene->storage.game_state = game_state;
    scene->storage.time = time;
    scene->storage.frame_time = frame_time;
    scene->storage.view_origin = view_origin;
    scene->storage.view_angles = view_angles;
    scene->storage.view_axis = view_axis;
    scene->storage.server_command_sequence = fint(&storage, "serverCommandSequence", 0);
    scene->storage.centities.address = cent_address;
    scene->storage.centities.stride = cent_stride;
    scene->storage.centities.capacity = cent_capacity;
    scene->storage.centities.state = cent_state;
    scene->storage.centities.previous_event = fint(&centities, "previousEvent", 0);
    scene->storage.centities.snapshot_time = fint(&centities, "snapshotTime", 0);
    scene->snapshots = read_calls(reader, "snapshots", &scene->snapshot_count);
    scene->event_entity_type = fint(reader, "eventEntityType", 0);
    {
      qa_save_reader check = field(reader, "eventCheck");
      scene->event_check.entry = fint(&check, "entry", 0);
      scene->event_check.centity_argument = fint(&check, "centityArgument", 0);
      qa_save_reader_destroy(&check);
    }
    {
      qa_save_reader body = field(reader, "body");
      qa_save_reader player = field(&body, "player");
      scene->body.player.entry = fint(&player, "entry", 0);
      scene->body.player.centity_argument = fint(&player, "centityArgument", 0);
      qa_save_reader_destroy(&player);
      qa_save_reader mesh = field(&body, "mesh");
      scene->body.mesh.entry = fint(&mesh, "entry", 0);
      scene->body.mesh.entity_argument = fint(&mesh, "entityArgument", 0);
      scene->body.mesh.state_argument = fint(&mesh, "stateArgument", 0);
      scene->body.mesh.shader_offset = fint(&mesh, "shaderOffset", 0);
      if (fpresent(&mesh, "parts")) {
        qa_save_reader list = field(&mesh, "parts");
        size_t count = qa_save_reader_list_count(&list);
        qa_qvm_body_part_call *rows =
            (qa_qvm_body_part_call *)xalloc(reader, count, sizeof *rows);
        for (size_t i = 0; i < count; i++) {
          qa_save_reader entry = item(&list, i);
          rows[i].call = fint(&entry, "call", 0);
          rows[i].part = fchoice(&entry, "part", parts, 4);
          qa_save_reader_destroy(&entry);
        }
        qa_save_reader_destroy(&list);
        scene->body.mesh.parts = qa_mod_declaration_present(rows);
        scene->body.mesh.part_count = count;
      }
      qa_save_reader_destroy(&mesh);
      qa_save_reader_destroy(&body);
    }
    decl->as.scene = scene;
  } else {
    qa_qvm_player_event_presentation *events =
        (qa_qvm_player_event_presentation *)xalloc(reader, 1, sizeof *events);
    events->base = base;
    events->runtime = runtime;
    events->storage.game_state = game_state;
    events->storage.player_state = fint(&storage, "playerState", 0);
    {
      qa_save_reader snapshot = field(&storage, "snapshot");
      qa_save_reader f = field(&snapshot, "kind");
      (void)qa_save_reader_literal_string(&f, "synthetic-player-event");
      qa_save_reader_destroy(&f);
      events->storage.snapshot.address = fint(&snapshot, "address", 0);
      events->storage.snapshot.pointers = read_int_list(&snapshot, "pointers");
      qa_save_reader_destroy(&snapshot);
    }
    events->storage.centities.address = cent_address;
    events->storage.centities.stride = cent_stride;
    events->storage.centities.capacity = cent_capacity;
    events->storage.centities.state = cent_state;
    events->storage.centities.origin = fint(&centities, "origin", 0);
    events->storage.time = time;
    events->storage.frame_time = frame_time;
    events->storage.view_origin = view_origin;
    events->storage.view_angles = view_angles;
    events->storage.view_axis = view_axis;
    events->project = read_calls(reader, "project", &events->project_count);
    qa_save_reader f = field(reader, "event");
    events->event = read_call(&f);
    qa_save_reader_destroy(&f);
    decl->as.player_events = events;
  }
  qa_save_reader_destroy(&centities);
  qa_save_reader_destroy(&storage);
  return decl;
}

qa_qvm_mod_presentation_declaration *qa_read_qvm_mod_presentation(
    const uint8_t *bytes, size_t length) {
  qa_save_value *value = qa_parse_json_value(bytes, length, false);
  if (value == NULL) return NULL;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "qvm-presentation")) return NULL;
  qa_qvm_mod_presentation_declaration *decl =
      qa_read_qvm_mod_presentation_declaration(&reader);
  qa_save_reader_destroy(&reader);
  return decl;
}
