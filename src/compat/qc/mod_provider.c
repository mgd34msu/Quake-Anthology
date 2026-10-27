/* Port of src/compat/qc/mod-provider.ts. GPL-2.0-or-later.
 * Unresolved (landed ModHostServices keeps engine scene/physics/
 * events opaque, and the session inventory/clients bind surfaces
 * the subsystems need are not part of qa_mod_host_services):
 * QcModActors spawn/remove/think scheduling, QcModMessages +
 * presentation/spatial/movement/pusher/aim host builtins,
 * QcModCombat/QcModProtection/QcModPickups/QcModItems binding
 * (their validators and store observers are landed and, for
 * items/combat/protection-regions, run from validateQcMod),
 * QcActorState source physics, water transition, and
 * clientPresentation()/presentations() (their TS owners
 * app/bootstrap/simulation are unported). Callers supply those
 * host builtins via extra_host; the callbacks below that only
 * those subsystems drive (pickup scopes) stay empty. */
#include "qa/compat/qc/mod_provider.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qc/mod_combat.h"
#include "qa/compat/qc/mod_commands.h"
#include "qa/compat/qc/mod_items.h"
#include "qa/compat/qc/mod_objectives.h"
#include "qa/compat/qc/mod_protection.h"
#include "qa/compat/qc/profile.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/value.h"
#include "qa/world/scheduler.h"

typedef qa_compat_qc_mod_provider P;
static bool actor_eq(qa_actor_id a, qa_actor_id b) {
  return a.slot == b.slot && a.generation == b.generation &&
         a.token == b.token && a.session.token == b.session.token;
}
static bool text_eq(const char *a, const char *b) {
  return a != NULL && b != NULL && strcmp(a, b) == 0;
}
static double seconds_of(qa_source_time time) {
  return time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000;
}
static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size);
  if (!next) qa_range_error("mod provider allocation failed");
  return next;
}
static const qa_mod_actor_field *actor_field_named(
    qa_mod_callback_declaration decl, const char *name) {
  if (!name) return NULL;
  for (size_t i = 0; i < decl.actor_field_count; i++)
    if (strcmp(decl.actor_fields[i].field, name) == 0)
      return &decl.actor_fields[i];
  return NULL;
}

/* ---------------- validateQcMod ---------------- */
typedef struct {
  qa_compat_qc_program *program;
  qa_mod_callback_declaration decl;
} OutCtx;
static void output_field(OutCtx *c, const char *name, bool vector) {
  const qa_compat_qc_definition *field =
      qa_compat_qc_program_field_named(c->program, name);
  const qa_mod_actor_field *binding = actor_field_named(c->decl, name);
  bool ok = field && text_eq(field->type, vector ? "vector" : "float") &&
            binding &&
            (text_eq(binding->binding, "private") ||
             (vector && (text_eq(binding->binding, "view-offset") ||
                         text_eq(binding->binding, "bounds-min") ||
                         text_eq(binding->binding, "bounds-max"))) ||
             (!vector && text_eq(binding->binding, "client-flags")));
  if (!ok)
    qa_range_error("QC client outputs require declared private fields or "
                   "the explicit view-offset field");
}
static void out_scalar(void *ctx, const void *field) {
  output_field(ctx, (const char *)field, false);
}
static void out_vector(void *ctx, const void *field) {
  output_field(ctx, (const char *)field, true);
}

static bool pickup_storage_ok(qa_mod_callback_declaration decl,
                              const qa_pickup_write *resource) {
  if (resource->kind == QA_PICKUP_RESOURCE_PROTECTION) {
    const char *channel =
        resource->channel == QA_PROTECTION_REGULAR ? "regular" : "powered";
    for (size_t j = 0; j < decl.protection_count; j++)
      if (text_eq(decl.protection[j].channel, channel)) return true;
    return false;
  }
  bool count = text_eq(resource->fields, "count");
  if (count)
    for (size_t j = 0; j < decl.actor_field_count; j++)
      if (text_eq(decl.actor_fields[j].binding, "inventory") &&
          text_eq(decl.actor_fields[j].item, resource->item))
        return true;
  if (decl.items)
    for (size_t j = 0; j < decl.items->storage_count; j++) {
      const qa_mod_qc_item_storage *st = &decl.items->storage[j];
      if (text_eq(st->kind, "bits")) {
        if (!count) continue;
        for (size_t k = 0; k < st->item_count; k++)
          if (text_eq(st->items[k].item, resource->item)) return true;
      } else if (text_eq(st->item, resource->item) &&
                 (count || !st->capacity.is_constant))
        return true;
    }
  return false;
}

void qa_compat_qc_validate_mod(qa_compat_qc_program *program,
                               qa_mod_callback_declaration decl) {
  if (!decl.program.digest ||
      strcmp(program->digest, decl.program.digest) != 0)
    qa_range_error(
        "Gameplay mod program differs from its declared artifact digest");
  const qa_mod_client_output_declaration *outputs =
      decl.clients ? decl.clients->outputs : NULL;
  size_t output_count = decl.clients ? decl.clients->output_count : 0;
  OutCtx oc = {program, decl};
  qa_validate_mod_client_outputs(outputs, output_count, out_scalar,
                                 out_vector, &oc);
  for (size_t i = 0; i < output_count; i++) {
    const qa_mod_client_output_declaration *output = &outputs[i];
    if (output->kind == QA_MOD_CLIENT_OUTPUT_BODY_SHAPE) {
      const qa_mod_actor_field *mn =
          actor_field_named(decl, (const char *)output->min_vector);
      const qa_mod_actor_field *mx =
          actor_field_named(decl, (const char *)output->max_vector);
      if (!mn || !text_eq(mn->binding, "bounds-min") || !mx ||
          !text_eq(mx->binding, "bounds-max"))
        qa_range_error("Client body shape must name its declared source "
                       "mins/maxs");
      continue;
    }
    const char *field_name = output->height
                                 ? (const char *)output->height
                                 : (const char *)output->field;
    const qa_mod_actor_field *binding = actor_field_named(decl, field_name);
    if (binding && text_eq(binding->binding, "client-flags") &&
        (output->kind == QA_MOD_CLIENT_OUTPUT_VIEW_OFFSET ||
         !output->has_mask ||
         (((int64_t)output->mask) &
          ~((int64_t)(binding->has_private_mask ? binding->private_mask
                                                : 0))) != 0))
      qa_range_error("QC client flag outputs must name an explicit mask of "
                     "source-private bits");
  }
  size_t region_count = 0;
  qa_q1qc_armor_stage **regions =
      qa_compat_qc_protection_regions(program, decl, &region_count);
  qa_compat_qc_protection_regions_free(regions, region_count);
  qa_compat_qc_validate_items(program, decl);
  qa_mod_callback_input self_time[2] = {"self", "time"};
  if (decl.items && decl.items->weapons) {
    qa_compat_qc_validate_source_call(program,
                                      decl.items->weapons->select_call,
                                      self_time, 2, "source weapon");
    for (size_t i = 0; i < decl.items->weapons->resume_count; i++)
      qa_compat_qc_validate_source_call(program,
                                        decl.items->weapons->resume[i],
                                        self_time, 2, "source weapon");
  }
  if (decl.items)
    for (size_t i = 0; i < decl.items->definition_count; i++) {
      if (decl.items->definitions[i].action_use)
        qa_compat_qc_validate_source_call(
            program, *decl.items->definitions[i].action_use, self_time, 2,
            "source item action");
      if (decl.items->definitions[i].action_drop)
        qa_compat_qc_validate_source_call(
            program, *decl.items->definitions[i].action_drop, self_time, 2,
            "source item action");
    }
  for (size_t i = 0; i < decl.protection_count; i++) {
    qa_mod_callback_input avail[11] = {
        "self", "attacker", "inflictor", "amount", "knockback",
        "damage-flags", "regular-protection-scale", "direction", "point",
        "normal", "time"};
    qa_compat_qc_validate_source_call(program, decl.protection[i].absorb_call,
                                      avail, 11, "protection");
  }
  for (size_t pi = 0; pi < decl.pickup_count; pi++) {
    const qa_mod_pickup_rule *pickup = &decl.pickups[pi];
    bool bad = !decl.clients || !pickup->id[0] || pickup->offered_count == 0;
    for (size_t j = 0; j < pi; j++)
      if (strcmp(decl.pickups[j].id, pickup->id) == 0) bad = true;
    for (size_t j = 0; j < pickup->offered_count; j++) {
      for (size_t k = j + 1; k < pickup->offered_count; k++)
        if (strcmp(pickup->offered[j], pickup->offered[k]) == 0) bad = true;
      for (size_t q = 0; q < pi; q++)
        for (size_t k = 0; k < decl.pickups[q].offered_count; k++)
          if (strcmp(decl.pickups[q].offered[k], pickup->offered[j]) == 0)
            bad = true;
    }
    if (bad)
      qa_range_error("QC pickups require clients, unique rule ids and "
                     "distinct offered items");
    for (size_t wi = 0; wi < pickup->writes.count; wi++)
      if (!pickup_storage_ok(decl, &pickup->writes.values[wi]))
        qa_range_error("QC pickup resource requires its declared source "
                       "storage");
    qa_mod_callback_input avail[7] = {"self", "other", "item", "time",
                                      "pickup-count", "pickup-has-count",
                                      "pickup-dropped"};
    if (pickup->operation.is_gate_then_grant)
      qa_compat_qc_validate_source_call(
          program, *(const qa_mod_source_call *)pickup->operation.gate,
          avail, 7, "pickup");
    qa_compat_qc_validate_source_call(
        program, *(const qa_mod_source_call *)pickup->operation.grant, avail,
        7, "pickup");
  }
  for (int bi = 0; bi < 2; bi++) {
    const char *binding = bi ? "score" : "team";
    size_t n = 0;
    for (size_t i = 0; i < decl.actor_field_count; i++)
      if (text_eq(decl.actor_fields[i].binding, binding)) n++;
    if (n > 1)
      qa_range_error("Duplicate original %s authority field", binding);
  }
  size_t think_n = 0, nextthink_n = 0;
  for (size_t i = 0; i < decl.actor_field_count; i++) {
    if (text_eq(decl.actor_fields[i].binding, "think")) think_n++;
    if (text_eq(decl.actor_fields[i].binding, "nextthink")) nextthink_n++;
  }
  if (think_n != nextthink_n || think_n > 1)
    qa_range_error("Mod source scheduling requires one think and one "
                   "nextthink binding together");
  int64_t *used_words = NULL;
  size_t used_count = 0, used_capacity = 0;
  for (size_t i = 0; i < decl.actor_field_count; i++) {
    const qa_mod_actor_field *entry = &decl.actor_fields[i];
    if (text_eq(entry->binding, "team") || text_eq(entry->binding, "score"))
      qa_validate_source_match_field(
          (qa_source_match_field){text_eq(entry->binding, "score"),
                                  entry->team_values, entry->team_value_count});
    if (text_eq(entry->binding, "client-input") &&
        (!decl.clients || decl.clients->input_count == 0 ||
         (text_eq(entry->update, "nonzero") &&
          text_eq(entry->input, "view-angles"))))
      qa_range_error("Mod client input fields require declared applications "
                     "and scalar nonzero updates");
    if (text_eq(entry->binding, "client-flags") && entry->has_private_mask &&
        (entry->private_mask != trunc(entry->private_mask) ||
         entry->private_mask < 0 || entry->private_mask > 0x7fffff ||
         (((int64_t)entry->private_mask) &
          (8 | 128 | (entry->has_grounded ? 512 : 0))) != 0))
      qa_range_error("Mod private client flags overlap canonical flags or "
                     "exceed the source flag word");
    if (text_eq(entry->binding, "userinfo") &&
        (!decl.clients || !entry->key || !entry->key[0] ||
         strchr(entry->key, '\\')))
      qa_range_error("Mod userinfo field requires a declared client and "
                     "valid info key");
    if (text_eq(entry->binding, "client-input") && entry->has_scale &&
        (!isfinite(entry->scale) || entry->scale == 0 ||
         text_eq(entry->input, "view-angles")))
      qa_range_error("QC input scale requires a finite nonzero scalar "
                     "encoding");
    const qa_compat_qc_definition *field =
        qa_compat_qc_program_field_named(program, entry->field);
    if (!field)
      qa_range_error("Missing mod actor field %s", entry->field);
    const char *type;
    if (text_eq(entry->binding, "private"))
      type = field->type;
    else if (text_eq(entry->binding, "constant"))
      type = entry->constant_value.kind;
    else if (text_eq(entry->binding, "client-input"))
      type = text_eq(entry->input, "view-angles") ? "vector" : "float";
    else if (text_eq(entry->binding, "classname") ||
             text_eq(entry->binding, "userinfo"))
      type = "string";
    else if (text_eq(entry->binding, "think"))
      type = "function";
    else if (text_eq(entry->binding, "team") ||
             text_eq(entry->binding, "score") ||
             text_eq(entry->binding, "health") ||
             text_eq(entry->binding, "inventory") ||
             text_eq(entry->binding, "nextthink") ||
             text_eq(entry->binding, "client-flags"))
      type = "float";
    else
      type = "vector";
    if (!text_eq(field->type, type))
      qa_range_error("Mod actor field %s requires %s, found %s", entry->field,
                     type, field->type);
    int64_t width = strcmp(type, "vector") == 0 ? 3 : 1;
    for (int64_t w = field->offset; w < field->offset + width; w++) {
      for (size_t u = 0; u < used_count; u++)
        if (used_words[u] == w)
          qa_range_error("Overlapping mod actor field %s", entry->field);
      if (used_count == used_capacity) {
        used_capacity = used_capacity ? used_capacity * 2 : 16;
        used_words = xrealloc(used_words, used_capacity * sizeof *used_words);
      }
      used_words[used_count++] = w;
    }
  }
  free(used_words);
  if (decl.client_presentation) {
    if (!decl.clients)
      qa_range_error("QC client presentation requires declared clients");
    if (!text_eq(decl.client_presentation->hud, "none"))
      for (int i = 0; i < 2; i++) {
        const char *n = i ? "armorvalue" : "health";
        const qa_compat_qc_definition *f =
            qa_compat_qc_program_field_named(program, n);
        if (!f || !text_eq(f->type, "float"))
          qa_range_error("QC status requires original %s", n);
      }
    if (!text_eq(decl.client_presentation->view, "none"))
      for (int i = 0; i < 3; i++) {
        const char *n =
            (const char *const[]){"origin", "angles", "view_ofs"}[i];
        const qa_compat_qc_definition *f =
            qa_compat_qc_program_field_named(program, n);
        if (!f || !text_eq(f->type, "vector"))
          qa_range_error("QC view requires original %s", n);
      }
  }
  if (decl.clients) {
    double maximum = decl.clients->maximum;
    if (maximum != trunc(maximum) || maximum < 1 || maximum >= 8191)
      qa_range_error("Mod client capacity must fit reserved QuakeC edicts");
    for (size_t i = 0; i < decl.clients->admit_count; i++)
      qa_compat_qc_validate_source_call(program, decl.clients->admit[i],
                                        self_time, 2, "client lifecycle");
    for (size_t i = 0; i < decl.clients->userinfo_count; i++)
      qa_compat_qc_validate_source_call(program, decl.clients->userinfo[i],
                                        self_time, 2, "client lifecycle");
    for (size_t i = 0; i < decl.clients->disconnect_count; i++)
      qa_compat_qc_validate_source_call(program, decl.clients->disconnect[i],
                                        self_time, 2, "client lifecycle");
    qa_mod_callback_input frame_avail[3] = {"self", "time", "elapsed"};
    for (size_t i = 0; i < decl.clients->frame_count; i++)
      qa_compat_qc_validate_source_call(program, decl.clients->frame[i],
                                        frame_avail, 3, "client frame");
    for (size_t bi = 0; bi < decl.clients->input_count; bi++) {
      const qa_mod_client_input_binding *binding = &decl.clients->input[bi];
      if (text_eq(binding->phase, "before")) {
        const qa_mod_qc_input_output *outs =
            (const qa_mod_qc_input_output *)binding->outputs;
        for (size_t oi = 0; oi < binding->output_count; oi++) {
          for (size_t oj = 0; oj < oi; oj++)
            if (outs[oj].is_handler == outs[oi].is_handler &&
                text_eq(outs[oi].is_handler ? outs[oi].function
                                            : outs[oi].field,
                        outs[oj].is_handler ? outs[oj].function
                                            : outs[oj].field))
              qa_range_error("QC input output declaration is duplicated");
          if (!outs[oi].is_handler) {
            bool stored = false;
            for (size_t j = 0; j < decl.actor_field_count; j++)
              if (text_eq(decl.actor_fields[j].binding, "client-input") &&
                  text_eq(decl.actor_fields[j].field, outs[oi].field))
                stored = true;
            if (!stored)
              qa_range_error(
                  "QC output requires declared client input storage");
          } else {
            const qa_compat_qc_function *fn =
                qa_compat_qc_program_function_named_map(program,
                                                        outs[oi].function);
            const qa_compat_qc_definition *self =
                qa_compat_qc_program_global_named(program, "self");
            bool distinct = outs[oi].input_count != 0;
            for (size_t a = 0; a < outs[oi].input_count; a++)
              for (size_t b = a + 1; b < outs[oi].input_count; b++)
                if (strcmp(outs[oi].inputs[a], outs[oi].inputs[b]) == 0)
                  distinct = false;
            if (!fn || fn->index == 0 || fn->first_statement <= 0 ||
                fn->named_builtin || !self || !text_eq(self->type, "entity") ||
                !distinct)
              qa_range_error("QC output requires an original actor handler "
                             "and distinct controls");
          }
        }
      }
    }
    for (size_t bi = 0; bi < decl.clients->input_count; bi++) {
      const qa_mod_client_input_binding *binding = &decl.clients->input[bi];
      qa_mod_callback_input avail[10] = {
          "self", "time", "elapsed", "view-angles", "attack", "jump",
          "impulse", "forward-move", "side-move", "up-move"};
      const qa_mod_source_call *calls =
          (const qa_mod_source_call *)binding->calls;
      for (size_t ci = 0; ci < binding->call_count; ci++)
        qa_compat_qc_validate_source_call(program, calls[ci], avail, 10,
                                          "client input");
    }
  }
  for (size_t i = 0; i < decl.objective_count; i++) {
    const qa_source_objective_declaration *objective = &decl.objectives[i];
    qa_compat_qc_validate_objective_storage(
        program, *(const qa_qc_mod_objective_storage *)objective->state_storage,
        "float");
    if (objective->carrier)
      qa_compat_qc_validate_objective_storage(
          program, *(const qa_qc_mod_objective_storage *)objective->carrier,
          "entity");
    if (objective->target)
      qa_compat_qc_validate_objective_storage(
          program, *(const qa_qc_mod_objective_storage *)objective->target,
          "entity");
    if (objective->is_owned && objective->change) {
      qa_mod_callback_input avail[5] = {"self", "other", "activator", "amount",
                                        "time"};
      qa_compat_qc_validate_source_call(
          program, *(const qa_mod_source_call *)objective->change, avail, 5,
          "objective change");
    }
  }
  for (size_t i = 0; i < decl.command_count; i++) {
    const qa_mod_console_command *command = &decl.commands[i];
    char *folded = qa_ascii_fold(command->name);
    if (!folded) qa_range_error("mod command allocation failed");
    bool duplicate = false;
    for (size_t j = 0; j < i; j++) {
      char *other = qa_ascii_fold(decl.commands[j].name);
      if (!other) qa_range_error("mod command allocation failed");
      if (strcmp(other, folded) == 0) duplicate = true;
      free(other);
    }
    qa_command_tokens tok;
    qa_command_dialect dialect = program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD
                                     ? QA_COMMAND_DIALECT_Q1_QUAKEWORLD
                                     : QA_COMMAND_DIALECT_Q1_NETQUAKE;
    if (!qa_tokenize_command(command->name, dialect, QA_COMMAND_TEXT_SOURCE,
                             &tok))
      qa_range_error("mod command allocation failed");
    bool bad = tok.argc != 1 || strcmp(tok.argv[0], command->name) != 0 ||
               strchr(command->name, ';') != NULL || duplicate;
    qa_command_tokens_destroy(&tok);
    free(folded);
    if (bad)
      qa_range_error("Invalid or duplicate mod command %s", command->name);
    qa_mod_source_call call;
    qa_mod_callback_value *args = NULL;
    qa_mod_named_value *globals = NULL;
    if (!qa_compat_qc_console_call(*command, NULL, 0, "", &call, &args,
                                   &globals))
      qa_range_error("mod command allocation failed");
    char label[256];
    snprintf(label, sizeof label, "console command %s", command->name);
    qa_compat_qc_validate_source_call(program, call, NULL, 0, label);
    qa_compat_qc_console_call_destroy(args, globals);
  }
  for (size_t i = 0; i < decl.initialize_count; i++)
    qa_compat_qc_validate_source_call(program, decl.initialize[i], self_time, 2,
                                      "initialization");
  if (decl.frame) {
    qa_mod_callback_input avail[3] = {"self", "time", "elapsed"};
    qa_compat_qc_validate_source_call(program, *decl.frame, avail, 3,
                                      "source frame");
  }
  for (size_t i = 0; i < decl.cvar_count; i++)
    for (size_t j = i + 1; j < decl.cvar_count; j++)
      if (strcmp(decl.cvars[i].name, decl.cvars[j].name) == 0)
        qa_range_error("Duplicate mod cvar %s", decl.cvars[i].name);
  for (size_t i = 0; i < decl.callback_count; i++) {
    const qa_mod_callback *call = &decl.callbacks[i];
    for (size_t j = 0; j < i; j++)
      if (strcmp(decl.callbacks[j].binding.id, call->binding.id) == 0)
        qa_range_error("Duplicate mod callback %s", call->binding.id);
    qa_mod_callback_input avail[16];
    size_t an = 0;
    avail[an++] = "self";
    avail[an++] = "time";
    if (text_eq(call->binding.stage, "observe")) avail[an++] = "result";
    const char *op = call->binding.operation;
    const char *extra[7];
    size_t en = 0;
    if (text_eq(op, "damage")) {
      const char *e[] = {"attacker", "inflictor", "amount", "knockback",
                         "direction", "point", "normal"};
      memcpy(extra, e, sizeof e);
      en = 7;
    } else if (text_eq(op, "inventory.give") ||
               text_eq(op, "inventory.consume")) {
      extra[0] = "item";
      extra[1] = "amount";
      en = 2;
    } else if (text_eq(op, "actor.use")) {
      extra[0] = "other";
      extra[1] = "activator";
      en = 2;
    } else if (text_eq(op, "actor.touch")) {
      extra[0] = "other";
      en = 1;
    } else if (text_eq(op, "actor.think")) {
      extra[0] = "elapsed";
      en = 1;
    } else if (text_eq(op, "actor.pain")) {
      extra[0] = "attacker";
      extra[1] = "amount";
      extra[2] = "knockback";
      en = 3;
    } else {
      const char *e[] = {"attacker", "inflictor", "amount", "knockback",
                         "point"};
      memcpy(extra, e, sizeof e);
      en = 5;
    }
    for (size_t j = 0; j < en; j++) avail[an++] = extra[j];
    qa_compat_qc_validate_source_call(program, call->call, avail, an,
                                      call->binding.id);
  }
  if (decl.combat) qa_compat_qc_validate_mod_combat(program, *decl.combat);
}

/* ---------------- provider core ---------------- */
static int64_t projection_slot(P *p, qa_actor_id actor) {
  for (size_t i = 0; i < p->projection_count; i++)
    if (actor_eq(p->projections[i].actor, actor)) return p->projections[i].slot;
  return -1;
}
static void projection_set(P *p, qa_actor_id actor, int64_t slot) {
  int64_t existing = projection_slot(p, actor);
  if (existing >= 0) {
    for (size_t i = 0; i < p->projection_count; i++)
      if (actor_eq(p->projections[i].actor, actor)) p->projections[i].slot = slot;
    return;
  }
  if (p->projection_count == p->projection_capacity) {
    p->projection_capacity = p->projection_capacity ? p->projection_capacity * 2 : 16;
    p->projections = xrealloc(p->projections,
                              p->projection_capacity * sizeof *p->projections);
  }
  p->projections[p->projection_count].actor = actor;
  p->projections[p->projection_count].slot = slot;
  p->projection_count++;
}
static void projection_delete(P *p, qa_actor_id actor) {
  for (size_t i = 0; i < p->projection_count; i++)
    if (actor_eq(p->projections[i].actor, actor)) {
      memmove(&p->projections[i], &p->projections[i + 1],
              (p->projection_count - i - 1) * sizeof *p->projections);
      p->projection_count--;
      return;
    }
}
static bool projection_actor_at(P *p, int64_t slot, qa_actor_id *out) {
  for (size_t i = 0; i < p->projection_count; i++)
    if (p->projections[i].slot == slot) { *out = p->projections[i].actor; return true; }
  return false;
}
static void retired_add(P *p, qa_actor_id actor) {
  for (size_t i = 0; i < p->retired_count; i++)
    if (actor_eq(p->retired[i], actor)) return;
  if (p->retired_count == p->retired_capacity) {
    p->retired_capacity = p->retired_capacity ? p->retired_capacity * 2 : 8;
    p->retired = xrealloc(p->retired, p->retired_capacity * sizeof *p->retired);
  }
  p->retired[p->retired_count++] = actor;
}
static int64_t reference_actor(void *ctx, const qa_actor_id *actor) {
  return qa_compat_qc_mod_provider_reference(ctx, actor);
}
static bool world_actor(P *p, qa_actor_id *out) {
  const qa_session_mod_engine_services *engine = p->services->engine;
  return engine && engine->world && engine->world(engine->ctx, out);
}
qa_actor_id qa_compat_qc_mod_provider_actor(P *p, int64_t reference) {
  if (reference == 0) {
    qa_actor_id world;
    if (!world_actor(p, &world) ||
        !qa_session_actor_registry_is_live(p->services->actors, world))
      qa_range_error("Mod source world has no canonical actor");
    return world;
  }
  qa_actor_id actor;
  int64_t slot = qa_compat_qc_entity_memory_slot(&p->entities, reference);
  if (!projection_actor_at(p, slot, &actor) ||
      !qa_session_actor_registry_is_live(p->services->actors, actor))
    qa_range_error(
        "Gameplay mod references an absent or expired actor projection");
  return actor;
}
int64_t qa_compat_qc_mod_provider_reference(P *p, const qa_actor_id *actor) {
  if (!actor) return 0;
  qa_actor_id world;
  if (world_actor(p, &world) && actor_eq(*actor, world)) return 0;
  qa_owned_actor owned;
  if (!qa_session_actor_registry_resolve_owned(p->services->actors, *actor,
                                               &owned))
    qa_range_error("Gameplay mod cannot project a stale actor");
  qa_actor_id current = owned.id;
  int64_t existing = projection_slot(p, current);
  if (existing >= 0)
    return qa_compat_qc_entity_memory_reference(&p->entities, existing);
  int64_t client_slot = -1;
  if (p->has_clients &&
      !qa_compat_qc_mod_client_bindings_slot(&p->clients, current,
                                             &client_slot))
    client_slot = -1;
  int64_t slot = client_slot >= 0 ? client_slot : p->entities.count;
  if (client_slot < 0)
    qa_compat_qc_entity_memory_set_count(&p->entities, slot + 1);
  else {
    qa_compat_qc_words *words = qa_compat_qc_entity_memory_at(&p->entities, slot);
    memset(words->bytes, 0, words->byte_length);
  }
  projection_set(p, current, slot);
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_at(&p->entities, slot);
  for (size_t i = 0; i < p->field_count; i++) {
    const qa_compat_qc_mod_provider_field *field = &p->fields[i];
    if (!text_eq(field->declaration.binding, "constant")) continue;
    qa_mod_runtime_value value;
    memset(&value, 0, sizeof value);
    value.kind = field->declaration.constant_value.kind;
    value.number_value = field->declaration.constant_value.number_value;
    value.string_value = field->declaration.constant_value.string_value;
    value.vector_value = field->declaration.constant_value.vector_value;
    qa_compat_qc_write_source_value(
        &p->machine, words, field->offset, value,
        (qa_compat_qc_actor_reference){p, reference_actor});
  }
  if (client_slot >= 0) {
    qa_body_state_result body =
        qa_shared_body_table_read(p->services->bodies, current);
    if (body.has_value)
      for (size_t i = 0; i < p->field_count; i++) {
        const qa_compat_qc_mod_provider_field *field = &p->fields[i];
        if (text_eq(field->declaration.binding, "bounds-min"))
          qa_compat_qc_words_set_vector(words, field->offset,
                                        body.value.bounds.min);
        else if (text_eq(field->declaration.binding, "bounds-max"))
          qa_compat_qc_words_set_vector(words, field->offset,
                                        body.value.bounds.max);
      }
  }
  return qa_compat_qc_entity_memory_reference(&p->entities, slot);
}
bool qa_compat_qc_mod_provider_lookup(P *p, const char *kind, const char *name,
                                      qa_compat_qc_precached_resource *out) {
  size_t key_length = strlen(kind) + 1 + strlen(name) + 1;
  char *key = xrealloc(NULL, key_length);
  snprintf(key, key_length, "%s:%s", kind, name);
  for (size_t i = 0; i < p->precached_count; i++)
    if (strcmp(p->precached[i].key, key) == 0) {
      *out = p->precached[i].value;
      free(key);
      return true;
    }
  const qa_compat_qc_mod_media_resource *resource = NULL;
  if (p->media)
    for (size_t i = 0; i < p->media->resource_count; i++)
      if (strcmp(p->media->resources[i].name, name) == 0)
        resource = &p->media->resources[i];
  if (!resource) {
    free(key);
    return false;
  }
  double index = 1;
  for (size_t i = 0; i < p->precached_count; i++)
    if (strncmp(p->precached[i].key, kind, strlen(kind)) == 0 &&
        p->precached[i].key[strlen(kind)] == ':')
      index += 1;
  qa_compat_qc_precached_resource value = {index, resource->resource};
  if (p->precached_count == p->precached_capacity) {
    p->precached_capacity = p->precached_capacity ? p->precached_capacity * 2 : 8;
    p->precached = xrealloc(p->precached,
                            p->precached_capacity * sizeof *p->precached);
  }
  p->precached[p->precached_count].key = key;
  p->precached[p->precached_count].value = value;
  p->precached_count++;
  *out = value;
  return true;
}
static qa_source_match_field match_field(const qa_mod_actor_field *declared) {
  return (qa_source_match_field){text_eq(declared->binding, "score"),
                                 declared->team_values,
                                 declared->team_value_count};
}
static bool output_field_owned(P *p, qa_actor_id actor, const char *field) {
  if (!p->has_clients ||
      !qa_compat_qc_mod_client_bindings_admitted(&p->clients, actor))
    return false;
  if (!p->declaration.clients) return false;
  const qa_mod_client_output_declaration *outputs =
      p->declaration.clients->outputs;
  for (size_t i = 0; i < p->declaration.clients->output_count; i++) {
    const qa_mod_client_output_declaration *output = &outputs[i];
    if (output->kind == QA_MOD_CLIENT_OUTPUT_BODY_SHAPE) {
      if (text_eq((const char *)output->min_vector, field) ||
          text_eq((const char *)output->max_vector, field))
        return true;
    } else if (output->kind == QA_MOD_CLIENT_OUTPUT_VIEW_OFFSET &&
               output->field &&
               text_eq((const char *)output->field, field))
      return true;
  }
  return false;
}
static bool client_slot_of(P *p, qa_actor_id actor, int64_t *out) {
  return p->has_clients &&
         qa_compat_qc_mod_client_bindings_slot(&p->clients, actor, out);
}
static void refresh(P *p, qa_actor_id actor, int64_t slot, int64_t word,
                    int64_t count) {
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_at(&p->entities, slot);
  for (size_t i = 0; i < p->field_count; i++) {
    const qa_compat_qc_mod_provider_field *field = &p->fields[i];
    if (word + count <= field->offset || word >= field->offset + field->words)
      continue;
    const qa_mod_actor_field *declared = &field->declaration;
    const char *binding = declared->binding;
    if (text_eq(binding, "constant") || text_eq(binding, "private") ||
        text_eq(binding, "client-input") || text_eq(binding, "think") ||
        text_eq(binding, "nextthink")) {
      continue;
    } else if (text_eq(binding, "userinfo")) {
      if (!p->has_clients)
        qa_range_error("Mod userinfo field requires client services");
      int64_t client_slot;
      if (!client_slot_of(p, actor, &client_slot)) continue;
      char *value = qa_compat_qc_mod_client_bindings_userinfo(&p->clients,
                                                              actor,
                                                              declared->key);
      if (!value) qa_range_error("mod provider allocation failed");
      char engine_name[256];
      snprintf(engine_name, sizeof engine_name, "mod-userinfo:%lld:%s",
               (long long)slot, declared->key);
      int64_t capacity = (int64_t)strlen(value) + 1;
      if (capacity < 128) capacity = 128;
      qa_compat_qc_words_set_int(
          words, field->offset,
          (double)qa_compat_qc_strings_set_engine(&p->machine.strings,
                                                  engine_name, value,
                                                  capacity));
      free(value);
    } else if (text_eq(binding, "classname")) {
      const qa_session_mod_engine_services *engine = p->services->engine;
      const char *name =
          engine && engine->classname ? engine->classname(engine->ctx, actor)
                                      : NULL;
      if (!name)
        qa_range_error(
            "Mod classname field requires canonical actor metadata");
      char engine_name[256];
      snprintf(engine_name, sizeof engine_name, "mod-classname:%s", name);
      int64_t capacity = (int64_t)strlen(name) + 1;
      if (capacity < 128) capacity = 128;
      qa_compat_qc_words_set_int(
          words, field->offset,
          (double)qa_compat_qc_strings_set_engine(&p->machine.strings,
                                                  engine_name, name,
                                                  capacity));
    } else if (text_eq(binding, "client-flags")) {
      qa_q1_visibility_client client;
      bool has_client = p->environment.clients &&
                        qa_compat_qc_mod_environment_client(&p->environment,
                                                            actor, &client);
      int64_t canonical = has_client ? 8 | (client.notarget ? 128 : 0) : 0;
      if (has_client && declared->has_grounded) {
        qa_client_id identity;
        qa_mod_client_services *clients = p->services->clients;
        if (!clients || !clients->for_actor ||
            !clients->for_actor(clients->ctx, actor, &identity) ||
            !clients->grounded)
          qa_range_error("Mod grounded flags require canonical client "
                         "movement state");
        if (clients->grounded(clients->ctx, identity)) canonical |= 512;
      }
      int64_t private_mask =
          declared->has_private_mask ? (int64_t)declared->private_mask : 0;
      qa_compat_qc_words_set_float(
          words, field->offset,
          (double)(canonical | ((int64_t)trunc(qa_compat_qc_words_float(
                                    words, field->offset)) &
                                private_mask)));
    } else if (text_eq(binding, "view-offset")) {
      if (output_field_owned(p, actor, declared->field)) continue;
      qa_q1_visibility_client client;
      bool has_client = p->environment.clients &&
                        qa_compat_qc_mod_environment_client(&p->environment,
                                                            actor, &client);
      qa_compat_qc_words_set_vector(
          words, field->offset,
          has_client ? client.view_offset : (qa_vec3){0, 0, 0});
    } else if (text_eq(binding, "team") || text_eq(binding, "score")) {
      qa_compat_qc_words_set_float(
          words, field->offset,
          qa_read_source_match_field(p->services->match, actor,
                                     match_field(declared)));
    } else if (text_eq(binding, "health")) {
      qa_combat_state_result state =
          qa_gameplay_authority_read(p->services->combat, actor);
      qa_compat_qc_words_set_float(words, field->offset,
                                   state.has_value ? state.value.health : 0);
    } else if (text_eq(binding, "inventory")) {
      size_t entry_count = 0;
      const qa_inventory_entry *entries = qa_shared_inventory_table_entries(
          p->services->inventory, actor, &entry_count);
      double value = 0;
      for (size_t e = 0; e < entry_count; e++)
        if (text_eq(entries[e].item, declared->item)) value = entries[e].count;
      qa_compat_qc_words_set_float(words, field->offset, value);
    } else {
      /* origin | angles | velocity | bounds-min | bounds-max */
      if (output_field_owned(p, actor, declared->field)) continue;
      qa_body_state_result body =
          qa_shared_body_table_read(p->services->bodies, actor);
      qa_vec3 value = {0, 0, 0};
      if (body.has_value) {
        if (text_eq(binding, "bounds-min"))
          value = body.value.bounds.min;
        else if (text_eq(binding, "bounds-max"))
          value = body.value.bounds.max;
        else if (text_eq(binding, "origin"))
          value = body.value.origin;
        else if (text_eq(binding, "angles"))
          value = body.value.angles;
        else
          value = body.value.velocity;
      }
      qa_compat_qc_words_set_vector(words, field->offset, value);
    }
  }
}
static void publish_client_outputs(P *p) {
  if (!p->client_outputs ||
      !qa_source_mod_client_outputs_enabled(p->client_outputs))
    return;
  if (p->closed || !p->has_clients) return;
  for (size_t i = 0; i < p->clients.entry_count; i++) {
    qa_actor_id actor = p->clients.entries[i].actor;
    if (p->clients.entries[i].admitted &&
        qa_source_mod_client_outputs_has(p->client_outputs, actor) &&
        qa_session_actor_registry_is_live(p->services->actors, actor))
      qa_source_mod_client_outputs_publish(p->client_outputs, actor);
  }
}
typedef struct {
  qa_compat_qc_words *words;
  int64_t offset;
} PickupCommit;
static void pickup_committed(void *ctx, qa_inventory_committed_change change) {
  PickupCommit *commit = ctx;
  qa_compat_qc_words_set_float(commit->words, commit->offset,
                               change.after.count);
}
static bool pickup_scope(P *p, size_t *out_index) {
  if (p->pickup_scope_count == 0) return false;
  size_t index = p->pickup_scope_count - 1;
  if (p->pickup_scopes[index].depth != p->depth) return false;
  *out_index = index;
  return true;
}
static void validate_pickup_store(P *p,
                                  qa_compat_qc_entity_store_observation store) {
  size_t scope_index;
  if (!pickup_scope(p, &scope_index)) return;
  qa_original_pickup_execution *execution =
      &p->pickup_scopes[scope_index].execution;
  if (!execution->current(execution->current_ctx))
    qa_range_error("Original pickup resource binding is no longer current");
  qa_actor_id actor = qa_compat_qc_mod_provider_actor(p, store.reference);
  int64_t store_words = (int64_t)(store.after_length / 4);
  for (size_t i = 0; i < p->field_count; i++) {
    const qa_compat_qc_mod_provider_field *field = &p->fields[i];
    if (store.word + store_words <= field->offset ||
        store.word >= field->offset + field->words)
      continue;
    const qa_mod_actor_field *declaration = &field->declaration;
    if (text_eq(declaration->binding, "constant") ||
        text_eq(declaration->binding, "private"))
      continue;
    bool written = false;
    for (size_t w = 0; w < execution->writes.count; w++) {
      const qa_pickup_write *write = &execution->writes.values[w];
      if (write->kind == QA_PICKUP_RESOURCE_INVENTORY &&
          text_eq(write->item, declaration->item) &&
          !text_eq(write->fields, "capacity"))
        written = true;
    }
    if (!actor_eq(actor, p->pickup_scopes[scope_index].actor) ||
        !text_eq(declaration->binding, "inventory") || !written)
      qa_range_error("Original pickup changed an undeclared resource");
  }
}
static void write_through(P *p, qa_compat_qc_entity_store_observation store) {
  qa_actor_id actor_id = qa_compat_qc_mod_provider_actor(p, store.reference);
  qa_owned_actor actor;
  if (!qa_session_actor_registry_resolve_owned(p->services->actors, actor_id,
                                               &actor))
    qa_range_error("Mod actor was released before its source store");
  qa_compat_qc_words *words =
      qa_compat_qc_entity_memory_from_reference(&p->entities, store.reference);
  int64_t store_words = (int64_t)(store.after_length / 4);
  if (text_eq(actor.owner, p->module.id)) {
    /* TS ownedActors.schedule(actor) on think/nextthink stores:
     * owned-actor scheduling is unresolved wiring (file header). */
    return;
  }
  size_t scope_index;
  bool has_pickup = pickup_scope(p, &scope_index);
  if (has_pickup) {
    qa_original_pickup_execution *execution =
        &p->pickup_scopes[scope_index].execution;
    for (size_t i = 0; i < p->field_count; i++) {
      const qa_compat_qc_mod_provider_field *field = &p->fields[i];
      if (!text_eq(field->declaration.binding, "inventory") ||
          store.word >= field->offset + field->words ||
          store.word + store_words <= field->offset)
        continue;
      if (!execution->current(execution->current_ctx))
        qa_range_error("Original pickup resource binding is no longer "
                       "current");
      size_t entry_count = 0;
      const qa_inventory_entry *entries = qa_shared_inventory_table_entries(
          p->services->inventory, actor.id, &entry_count);
      const qa_inventory_entry *entry = NULL;
      for (size_t e = 0; e < entry_count; e++)
        if (text_eq(entries[e].item, field->declaration.item)) entry = &entries[e];
      if (!entry)
        qa_range_error("Mod inventory store requires %s",
                       field->declaration.item);
      qa_inventory_entry next = *entry;
      next.count = qa_compat_qc_words_float(words, field->offset);
      PickupCommit commit = {words, field->offset};
      qa_shared_inventory_table_configure(p->services->inventory, actor, next,
                                          pickup_committed, &commit);
      if (!execution->current(execution->current_ctx))
        qa_range_error("Original pickup resource binding is no longer "
                       "current");
    }
  }
  for (size_t i = 0; i < p->field_count; i++) {
    const qa_compat_qc_mod_provider_field *field = &p->fields[i];
    if (store.word + store_words <= field->offset ||
        store.word >= field->offset + field->words)
      continue;
    const qa_mod_actor_field *declared = &field->declaration;
    const char *binding = declared->binding;
    if (text_eq(binding, "constant") || text_eq(binding, "private") ||
        text_eq(binding, "client-input")) {
      continue;
    } else if (text_eq(binding, "userinfo")) {
      if (!p->has_clients)
        qa_range_error("Mod userinfo field requires client services");
      int64_t client_slot;
      if (client_slot_of(p, actor.id, &client_slot))
        qa_compat_qc_mod_client_bindings_set_userinfo(
            &p->clients, actor.id, declared->key,
            qa_compat_qc_strings_get(
                &p->machine.strings,
                qa_compat_qc_words_int(words, field->offset)));
    } else if (text_eq(binding, "client-flags")) {
      float before_bits;
      size_t byte_offset = (size_t)(field->offset - store.word) * 4;
      memcpy(&before_bits, store.before + byte_offset, sizeof before_bits);
      int64_t next = (int64_t)trunc(qa_compat_qc_words_float(words,
                                                            field->offset));
      int64_t private_mask =
          declared->has_private_mask ? (int64_t)declared->private_mask : 0;
      int64_t changed =
          (((int64_t)trunc((double)before_bits)) ^ next) & ~private_mask;
      if (changed != 0) {
        if (!declared->has_grounded || changed != 512 || (next & 512) != 0)
          qa_range_error("Mod client-flags store requires its canonical "
                         "owner");
        qa_body_state_result body =
            qa_shared_body_table_read(p->services->bodies, actor.id);
        if (!body.has_value)
          qa_range_error("Mod ground detachment requires a live body");
        body.value.ground = NULL;
        qa_shared_body_table_write(p->services->bodies, actor, body.value);
      }
    } else if (text_eq(binding, "view-offset")) {
      int64_t client_slot;
      bool owned_output = false;
      if (p->declaration.clients)
        for (size_t o = 0; o < p->declaration.clients->output_count; o++) {
          const qa_mod_client_output_declaration *output =
              &p->declaration.clients->outputs[o];
          if (output->kind == QA_MOD_CLIENT_OUTPUT_VIEW_OFFSET &&
              output->field &&
              text_eq((const char *)output->field, declared->field))
            owned_output = true;
        }
      if (!(owned_output && client_slot_of(p, actor.id, &client_slot)))
        qa_range_error("Mod view-offset store requires its declared client "
                       "output owner");
    } else if (text_eq(binding, "classname")) {
      qa_range_error("Mod %s store requires its canonical owner", binding);
    } else if (text_eq(binding, "think") || text_eq(binding, "nextthink")) {
      /* TS ownedActors.schedule for slots past the client reserve:
       * unresolved wiring (file header). */
    } else if (text_eq(binding, "team") || text_eq(binding, "score")) {
      qa_write_source_match_field(
          p->services->match, actor.id, match_field(declared),
          qa_compat_qc_words_float(words, field->offset));
      if (qa_session_actor_registry_is_live(p->services->actors, actor.id))
        qa_compat_qc_words_set_float(
            words, field->offset,
            qa_read_source_match_field(p->services->match, actor.id,
                                       match_field(declared)));
    } else if (text_eq(binding, "health")) {
      if (!qa_gameplay_authority_read(p->services->combat, actor.id).has_value)
        qa_range_error("Mod health store requires a combat actor");
      qa_gameplay_authority_set_health(
          p->services->combat, actor,
          qa_compat_qc_words_float(words, field->offset));
    } else if (text_eq(binding, "inventory")) {
      if (has_pickup) continue;
      size_t entry_count = 0;
      const qa_inventory_entry *entries = qa_shared_inventory_table_entries(
          p->services->inventory, actor.id, &entry_count);
      const qa_inventory_entry *entry = NULL;
      for (size_t e = 0; e < entry_count; e++)
        if (text_eq(entries[e].item, declared->item)) entry = &entries[e];
      if (!entry)
        qa_range_error("Mod inventory store requires %s", declared->item);
      qa_inventory_entry next = *entry;
      next.count = qa_compat_qc_words_float(words, field->offset);
      qa_shared_inventory_table_configure(p->services->inventory, actor, next,
                                          NULL, NULL);
    } else if (text_eq(binding, "origin") || text_eq(binding, "angles") ||
               text_eq(binding, "velocity")) {
      qa_body_state_result body =
          qa_shared_body_table_read(p->services->bodies, actor.id);
      if (!body.has_value) qa_range_error("Mod body binding disappeared");
      qa_vec3 value = qa_compat_qc_words_vector(words, field->offset);
      if (text_eq(binding, "origin"))
        body.value.origin = value;
      else if (text_eq(binding, "angles"))
        body.value.angles = value;
      else
        body.value.velocity = value;
      qa_shared_body_table_write(p->services->bodies, actor, body.value);
    } else {
      /* bounds-min | bounds-max */
      if (output_field_owned(p, actor.id, declared->field)) continue;
      qa_body_state_result body =
          qa_shared_body_table_read(p->services->bodies, actor.id);
      if (!body.has_value)
        qa_range_error("Mod bounds store requires a physical actor");
      if (text_eq(binding, "bounds-min"))
        body.value.bounds.min = qa_compat_qc_words_vector(words, field->offset);
      else
        body.value.bounds.max = qa_compat_qc_words_vector(words, field->offset);
      qa_shared_body_table_write(p->services->bodies, actor, body.value);
    }
  }
}
static void release_client_bindings(P *p, qa_actor_id actor);
static const char *release_client_projection(P *p, qa_actor_id actor);
static void drain_retired_projections(P *p);
static bool server_active(void *ctx) { return !((P *)ctx)->loading; }
static bool builtin_is_free_entity(void *ctx, int64_t slot) {
  P *p = ctx;
  qa_actor_id actor;
  return !projection_actor_at(p, slot, &actor) ||
         !qa_session_actor_registry_is_live(p->services->actors, actor);
}
static void prepare_entities(void *ctx) {
  P *p = ctx;
  size_t count = 0;
  const qa_actor_observation *observations =
      qa_session_actor_registry_observations(p->services->actors, &count);
  for (size_t i = 0; i < count; i++)
    qa_compat_qc_mod_provider_reference(p, &observations[i].id);
}
static void validate_entity_access(void *ctx, int64_t reference, int64_t word,
                                   int64_t words, const char *kind) {
  P *p = ctx;
  qa_actor_id actor = qa_compat_qc_mod_provider_actor(p, reference);
  qa_actor_observation_result observed =
      qa_session_actor_registry_observe(p->services->actors, actor);
  if (observed.has_value && text_eq(observed.value.owner, p->module.id))
    return;
  for (int64_t offset = word; offset < word + words; offset++) {
    bool mapped = false;
    for (size_t i = 0; i < p->field_count; i++)
      if (offset >= p->fields[i].offset &&
          offset < p->fields[i].offset + p->fields[i].words)
        mapped = true;
    if (!mapped)
      qa_range_error("Mod %s accessed unmapped actor field word %lld",
                     p->module.id, (long long)offset);
  }
  if (strcmp(kind, "read") == 0)
    refresh(p, actor, qa_compat_qc_entity_memory_slot(&p->entities, reference),
            word, words);
}
static void observe_call(void *ctx, qa_compat_qc_call_site call) {
  P *p = ctx;
  if (p->objectives) {
    qa_mod_source_objectives_flush(p->objectives);
    qa_mod_source_objectives_refresh(p->objectives);
  }
  qa_compat_qc_mod_input_observe_call(&p->input, call);
}
static void observe_entity_store(void *ctx,
                                 qa_compat_qc_entity_store_observation store) {
  P *p = ctx;
  validate_pickup_store(p, store);
  /* protection/items/combat observers: unresolved wiring. */
  write_through(p, store);
}
typedef struct {
  P *p;
  qa_mod_source_call call;
  const qa_q1qc_armor_stage *region;
} ExecCtx;
static double exec_fn(void *ctx, int64_t argument_count) {
  ExecCtx *e = ctx;
  double result;
  if (!e->region) {
    qa_compat_qc_machine_execute(
        &e->p->machine,
        qa_compat_qc_program_function_named(e->p->program, e->call.function)
            ->index,
        argument_count);
    result = qa_compat_qc_words_float(&e->p->machine.globals, 1);
  } else {
    qa_q1qc_inline_region region = e->region->region;
    qa_compat_qc_inline_region converted = {
        region.function_index, region.entry, region.exit, region.replaceable,
        region.has_standalone, region.standalone_saved,
        region.standalone_scope};
    result = qa_compat_qc_machine_execute_region(&e->p->machine, converted,
                                                 argument_count);
  }
  publish_client_outputs(e->p);
  /* TS messages flush at depth 1: messages subsystem unresolved. */
  return result;
}
double qa_compat_qc_mod_provider_invoke(
    P *p, qa_mod_source_call call,
    const qa_compat_qc_mod_input_entry *inputs, size_t input_count,
    const qa_q1qc_armor_stage *region) {
  if (p->closed) qa_range_error("Gameplay mod is closed");
  if (p->depth >= 64)
    qa_range_error("Gameplay mod callback recursion exceeded 64 calls");
  if (p->objectives) {
    qa_mod_source_objectives_flush(p->objectives);
    qa_mod_source_objectives_refresh(p->objectives);
  }
  p->depth++;
  ExecCtx exec = {p, call, region};
  double result = qa_compat_qc_with_source_call(
      &p->machine, call, inputs, input_count,
      (qa_compat_qc_actor_reference){p, reference_actor},
      (qa_compat_qc_source_execute){&exec, exec_fn});
  if (p->objectives) qa_mod_source_objectives_flush(p->objectives);
  p->depth--;
  drain_retired_projections(p);
  return result;
}
static void invoke_owned(P *p, int64_t index, qa_actor_id actor,
                         const qa_actor_id *other, qa_source_time time) {
  const qa_compat_qc_function *fn =
      qa_compat_qc_program_function_at(p->program, index);
  if (fn->parameter_size_count != 0 || fn->index == 0)
    qa_range_error("Mod actor callback must be a no-argument source "
                   "function");
  qa_actor_id self_copy = actor;
  qa_actor_id other_copy;
  if (other) other_copy = *other;
  qa_mod_named_value globals[3] = {
      {"self", {"input", "self", 0, NULL, {0, 0, 0}}},
      {"other", {"input", "other", 0, NULL, {0, 0, 0}}},
      {"time", {"input", "time", 0, NULL, {0, 0, 0}}}};
  qa_compat_qc_mod_input_entry inputs[3];
  memset(inputs, 0, sizeof inputs);
  inputs[0].name = "self";
  inputs[0].value.kind = "actor";
  inputs[0].value.actor_value = &self_copy;
  inputs[1].name = "other";
  inputs[1].value.kind = "actor";
  inputs[1].value.actor_value = other ? &other_copy : NULL;
  inputs[2].name = "time";
  inputs[2].value.kind = "float";
  inputs[2].value.number_value = seconds_of(time);
  qa_compat_qc_mod_provider_invoke(
      p, (qa_mod_source_call){fn->name, NULL, 0, globals, 3}, inputs, 3, NULL);
}
static void run_client_think(void *ctx, qa_actor_id actor,
                             qa_frame_context frame, void *live_ctx,
                             bool (*live)(void *live_ctx)) {
  P *p = ctx;
  int64_t nextthink = qa_compat_qc_machine_field_offset(&p->machine,
                                                        "nextthink");
  int64_t think = qa_compat_qc_machine_field_offset(&p->machine, "think");
  bool quakeworld = p->program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD;
  qa_clock_profile profile;
  memset(&profile, 0, sizeof profile);
  if (quakeworld) {
    profile.kind = QA_CLOCK_Q1_QUAKEWORLD;
    profile.maximum_command_milliseconds = 100;
  } else {
    profile.kind = QA_CLOCK_Q1_NETQUAKE;
    profile.minimum_frame_seconds = 0;
    profile.maximum_frame_seconds = 0.1;
    profile.has_fixed_frame_seconds = false;
  }
  while (live(live_ctx)) {
    qa_compat_qc_words *words = qa_compat_qc_entity_memory_from_reference(
        &p->entities, qa_compat_qc_mod_provider_reference(p, &actor));
    qa_source_time due = {QA_SOURCE_TIME_SECONDS,
                          qa_compat_qc_words_float(words, nextthink)};
    qa_source_time_result time = qa_think_callback_time(profile, due, frame);
    if (!time.has_value) return;
    int32_t index = qa_compat_qc_words_int(words, think);
    qa_compat_qc_words_set_float(words, nextthink, 0);
    bool had_frame = p->has_frame;
    qa_frame_context previous = p->frame;
    p->frame = frame;
    p->frame.time = time.value;
    p->frame.phase = QA_FRAME_PHASE_ENTITY_THINK;
    p->has_frame = true;
    invoke_owned(p, index, actor, NULL, time.value);
    p->frame = previous;
    p->has_frame = had_frame;
    if (!quakeworld) return;
  }
}
static void release_client_bindings(P *p, qa_actor_id actor) {
  if (p->client_outputs)
    qa_source_mod_client_outputs_release(p->client_outputs, actor);
  /* pickups/items/protection release: unresolved wiring. */
}
static const char *release_client_projection(P *p, qa_actor_id actor) {
  if (p->depth != 0 || qa_compat_qc_mod_input_get_active(&p->input)) {
    retired_add(p, actor);
    return "deferred";
  }
  int64_t slot = projection_slot(p, actor);
  if (slot >= 0) {
    projection_delete(p, actor);
    qa_compat_qc_words *words = qa_compat_qc_entity_memory_at(&p->entities,
                                                               slot);
    memset(words->bytes, 0, words->byte_length);
  }
  return "released";
}
static void drain_retired_projections(P *p) {
  if (p->depth != 0 || qa_compat_qc_mod_input_get_active(&p->input)) return;
  while (p->retired_count > 0) {
    qa_actor_id actor = p->retired[0];
    memmove(&p->retired[0], &p->retired[1],
            (p->retired_count - 1) * sizeof *p->retired);
    p->retired_count--;
    if (p->has_clients) qa_compat_qc_mod_client_bindings_forget(&p->clients,
                                                                 actor);
    release_client_projection(p, actor);
  }
}
static void on_actor_release(void *ctx, qa_owned_actor actor) {
  P *p = ctx;
  release_client_bindings(p, actor.id);
  retired_add(p, actor.id);
  drain_retired_projections(p);
}
static void close_client_bindings(P *p) {
  if (p->client_outputs) qa_source_mod_client_outputs_close(p->client_outputs);
  /* pickups/items/protection close: unresolved wiring. */
}
/* Objectives storage (TS ModSourceObjectives options). */
static void objective_current(void *ctx, bool has_reference,
                              int64_t reference) {
  P *p = ctx;
  if (p->closed) qa_range_error("QuakeC objective source is closed");
  if (has_reference) qa_compat_qc_mod_provider_actor(p, reference);
}
static qa_compat_qc_objective_resolved objective_storage(
    P *p, const qa_qc_mod_objective_storage *storage) {
  return qa_compat_qc_resolve_objective_storage(
      &p->machine, *storage, (qa_compat_qc_objective_current){p,
                                                               objective_current});
}
static bool objectives_current(void *ctx) { return !((P *)ctx)->closed; }
static const char *objectives_location(
    void *ctx, const qa_source_objective_declaration *declaration) {
  P *p = ctx;
  const qa_qc_mod_objective_storage *storages[3] = {
      (const qa_qc_mod_objective_storage *)declaration->state_storage,
      (const qa_qc_mod_objective_storage *)declaration->carrier,
      (const qa_qc_mod_objective_storage *)declaration->target};
  free(p->objective_location);
  p->objective_location = NULL;
  size_t length = 0, capacity = 0;
  for (int i = 0; i < 3; i++) {
    char part[160];
    if (!storages[i]) {
      snprintf(part, sizeof part, "none");
    } else {
      qa_compat_qc_objective_resolved resolved =
          objective_storage(p, storages[i]);
      if (!resolved.has_reference)
        snprintf(part, sizeof part, "global:%lld", (long long)resolved.offset);
      else {
        qa_actor_id actor =
            qa_compat_qc_mod_provider_actor(p, resolved.reference);
        snprintf(part, sizeof part, "entity:%lld:%lld:%lld:%lld",
                 (long long)actor.slot, (long long)actor.generation,
                 (long long)resolved.reference, (long long)resolved.offset);
      }
    }
    size_t part_length = strlen(part);
    size_t need = length + part_length + 2;
    if (need > capacity) {
      capacity = need * 2;
      p->objective_location = xrealloc(p->objective_location, capacity);
    }
    if (length > 0) p->objective_location[length++] = '|';
    memcpy(p->objective_location + length, part, part_length);
    length += part_length;
    p->objective_location[length] = '\0';
  }
  return p->objective_location;
}
static double objectives_read_scalar(void *ctx, const void *storage) {
  qa_compat_qc_objective_resolved resolved = objective_storage(
      ctx, (const qa_qc_mod_objective_storage *)storage);
  return qa_compat_qc_words_float(resolved.words, resolved.offset);
}
static void objectives_write_scalar(void *ctx, const void *storage,
                                    double value) {
  qa_compat_qc_objective_resolved resolved = objective_storage(
      ctx, (const qa_qc_mod_objective_storage *)storage);
  qa_compat_qc_words_set_float(resolved.words, resolved.offset, value);
}
static bool objectives_read_actor(void *ctx, const void *storage,
                                  qa_actor_id *out) {
  qa_compat_qc_objective_resolved resolved = objective_storage(
      ctx, (const qa_qc_mod_objective_storage *)storage);
  int32_t reference = qa_compat_qc_words_int(resolved.words, resolved.offset);
  if (reference == 0) return false;
  *out = qa_compat_qc_mod_provider_actor(ctx, reference);
  return true;
}
static void objectives_write_actor(void *ctx, const void *storage,
                                   const qa_actor_id *value) {
  P *p = ctx;
  int64_t reference = qa_compat_qc_mod_provider_reference(p, value);
  qa_compat_qc_objective_resolved resolved = objective_storage(
      p, (const qa_qc_mod_objective_storage *)storage);
  qa_compat_qc_words_set_int(resolved.words, resolved.offset,
                             (double)reference);
}
static void objectives_invoke(void *ctx, const void *call,
                              const qa_mod_objective_input_entry *inputs,
                              size_t count) {
  P *p = ctx;
  qa_compat_qc_mod_input_entry *converted = NULL;
  if (count > 0) {
    converted = xrealloc(NULL, count * sizeof *converted);
    for (size_t i = 0; i < count; i++) {
      converted[i].name = inputs[i].name;
      converted[i].value = inputs[i].value;
    }
  }
  qa_compat_qc_mod_provider_invoke(p, *(const qa_mod_source_call *)call,
                                   converted, count, NULL);
  free(converted);
}
static double objectives_seconds(void *ctx) {
  P *p = ctx;
  return seconds_of(p->services->time(p->services->ctx));
}
/* Client bindings + client-output adapters. */
static void clients_project(void *ctx, qa_actor_id actor) {
  qa_compat_qc_mod_provider_reference(ctx, &actor);
}
static const char *clients_release(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  release_client_bindings(p, actor);
  return release_client_projection(p, actor);
}
static void clients_admitted(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  if (p->client_outputs)
    qa_source_mod_client_outputs_publish(p->client_outputs, actor);
  /* items/pickups/protection admit: unresolved wiring. */
}
static void clients_invoke(void *ctx, qa_mod_source_call call,
                           qa_actor_id actor, const qa_frame_context *frame) {
  P *p = ctx;
  bool had_frame = p->has_frame;
  qa_frame_context previous = p->frame;
  qa_source_time now = frame ? frame->time : p->services->time(p->services->ctx);
  if (frame) {
    p->frame = *frame;
    p->has_frame = true;
  }
  qa_actor_id self_copy = actor;
  qa_compat_qc_mod_input_entry inputs[3];
  memset(inputs, 0, sizeof inputs);
  inputs[0].name = "self";
  inputs[0].value.kind = "actor";
  inputs[0].value.actor_value = &self_copy;
  inputs[1].name = "time";
  inputs[1].value.kind = "float";
  inputs[1].value.number_value = seconds_of(now);
  size_t count = 2;
  if (frame) {
    inputs[2].name = "elapsed";
    inputs[2].value.kind = "float";
    inputs[2].value.number_value = seconds_of(frame->elapsed);
    count = 3;
  }
  qa_compat_qc_mod_provider_invoke(p, call, inputs, count, NULL);
  p->frame = previous;
  p->has_frame = had_frame;
}
typedef struct {
  P *p;
  qa_compat_qc_mod_input_cleanup inner;
} InputCleanupCell;
static void input_cleanup(void *ctx) {
  InputCleanupCell *cell = ctx;
  cell->inner.fn(cell->inner.ctx);
  drain_retired_projections(cell->p);
  free(cell);
}
static void input_open(void *ctx, qa_mod_client_application *application,
                       void (**cleanup)(void *), void **cleanup_ctx) {
  P *p = ctx;
  InputCleanupCell *cell = xrealloc(NULL, sizeof *cell);
  cell->p = p;
  cell->inner = qa_compat_qc_mod_input_open(&p->input, application);
  *cleanup = input_cleanup;
  *cleanup_ctx = cell;
}
static void input_invoke(void *ctx, qa_mod_source_call call,
                         qa_mod_client_application *application) {
  P *p = ctx;
  qa_mod_client_input_values values =
      qa_compat_qc_mod_input_values(&p->input, application);
  qa_compat_qc_mod_input_entry inputs[10];
  for (size_t i = 0; i < values.count; i++) {
    inputs[i].name = values.entries[i].name;
    inputs[i].value = values.entries[i].value;
  }
  qa_compat_qc_mod_provider_invoke(p, call, inputs, values.count, NULL);
}
static void input_output(void *ctx, const qa_mod_qc_input_output *outputs,
                         size_t output_count,
                         qa_mod_client_application *application,
                         void (*run)(void *), void *run_ctx,
                         qa_mod_client_input_output **out_outputs,
                         size_t *out_count) {
  P *p = ctx;
  *out_outputs = qa_compat_qc_mod_input_output(&p->input, outputs,
                                               output_count, application,
                                               run_ctx, run, out_count);
}
static qa_mod_client_output_lease outputs_claim(
    void *ctx, qa_provider_id owner,
    const qa_mod_client_output_channel *channels, size_t channel_count) {
  P *p = ctx;
  return p->services->clients->claim_outputs(p->services->clients->ctx, owner,
                                              channels, channel_count);
}
static double outputs_scalar(void *ctx, qa_actor_id actor, const void *field) {
  P *p = ctx;
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_from_reference(
      &p->entities, qa_compat_qc_mod_provider_reference(p, &actor));
  return qa_compat_qc_words_float(
      words, qa_compat_qc_machine_field_offset(&p->machine,
                                               (const char *)field));
}
static qa_vec3 outputs_vector(void *ctx, qa_actor_id actor, const void *field) {
  P *p = ctx;
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_from_reference(
      &p->entities, qa_compat_qc_mod_provider_reference(p, &actor));
  return qa_compat_qc_words_vector(
      words, qa_compat_qc_machine_field_offset(&p->machine,
                                               (const char *)field));
}
static int64_t input_reference_fn(void *ctx, qa_actor_id actor) {
  return qa_compat_qc_mod_provider_reference(ctx, &actor);
}
static bool input_live_fn(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  return qa_session_actor_registry_is_live(p->services->actors, actor);
}
static void builtin_localcmd(void *ctx, qa_compat_qc_machine *machine) {
  P *p = ctx;
  if (!p->commands)
    qa_compat_qc_machine_fail(
        machine, "Mod localcmd requires the destination command service");
  qa_mod_command_port_append(p->commands,
                             qa_compat_qc_machine_arg_string(machine, 0),
                             NULL);
}
/* Host checkpoint state (TS hostState). */
static bool set_own(qa_save_value *object, const char *key,
                    qa_save_value *child) {
  if (!child) return false;
  if (!qa_save_value_set(object, key, child)) {
    qa_save_value_free(child);
    return false;
  }
  return true;
}
static bool push_own(qa_save_value *array, qa_save_value *child) {
  if (!child) return false;
  if (!qa_save_value_push(array, child)) {
    qa_save_value_free(child);
    return false;
  }
  return true;
}
static qa_save_value *saved_actor_value(qa_actor_id actor) {
  qa_saved_actor_id saved = qa_saved_actor_id_of(actor);
  qa_save_value *obj = qa_save_value_object();
  if (!obj) return NULL;
  if (!set_own(obj, "slot", qa_save_value_number(saved.slot)) ||
      !set_own(obj, "generation", qa_save_value_number(saved.generation))) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
static qa_compat_qc_host_saved_state host_checkpoint(void *ctx) {
  P *p = ctx;
  qa_save_value *root = qa_save_value_object();
  bool ok = root != NULL;
  qa_save_value *projections = ok ? qa_save_value_array() : NULL;
  ok = projections != NULL;
  if (ok)
    for (size_t i = 0; i < p->projection_count && ok; i++) {
      qa_save_value *entry = qa_save_value_object();
      ok = entry &&
           set_own(entry, "actor", saved_actor_value(p->projections[i].actor)) &&
           set_own(entry, "slot",
                   qa_save_value_number((double)p->projections[i].slot)) &&
           push_own(projections, entry);
      if (!ok) qa_save_value_free(entry);
    }
  ok = ok && set_own(root, "projections", projections);
  if (!ok && projections) qa_save_value_free(projections);
  qa_save_value *precached = ok ? qa_save_value_array() : NULL;
  ok = precached != NULL;
  if (ok)
    for (size_t i = 0; i < p->precached_count && ok; i++)
      ok = push_own(precached, qa_save_value_string(p->precached[i].key));
  ok = ok && set_own(root, "precached", precached);
  if (!ok && precached) qa_save_value_free(precached);
  ok = ok && set_own(root, "messages", qa_save_value_null());
  ok = ok && set_own(root, "cvars",
                     qa_cvar_registry_capture_quakec_state(p->environment.cvars));
  if (ok && p->environment.visibility) {
    qa_q1_client_visibility_state state =
        qa_q1_client_visibility_capture(p->environment.visibility);
    qa_save_value *visibility = qa_save_value_object();
    ok = visibility &&
         set_own(visibility, "lastCheckSlot",
                 qa_save_value_number(state.last_check_slot)) &&
         set_own(visibility, "lastCheckTime",
                 qa_save_value_number(state.last_check_time)) &&
         set_own(visibility, "checkedCluster",
                 state.has_checked_cluster
                     ? qa_save_value_number(state.checked_cluster)
                     : qa_save_value_null()) &&
         set_own(root, "visibility", visibility);
    if (!ok) qa_save_value_free(visibility);
  } else
    ok = ok && set_own(root, "visibility", qa_save_value_null());
  ok = ok && set_own(root, "initialized",
                     qa_save_value_boolean(p->initialized));
  if (ok && p->has_clients) {
    qa_save_value *clients = qa_save_value_array();
    ok = clients != NULL;
    if (ok)
      for (size_t i = 0; i < p->clients.entry_count && ok; i++) {
        qa_save_value *entry = qa_save_value_object();
        ok = entry &&
             set_own(entry, "actor",
                     saved_actor_value(p->clients.entries[i].actor)) &&
             set_own(entry, "slot",
                     qa_save_value_number((double)p->clients.entries[i].slot)) &&
             set_own(entry, "admitted",
                     qa_save_value_boolean(p->clients.entries[i].admitted)) &&
             push_own(clients, entry);
        if (!ok) qa_save_value_free(entry);
      }
    ok = ok && set_own(root, "clients", clients);
    if (!ok && clients) qa_save_value_free(clients);
  } else
    ok = ok && set_own(root, "clients", qa_save_value_null());
  uint8_t *encoded = NULL;
  size_t encoded_length = 0;
  if (ok) {
    encoded = qa_encode_checkpoint_value(root, &encoded_length);
    ok = encoded != NULL;
  }
  qa_save_value_free(root);
  if (!ok) qa_range_error("mod host checkpoint allocation failed");
  free(p->host_encoded);
  p->host_encoded = (char *)encoded;
  p->host_encoded_length = encoded_length;
  p->host_random_state = p->random->source.checkpoint(p->random->source.ctx);
  qa_compat_qc_host_saved_state saved;
  memset(&saved, 0, sizeof saved);
  saved.state.module = p->module;
  saved.state.format = "quakec:mod-host-v3";
  saved.state.bytes.bytes = encoded;
  saved.state.bytes.length = encoded_length;
  saved.random = &p->host_random_state;
  saved.random_count = 1;
  return saved;
}
static qa_actor_id resolve_saved(P *p, qa_saved_actor_id saved) {
  if (p->services->reference_saved)
    return p->services->reference_saved(p->services->ctx, saved);
  return qa_session_actor_registry_reference_saved(p->services->actors, saved,
                                                   "current");
}
static void host_restore(void *ctx, qa_compat_qc_host_saved_state saved) {
  P *p = ctx;
  if (!text_eq(saved.state.format, "quakec:mod-host-v3") ||
      saved.random_count != 1)
    qa_range_error("Invalid gameplay mod host state");
  if (!saved.random) qa_range_error("Missing gameplay mod random state");
  p->random->restore(p->random->source.ctx, saved.random[0]);
  p->projection_count = 0;
  qa_save_value *root = qa_decode_checkpoint_value(saved.state.bytes.bytes,
                                                   saved.state.bytes.length);
  if (!root) qa_range_error("mod host checkpoint allocation failed");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "mod-host"))
    qa_range_error("mod host checkpoint allocation failed");
  qa_save_reader field_reader;
  if (!qa_save_reader_field(&reader, "projections", &field_reader))
    qa_range_error("mod host checkpoint allocation failed");
  for (size_t i = 0; i < qa_save_reader_list_count(&field_reader); i++) {
    qa_save_reader entry, slot_reader, actor_reader;
    if (!qa_save_reader_item(&field_reader, i, &entry) ||
        !qa_save_reader_field(&entry, "slot", &slot_reader) ||
        !qa_save_reader_field(&entry, "actor", &actor_reader))
      qa_range_error("mod host checkpoint allocation failed");
    int64_t slot = (int64_t)qa_save_reader_integer_min(&slot_reader, 1);
    qa_actor_id actor = resolve_saved(p, qa_read_saved_actor(&actor_reader));
    qa_actor_id existing;
    if (slot >= p->entities.count || projection_actor_at(p, slot, &existing) ||
        projection_slot(p, actor) >= 0)
      qa_range_error("Invalid gameplay mod actor projection");
    projection_set(p, actor, slot);
    qa_save_reader_destroy(&actor_reader);
    qa_save_reader_destroy(&slot_reader);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&field_reader);
  for (size_t i = 0; i < p->precached_count; i++) free(p->precached[i].key);
  p->precached_count = 0;
  if (!qa_save_reader_field(&reader, "precached", &field_reader))
    qa_range_error("mod host checkpoint allocation failed");
  for (size_t i = 0; i < qa_save_reader_list_count(&field_reader); i++) {
    qa_save_reader entry;
    if (!qa_save_reader_item(&field_reader, i, &entry))
      qa_range_error("mod host checkpoint allocation failed");
    const char *key = qa_save_reader_string(&entry);
    const char *kind = strncmp(key, "model:", 6) == 0 ? "model"
                       : strncmp(key, "sound:", 6) == 0 ? "sound"
                                                        : NULL;
    qa_compat_qc_precached_resource found;
    bool duplicate = false;
    for (size_t j = 0; j < p->precached_count; j++)
      if (strcmp(p->precached[j].key, key) == 0) duplicate = true;
    if (!kind || duplicate ||
        !qa_compat_qc_mod_provider_lookup(p, kind, key + strlen(kind) + 1,
                                          &found))
      qa_range_error("Saved mod resource no longer resolves");
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&field_reader);
  if (!qa_save_reader_field(&reader, "messages", &field_reader))
    qa_range_error("mod host checkpoint allocation failed");
  if (!qa_save_reader_is_null(&field_reader))
    qa_range_error("Saved mod message services differ");
  qa_save_reader_destroy(&field_reader);
  qa_cvar_registry_restore_quakec_state(
      p->environment.cvars, qa_save_value_get(root, "cvars"));
  const qa_save_value *visibility = qa_save_value_get(root, "visibility");
  bool visibility_null = visibility == NULL;
  {
    qa_save_reader vis_reader;
    if (!qa_save_reader_field(&reader, "visibility", &vis_reader))
      qa_range_error("mod host checkpoint allocation failed");
    visibility_null = qa_save_reader_is_null(&vis_reader);
    qa_save_reader_destroy(&vis_reader);
  }
  if ((!p->environment.visibility && !visibility_null) ||
      (p->environment.visibility && visibility_null))
    qa_range_error("Saved mod client visibility differs");
  if (p->environment.visibility)
    qa_q1_client_visibility_restore(p->environment.visibility, visibility);
  if (!qa_save_reader_field(&reader, "initialized", &field_reader))
    qa_range_error("mod host checkpoint allocation failed");
  p->initialized = qa_save_reader_boolean(&field_reader);
  qa_save_reader_destroy(&field_reader);
  if (p->client_outputs) qa_source_mod_client_outputs_clear(p->client_outputs);
  if (!qa_save_reader_field(&reader, "clients", &field_reader))
    qa_range_error("mod host checkpoint allocation failed");
  if (p->has_clients) {
    size_t count = qa_save_reader_list_count(&field_reader);
    qa_compat_qc_mod_client_slot *entries =
        count ? xrealloc(NULL, count * sizeof *entries) : NULL;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry, actor_reader, slot_reader, admitted_reader;
      if (!qa_save_reader_item(&field_reader, i, &entry) ||
          !qa_save_reader_field(&entry, "actor", &actor_reader) ||
          !qa_save_reader_field(&entry, "slot", &slot_reader) ||
          !qa_save_reader_field(&entry, "admitted", &admitted_reader))
        qa_range_error("mod host checkpoint allocation failed");
      entries[i].actor = resolve_saved(p, qa_read_saved_actor(&actor_reader));
      entries[i].slot = (int64_t)qa_save_reader_integer_min(&slot_reader, 1);
      entries[i].admitted = qa_save_reader_boolean(&admitted_reader);
      qa_save_reader_destroy(&admitted_reader);
      qa_save_reader_destroy(&slot_reader);
      qa_save_reader_destroy(&actor_reader);
      qa_save_reader_destroy(&entry);
    }
    qa_compat_qc_mod_client_bindings_restore(&p->clients, entries, count);
    free(entries);
    int64_t maximum =
        p->declaration.clients ? (int64_t)p->declaration.clients->maximum : 0;
    bool bad = p->entities.count <= maximum;
    for (size_t i = 0; i < p->clients.entry_count && !bad; i++)
      if (projection_slot(p, p->clients.entries[i].actor) !=
          p->clients.entries[i].slot)
        bad = true;
    for (size_t i = 0; i < p->projection_count && !bad; i++)
      if (p->projections[i].slot <= maximum) {
        bool projected = false;
        for (size_t j = 0; j < p->clients.entry_count; j++)
          if (p->clients.entries[j].slot == p->projections[i].slot &&
              actor_eq(p->clients.entries[j].actor, p->projections[i].actor))
            projected = true;
        if (!projected) bad = true;
      }
    if (bad)
      qa_range_error("Saved QuakeC client projection differs from its "
                     "reserved slot");
  } else if (!qa_save_reader_is_undefined(&field_reader) &&
             !qa_save_reader_is_null(&field_reader)) {
    qa_range_error("Saved QuakeC clients require the declared lifecycle "
                   "service");
  }
  qa_save_reader_destroy(&field_reader);
  /* TS ownedActors.restored(): unresolved wiring. */
  if (p->objectives) qa_mod_source_objectives_restored(p->objectives);
  if (p->has_clients)
    for (size_t i = 0; i < p->clients.entry_count; i++) {
      qa_actor_id actor = p->clients.entries[i].actor;
      /* TS protection.reserve: unresolved wiring. */
      if (p->clients.entries[i].admitted && p->client_outputs)
        qa_source_mod_client_outputs_publish(p->client_outputs, actor);
    }
  if (p->has_clients) qa_compat_qc_mod_client_bindings_start(&p->clients);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(root);
}
/* Match players (TS matchPlayer closures). */
struct qa_compat_qc_match_player_cell {
  P *p;
  qa_actor_id actor;
  qa_owned_actor owned;
  int64_t slot;
};
typedef struct qa_compat_qc_match_player_cell MatchCell;
static const qa_compat_qc_mod_provider_field *match_bound_field(P *p,
                                                                const char *b) {
  for (size_t i = 0; i < p->field_count; i++)
    if (text_eq(p->fields[i].declaration.binding, b)) return &p->fields[i];
  return NULL;
}
static qa_compat_qc_words *match_current(MatchCell *cell) {
  P *p = cell->p;
  qa_owned_actor owned;
  qa_actor_source_result source =
      qa_session_actor_registry_source_of(p->services->actors, cell->actor);
  if (p->closed ||
      !qa_session_actor_registry_resolve_owned(p->services->actors,
                                               cell->actor, &owned) ||
      !qa_same_owned_actor(owned, cell->owned) || !source.has_value ||
      source.slot != cell->slot)
    qa_range_error("QuakeC match actor was retired");
  return qa_compat_qc_entity_memory_at(&p->entities, cell->slot);
}
static const char *match_team(void *ctx) {
  MatchCell *cell = ctx;
  const qa_compat_qc_mod_provider_field *team =
      match_bound_field(cell->p, "team");
  if (!team) return NULL;
  return qa_source_team(team->declaration.team_values,
                        team->declaration.team_value_count,
                        qa_compat_qc_words_float(match_current(cell),
                                                 team->offset));
}
static double match_score(void *ctx) {
  MatchCell *cell = ctx;
  const qa_compat_qc_mod_provider_field *score =
      match_bound_field(cell->p, "score");
  if (!score) qa_range_error("QuakeC match actor has no score field");
  return qa_compat_qc_words_float(match_current(cell), score->offset);
}
static void match_set_team(void *ctx, const char *value) {
  MatchCell *cell = ctx;
  const qa_compat_qc_mod_provider_field *team =
      match_bound_field(cell->p, "team");
  if (!team) qa_range_error("QuakeC match actor has no team field");
  qa_compat_qc_words_set_float(
      match_current(cell), team->offset,
      qa_original_team(team->declaration.team_values,
                       team->declaration.team_value_count, value));
}
static void match_set_score(void *ctx, double value) {
  MatchCell *cell = ctx;
  const qa_compat_qc_mod_provider_field *score =
      match_bound_field(cell->p, "score");
  if (!score || !isfinite(value))
    qa_range_error("QuakeC match actor has no finite score field");
  qa_compat_qc_words_set_float(match_current(cell), score->offset, value);
}
static qa_source_match_player_result match_resolve(void *ctx,
                                                   qa_actor_id actor) {
  P *p = ctx;
  qa_source_match_player_result result;
  memset(&result, 0, sizeof result);
  qa_owned_actor owned;
  qa_actor_source_result source =
      qa_session_actor_registry_source_of(p->services->actors, actor);
  bool has_owned = qa_session_actor_registry_resolve_owned(p->services->actors,
                                                           actor, &owned);
  if (p->closed || !has_owned || !text_eq(owned.owner, p->module.id) ||
      !source.has_value || !text_eq(source.provider, p->module.id))
    return result;
  MatchCell *cell = NULL;
  for (size_t i = 0; i < p->match_cell_count; i++)
    if (actor_eq(p->match_cells[i].actor, actor)) cell = &p->match_cells[i];
  if (!cell) {
    if (p->match_cell_count == p->match_cell_capacity) {
      p->match_cell_capacity =
          p->match_cell_capacity ? p->match_cell_capacity * 2 : 8;
      p->match_cells = xrealloc(p->match_cells,
                                p->match_cell_capacity * sizeof *p->match_cells);
    }
    cell = &p->match_cells[p->match_cell_count++];
    cell->p = p;
    cell->actor = actor;
  }
  cell->owned = owned;
  cell->slot = source.slot;
  result.has_value = true;
  result.value.ctx = cell;
  result.value.owner = p->module.id;
  result.value.team = match_team;
  result.value.score = match_score;
  result.value.set_team = match_set_team;
  result.value.set_score = match_set_score;
  return result;
}
void qa_compat_qc_mod_provider_activate_match(P *p) {
  if (p->objectives) qa_mod_source_objectives_activate(p->objectives);
  if (p->has_release_match ||
      (!match_bound_field(p, "team") && !match_bound_field(p, "score")))
    return;
  if (!p->services->match)
    qa_range_error("Declared match fields require destination match "
                   "services");
  p->release_match = p->services->match->bind_source(
      p->services->match->ctx, p->module.id, match_resolve, p);
  p->has_release_match = true;
}
/* Lifecycle. */
void qa_compat_qc_mod_provider_bind_commands(P *p,
                                             qa_mod_command_port *commands) {
  if (p->commands) qa_range_error("Mod commands are already bound");
  p->commands = commands;
}
bool qa_compat_qc_mod_provider_console_command(P *p,
                                               qa_command_invocation *inv) {
  qa_command_invocation_assert_active(inv);
  char *name = qa_ascii_fold(inv->argc > 0 ? inv->argv[0] : "");
  if (!name) qa_range_error("mod command allocation failed");
  const qa_mod_console_command *found = NULL;
  for (size_t i = 0; i < p->declaration.command_count; i++) {
    char *candidate = qa_ascii_fold(p->declaration.commands[i].name);
    if (!candidate) qa_range_error("mod command allocation failed");
    if (strcmp(candidate, name) == 0) found = &p->declaration.commands[i];
    free(candidate);
  }
  free(name);
  if (!found) return false;
  qa_mod_source_call call;
  qa_mod_callback_value *args = NULL;
  qa_mod_named_value *globals = NULL;
  if (!qa_compat_qc_console_call(*found, (const char *const *)inv->argv,
                                 inv->argc, inv->args_text, &call, &args,
                                 &globals))
    qa_range_error("mod command allocation failed");
  qa_compat_qc_mod_provider_invoke(p, call, NULL, 0, NULL);
  qa_compat_qc_console_call_destroy(args, globals);
  return true;
}
qa_cvar_registry *qa_compat_qc_mod_provider_get_cvars(P *p) {
  return p->environment.cvars;
}
void qa_compat_qc_mod_provider_initialize(P *p) {
  if (p->initialized || p->depth != 0)
    qa_range_error("Mod source initialization must run once at an idle "
                   "boundary");
  p->loading = true;
  /* TS protection.reserve over current clients: unresolved wiring. */
  qa_compat_qc_mod_input_entry inputs[2];
  memset(inputs, 0, sizeof inputs);
  inputs[0].name = "self";
  inputs[0].value.kind = "actor";
  inputs[0].value.actor_value = NULL;
  qa_source_time now = p->services->time(p->services->ctx);
  inputs[1].name = "time";
  inputs[1].value.kind = "float";
  inputs[1].value.number_value = seconds_of(now);
  for (size_t i = 0; i < p->declaration.initialize_count; i++)
    qa_compat_qc_mod_provider_invoke(p, p->declaration.initialize[i], inputs,
                                     2, NULL);
  p->initialized = true;
  p->loading = false;
  if (p->has_clients) qa_compat_qc_mod_client_bindings_start(&p->clients);
  /* TS messages.start(): unresolved wiring. */
}
void qa_compat_qc_mod_provider_advance(P *p, qa_frame_context frame) {
  double elapsed = seconds_of(frame.elapsed);
  double time = seconds_of(frame.time) - elapsed;
  const qa_compat_qc_definition *frametime =
      qa_compat_qc_program_global_named(p->program, "frametime");
  if (frametime && text_eq(frametime->type, "float"))
    qa_compat_qc_words_set_float(&p->machine.globals, frametime->offset,
                                 elapsed);
  if (p->declaration.frame) {
    qa_compat_qc_mod_input_entry inputs[3];
    memset(inputs, 0, sizeof inputs);
    inputs[0].name = "self";
    inputs[0].value.kind = "actor";
    inputs[1].name = "time";
    inputs[1].value.kind = "float";
    inputs[1].value.number_value = time;
    inputs[2].name = "elapsed";
    inputs[2].value.kind = "float";
    inputs[2].value.number_value = elapsed;
    qa_compat_qc_mod_provider_invoke(p, *p->declaration.frame, inputs, 3,
                                     NULL);
  }
  /* TS ownedActors.advance(frame): unresolved wiring. */
}
const qa_guest_checkpoint *qa_compat_qc_mod_provider_checkpoint(P *p) {
  /* TS protection.assertIdle(): unresolved wiring. */
  if (p->depth != 0 || qa_compat_qc_mod_input_get_active(&p->input))
    qa_range_error("Mod checkpoint requires an idle callback boundary");
  if (p->has_checkpoint) {
    qa_compat_qc_machine_snapshot_destroy(&p->snapshot);
    memset(&p->snapshot, 0, sizeof p->snapshot);
    free(p->host_state_bytes);
    p->host_state_bytes = NULL;
    p->has_checkpoint = false;
  }
  if (!qa_compat_qc_capture_checkpoint(&p->machine, p->module, p->host_state,
                                       &p->checkpoint_storage, &p->snapshot,
                                       &p->host_state_bytes))
    qa_compat_qc_machine_fail(&p->machine, "checkpoint allocation failed");
  p->has_checkpoint = true;
  return &p->checkpoint_storage;
}
void qa_compat_qc_mod_provider_restore(P *p, const qa_guest_checkpoint *saved) {
  p->presentation_generation++;
  if (p->depth != 0 || qa_compat_qc_mod_input_get_active(&p->input))
    qa_range_error("Mod restore requires an idle callback boundary");
  close_client_bindings(p);
  if (!qa_compat_qc_restore_checkpoint(&p->machine, p->module, p->host_state,
                                       saved))
    qa_compat_qc_machine_fail(&p->machine, "checkpoint restore failed");
}
void qa_compat_qc_mod_provider_close(P *p) {
  if (p->objectives) qa_mod_source_objectives_close(p->objectives);
  if (p->has_release_match) {
    if (p->release_match.invoke) p->release_match.invoke(p->release_match.ctx);
    p->release_match = (qa_source_match_unbind){NULL, NULL};
    p->has_release_match = false;
  }
  if (p->closed) return;
  p->closed = true;
  close_client_bindings(p);
  if (p->has_clients) qa_compat_qc_mod_client_bindings_close(&p->clients);
  /* TS ownedActors.close() / messages.close() / sourcePhysics
   * releases: unresolved wiring. */
  if (p->has_release_subscription) {
    qa_session_actor_registry_off_release(p->services->actors,
                                          p->release_subscription);
    p->has_release_subscription = false;
  }
  p->projection_count = 0;
}
bool qa_compat_qc_mod_provider_init(
    P *p, qa_compat_qc_program *program, qa_module_identity module,
    qa_mod_callback_declaration declaration, qa_mod_host_services *services,
    qa_compat_qc_mod_random *random, const qa_compat_qc_mod_media *media,
    const qa_compat_qc_mod_environment_clients *env_clients,
    const qa_compat_qc_host_binding *extra_host, size_t extra_host_count) {
  memset(p, 0, sizeof *p);
  p->program = program;
  p->module = module;
  p->declaration = declaration;
  p->services = services;
  p->random = random;
  p->media = media;
  p->host_state = (qa_compat_qc_executor_host){p, host_checkpoint,
                                               host_restore};
  qa_compat_qc_validate_mod(program, declaration);
  p->objectives = qa_mod_source_objectives_create(
      module.id, declaration.objectives, declaration.objective_count,
      services->match,
      (qa_mod_objective_storage){p, objectives_current, objectives_location,
                                 objectives_read_scalar, objectives_write_scalar,
                                 objectives_read_actor, objectives_write_actor,
                                 objectives_invoke, objectives_seconds});
  if (!p->objectives) goto fail_free;
  if (declaration.actor_field_count > 0) {
    p->fields =
        xrealloc(NULL, declaration.actor_field_count * sizeof *p->fields);
    p->field_count = declaration.actor_field_count;
    for (size_t i = 0; i < p->field_count; i++) {
      const qa_compat_qc_definition *field = qa_compat_qc_program_field_named(
          program, declaration.actor_fields[i].field);
      if (!field)
        qa_range_error("Missing validated actor field %s",
                       declaration.actor_fields[i].field);
      p->fields[i].declaration = declaration.actor_fields[i];
      p->fields[i].offset = field->offset;
      p->fields[i].words = text_eq(field->type, "vector") ? 3 : 1;
      if (text_eq(declaration.actor_fields[i].binding, "client-input"))
        p->input_field_count++;
    }
  }
  if (p->input_field_count > 0) {
    p->input_fields = xrealloc(NULL, p->input_field_count *
                                         sizeof *p->input_fields);
    size_t at = 0;
    for (size_t i = 0; i < p->field_count; i++)
      if (text_eq(p->fields[i].declaration.binding, "client-input")) {
        p->input_fields[at].offset = p->fields[i].offset;
        p->input_fields[at].words = p->fields[i].words;
        p->input_fields[at].declaration = p->fields[i].declaration;
        at++;
      }
  }
  if (declaration.clients && !services->clients)
    qa_range_error("QuakeC component clients require destination client "
                   "services");
  qa_compat_qc_entity_layout layout =
      qa_compat_qc_classic_entity_layout(program);
  int64_t count = (declaration.clients ? (int64_t)declaration.clients->maximum
                                       : 0) +
                  1;
  if (!qa_compat_qc_entity_memory_init(&p->entities, layout, 8192, count))
    goto fail_free;
  if (!qa_compat_qc_mod_environment_init(&p->environment, services, program,
                                         declaration, p, reference_actor,
                                         env_clients))
    goto fail_entities;
  /* TS host Map: caller-built bindings first, localcmd, then the
   * environment entries (TS installs environment.host last except
   * localcmd, which nothing else provides). */
  {
    size_t host_count = extra_host_count + 1 + p->environment.host_count;
    qa_compat_qc_host_binding *host =
        xrealloc(NULL, host_count * sizeof *host);
    size_t hn = 0;
    for (size_t i = 0; i < extra_host_count; i++) host[hn++] = extra_host[i];
    host[hn++] =
        (qa_compat_qc_host_binding){"localcmd", {p, builtin_localcmd}};
    for (size_t i = 0; i < p->environment.host_count; i++)
      host[hn++] = p->environment.host_entries[i];
    if (declaration.items && !media) {
      free(host);
      qa_range_error("QC source items require their prepared content owner");
    }
    p->numeric = qa_create_numeric_operations(&qa_q1_donor_profile);
    qa_compat_qc_builtin_services builtin_services;
    memset(&builtin_services, 0, sizeof builtin_services);
    builtin_services.kind = qa_compat_qc_host_kind_for_program(program);
    builtin_services.random = random ? &random->source : NULL;
    builtin_services.ctx = p;
    builtin_services.is_free_entity = builtin_is_free_entity;
    builtin_services.prepare_entities = prepare_entities;
    builtin_services.host = host;
    builtin_services.host_count = hn;
    bool built = qa_compat_qc_create_builtins(builtin_services, &p->registry);
    free(host);
    if (!built) goto fail_env;
  }
  {
    qa_compat_qc_machine_options mo;
    memset(&mo, 0, sizeof mo);
    mo.program = program;
    mo.numeric = p->numeric;
    mo.entities = &p->entities;
    mo.builtins = &p->registry;
    mo.ctx = p;
    mo.server_active = server_active;
    mo.observe_call = observe_call;
    mo.observe_entity_store = observe_entity_store;
    mo.validate_entity_access = validate_entity_access;
    if (!qa_compat_qc_machine_init(&p->machine, mo)) goto fail_registry;
  }
  qa_compat_qc_mod_input_init(&p->input, &p->machine, p->input_fields,
                              p->input_field_count, p, input_reference_fn,
                              input_live_fn);
  {
    bool has_claim = services->clients && services->clients->claim_outputs;
    const qa_mod_client_output_declaration *outputs =
        declaration.clients ? declaration.clients->outputs : NULL;
    size_t output_count =
        declaration.clients ? declaration.clients->output_count : 0;
    p->client_outputs = qa_source_mod_client_outputs_create(
        module.id, outputs, output_count, has_claim,
        has_claim ? outputs_claim : NULL, p, outputs_scalar, outputs_vector,
        p);
    if (!p->client_outputs) goto fail_machine;
  }
  if (declaration.clients && services->clients) {
    bool has_think = false;
    for (size_t i = 0; i < p->field_count; i++)
      if (text_eq(p->fields[i].declaration.binding, "think")) has_think = true;
    qa_compat_qc_mod_client_input_ops input_ops;
    memset(&input_ops, 0, sizeof input_ops);
    input_ops.ctx = p;
    input_ops.open = input_open;
    input_ops.invoke = input_invoke;
    input_ops.output = input_output;
    qa_compat_qc_mod_client_operations operations;
    memset(&operations, 0, sizeof operations);
    operations.ctx = p;
    operations.services = services->clients;
    operations.declaration = declaration.clients;
    operations.project = clients_project;
    operations.release = clients_release;
    operations.invoke = clients_invoke;
    operations.think = has_think ? run_client_think : NULL;
    operations.admitted = clients_admitted;
    /* The landed bindings keep only a pointer to the input ops;
     * retain it in provider-owned storage. */
    qa_compat_qc_mod_client_input_ops *retained =
        xrealloc(NULL, sizeof *retained);
    *retained = input_ops;
    operations.input = retained;
    qa_compat_qc_mod_client_bindings_init(&p->clients, operations);
    p->has_clients = true;
  }
  qa_compat_qc_mod_environment_initialize_globals(&p->environment, &p->machine);
  /* TS ownedActors/actorState/combat/protection/pickups/items/
   * messages construction: unresolved wiring (file header). */
  p->release_subscription =
      qa_session_actor_registry_on_release(services->actors, on_actor_release,
                                           p);
  p->has_release_subscription = true;
  return true;
fail_machine:
  qa_compat_qc_mod_input_destroy(&p->input);
  qa_compat_qc_machine_destroy(&p->machine);
fail_registry:
  qa_compat_qc_registry_destroy(&p->registry);
fail_env:
  qa_compat_qc_mod_environment_destroy(&p->environment);
fail_entities:
  qa_compat_qc_entity_memory_destroy(&p->entities);
fail_free:
  qa_mod_source_objectives_destroy(p->objectives);
  free(p->fields);
  free(p->input_fields);
  memset(p, 0, sizeof *p);
  return false;
}
void qa_compat_qc_mod_provider_destroy(P *p) {
  qa_compat_qc_mod_provider_close(p);
  if (p->program) {
    qa_compat_qc_machine_destroy(&p->machine);
    qa_compat_qc_registry_destroy(&p->registry);
    qa_compat_qc_mod_environment_destroy(&p->environment);
    qa_compat_qc_entity_memory_destroy(&p->entities);
  }
  qa_mod_source_objectives_destroy(p->objectives);
  qa_source_mod_client_outputs_destroy(p->client_outputs);
  if (p->has_clients) {
    free((void *)p->clients.operations.input);
    qa_compat_qc_mod_client_bindings_destroy(&p->clients);
  }
  qa_compat_qc_mod_input_destroy(&p->input);
  if (p->has_checkpoint) qa_compat_qc_machine_snapshot_destroy(&p->snapshot);
  free(p->host_state_bytes);
  free(p->host_encoded);
  free(p->objective_location);
  free(p->fields);
  free(p->input_fields);
  free(p->projections);
  free(p->retired);
  free(p->pickup_scopes);
  free(p->match_cells);
  for (size_t i = 0; i < p->precached_count; i++) free(p->precached[i].key);
  free(p->precached);
  memset(p, 0, sizeof *p);
}

