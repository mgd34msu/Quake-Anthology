/* Port of src/compat/qc/mod-provider.ts. GPL-2.0-or-later.
 * Engine scene/physics/events are read through their typed views in
 * world/session/mod_engine.h. */
#include "qa/compat/qc/mod_provider.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/quakec_local_messages.h"
#include "qa/compat/qc/mod_commands.h"
#include "qa/content/composition/q1/types.h"
#include "qa/content/q1/addons/context.h"
#include "qa/compat/qc/mod_objectives.h"
#include "qa/compat/qc/profile.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q1/pusher.h"
#include "qa/movement/q1/water_transition.h"
#include "qa/network/q1/handshake.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/value.h"
#include "qa/world/scheduler.h"
#include "qa/foundation/js.h"

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
         (qa_js_to_int32(output->mask) &
          ~qa_js_to_int32(binding->has_private_mask ? binding->private_mask
                                                    : 0)) != 0))
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
enum { FOREIGN_SLOT_QUARANTINE = 64 };
static int64_t client_maximum(const P *p) { return p->declaration.clients ? (int64_t)p->declaration.clients->maximum : 0; }
static void free_slot_push(P *p, int64_t slot) {
  if (p->free_slot_count == p->free_slot_capacity) {
    p->free_slot_capacity = p->free_slot_capacity ? p->free_slot_capacity * 2 : 16;
    p->free_slots = xrealloc(p->free_slots, p->free_slot_capacity * sizeof *p->free_slots);
  }
  p->free_slots[p->free_slot_count++] = slot;
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
  int64_t reused = -1;
  if (client_slot < 0 && p->free_slot_count > FOREIGN_SLOT_QUARANTINE) {
    reused = p->free_slots[0];
    memmove(p->free_slots, p->free_slots + 1, (p->free_slot_count - 1) * sizeof *p->free_slots);
    p->free_slot_count--;
  }
  int64_t slot = client_slot >= 0 ? client_slot : reused >= 0 ? reused : p->entities.count;
  if (client_slot < 0 && reused < 0)
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
      int32_t canonical = has_client ? 8 | (client.notarget ? 128 : 0) : 0;
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
      /* canonical | (Math.trunc(flags) & privateMask): ToInt32 operands. */
      int32_t private_mask =
          declared->has_private_mask ? qa_js_to_int32(declared->private_mask) : 0;
      qa_compat_qc_words_set_float(
          words, field->offset,
          (double)(canonical | (qa_js_to_int32(qa_compat_qc_words_float(
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
    static const char *const names[2] = {"think", "nextthink"};
    for (int n = 0; n < 2; n++) {
      const qa_compat_qc_definition *definition =
          qa_compat_qc_program_field_named(p->program, names[n]);
      if (definition && store.word <= definition->offset &&
          store.word + store_words > definition->offset) {
        qa_compat_qc_mod_actors_schedule(&p->owned_actors, actor);
        return;
      }
    }
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
      /* (Math.trunc(before) ^ next) & ~privateMask on guest floats: ToInt32. */
      int32_t next = qa_js_to_int32(qa_compat_qc_words_float(words,
                                                            field->offset));
      int32_t private_mask =
          declared->has_private_mask ? qa_js_to_int32(declared->private_mask) : 0;
      int32_t changed =
          (qa_js_to_int32((double)before_bits) ^ next) & ~private_mask;
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
      int64_t maximum = p->declaration.clients
                            ? (int64_t)p->declaration.clients->maximum : 0;
      if (qa_compat_qc_entity_memory_slot(&p->entities, store.reference) > maximum)
        qa_compat_qc_mod_actors_schedule(&p->owned_actors, actor);
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
  /* A freed edict still reads as plain words in id's engine; only writes need a live actor. */
  if (strcmp(kind, "read") == 0 && reference != 0) {
    qa_actor_id held;
    if (!projection_actor_at(p, qa_compat_qc_entity_memory_slot(&p->entities, reference), &held) ||
        !qa_session_actor_registry_is_live(p->services->actors, held))
      return;
  }
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
  if (p->has_combat)
    qa_q1_id1_damage_observe_call(
        p->combat.damage,
        (qa_q1qc_call_site){call.function_index, call.caller, call.statement});
}
static void observe_entity_store(void *ctx,
                                 qa_compat_qc_entity_store_observation store) {
  P *p = ctx;
  validate_pickup_store(p, store);
  if (p->has_protection) qa_compat_qc_mod_protection_observe(&p->protection, store);
  if (p->has_items) {
    const qa_original_pickup_execution *execution = NULL;
    if (p->pickup_scope_count > 0) {
      size_t top = p->pickup_scope_count - 1;
      if (p->pickup_scopes[top].depth == p->depth &&
          actor_eq(p->pickup_scopes[top].actor,
                   qa_compat_qc_mod_provider_actor(p, store.reference)))
        execution = &p->pickup_scopes[top].execution;
    }
    qa_compat_qc_mod_items_observe(&p->items, store, execution);
  }
  write_through(p, store);
  if (p->has_combat)
    qa_q1_id1_damage_observe_entity_store(
        p->combat.damage,
        (qa_q1qc_entity_store_observation){store.function_index, store.statement,
                                           store.reference, store.word, store.before,
                                           store.before_length, store.after,
                                           store.after_length});
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
  return result;
}
/* TS finally blocks: cleanup nodes run when a raise unwinds to a qa_try. */
typedef struct {
  P *p;
  bool had_frame;
  qa_frame_context frame;
} FrameRestore;
static void restore_frame(void *arg) {
  FrameRestore *r = arg;
  r->p->frame = r->frame;
  r->p->has_frame = r->had_frame;
}
static void unwind_depth(void *arg) {
  P *p = arg;
  p->depth--;
  drain_retired_projections(p);
}
static void end_loading(void *arg) { ((P *)arg)->loading = false; }
static void pop_pickup_scope(void *arg) { ((P *)arg)->pickup_scope_count--; }
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
  qa_cleanup scope;
  qa_cleanup_push(&scope, unwind_depth, p);
  ExecCtx exec = {p, call, region};
  double result = qa_compat_qc_with_source_call(
      &p->machine, call, inputs, input_count,
      (qa_compat_qc_actor_reference){p, reference_actor},
      (qa_compat_qc_source_execute){&exec, exec_fn});
  if (p->objectives) qa_mod_source_objectives_flush(p->objectives);
  qa_cleanup_pop(&scope);
  unwind_depth(p);
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
    FrameRestore restore = {p, had_frame, previous};
    qa_cleanup scope;
    qa_cleanup_push(&scope, restore_frame, &restore);
    invoke_owned(p, index, actor, NULL, time.value);
    qa_cleanup_pop(&scope);
    restore_frame(&restore);
    if (!quakeworld) return;
  }
}
static void release_client_bindings(P *p, qa_actor_id actor) {
  if (p->client_outputs)
    qa_source_mod_client_outputs_release(p->client_outputs, actor);
  if (p->has_pickups) qa_compat_qc_mod_pickups_release(&p->pickups, actor);
  if (p->has_items) qa_compat_qc_mod_items_release(&p->items, actor);
  if (p->has_protection) qa_compat_qc_mod_protection_release(&p->protection, actor);
}
static const char *release_client_projection(P *p, qa_actor_id actor) {
  if (p->depth != 0 || qa_compat_qc_mod_input_get_active(&p->input)) {
    retired_add(p, actor);
    return "deferred";
  }
  int64_t slot = projection_slot(p, actor);
  if (slot >= 0) {
    projection_delete(p, actor);
    /* Foreign edicts keep their last words, as a freed edict does; clients are zeroed because their slots are reserved. */
    if (slot > client_maximum(p)) free_slot_push(p, slot);
    else {
      qa_compat_qc_words *words = qa_compat_qc_entity_memory_at(&p->entities, slot);
      memset(words->bytes, 0, words->byte_length);
    }
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
static void source_physics_release_all(P *p);
static void close_client_bindings(P *p) {
  if (p->client_outputs) qa_source_mod_client_outputs_close(p->client_outputs);
  if (p->has_pickups) qa_compat_qc_mod_pickups_close(&p->pickups);
  if (p->has_items) qa_compat_qc_mod_items_close(&p->items);
  if (p->has_protection) qa_compat_qc_mod_protection_close(&p->protection);
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
  if (p->has_items) qa_compat_qc_mod_items_admit(&p->items, actor);
  if (p->has_pickups) qa_compat_qc_mod_pickups_admit(&p->pickups, actor);
  if (p->has_protection) qa_compat_qc_mod_protection_activate(&p->protection, actor);
}
static void clients_reserve(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  if (p->has_protection) qa_compat_qc_mod_protection_reserve(&p->protection, actor);
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
  FrameRestore restore = {p, had_frame, previous};
  qa_cleanup scope;
  qa_cleanup_push(&scope, restore_frame, &restore);
  qa_compat_qc_mod_provider_invoke(p, call, inputs, count, NULL);
  qa_cleanup_pop(&scope);
  restore_frame(&restore);
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
  ok = ok && set_own(root, "messages",
                     p->has_messages ? qa_compat_qc_mod_messages_capture(&p->messages)
                                     : qa_save_value_null());
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
static qa_actor_id resolve_saved_fn(void *ctx, qa_saved_actor_id saved) {
  return resolve_saved(ctx, saved);
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
  p->free_slot_count = 0;
  for (int64_t slot = client_maximum(p) + 1; slot < p->entities.count; slot++) {
    qa_actor_id held;
    if (!projection_actor_at(p, slot, &held)) free_slot_push(p, slot);
  }
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
  if (p->has_messages ? qa_save_reader_is_null(&field_reader)
                      : !qa_save_reader_is_null(&field_reader))
    qa_range_error("Saved mod message services differ");
  if (p->has_messages)
    qa_compat_qc_mod_messages_restore(&p->messages, &field_reader,
                                      (qa_compat_qc_actor_resolver){p, resolve_saved_fn});
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
  qa_compat_qc_mod_actors_restored(&p->owned_actors);
  if (p->objectives) qa_mod_source_objectives_restored(p->objectives);
  if (p->has_clients)
    for (size_t i = 0; i < p->clients.entry_count; i++) {
      qa_actor_id actor = p->clients.entries[i].actor;
      clients_reserve(p, actor);
      if (p->clients.entries[i].admitted) clients_admitted(p, actor);
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
  qa_compat_qc_words * w = match_current(cell);
  int64_t word = team->offset;
  double value2 = qa_original_team(team->declaration.team_values,
                       team->declaration.team_value_count, value);
  qa_compat_qc_words_set_float(
      w, word, value2);
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
  qa_cleanup loading_scope;
  qa_cleanup_push(&loading_scope, end_loading, p);
  if (p->services->clients && p->has_protection) {
    size_t client_count = 0;
    const qa_mod_client_identity *current =
        p->services->clients->clients(p->services->clients->ctx, &client_count);
    for (size_t i = 0; i < client_count; i++)
      qa_compat_qc_mod_protection_reserve(&p->protection, current[i].actor);
  }
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
  qa_cleanup_pop(&loading_scope);
  p->loading = false;
  if (p->has_clients) qa_compat_qc_mod_client_bindings_start(&p->clients);
  if (p->has_messages) qa_compat_qc_mod_messages_start(&p->messages);
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
  /* Source datagrams are built across callbacks and sent when the frame ends (SV_SendClientMessages), so a message split
   * between two callbacks is whole by now. */
  if (p->has_messages) qa_compat_qc_broadcast_messages_flush(&p->messages.messages);
  qa_compat_qc_mod_actors_advance(&p->owned_actors, frame);
}
const qa_guest_checkpoint *qa_compat_qc_mod_provider_checkpoint(P *p) {
  if (p->has_protection) qa_compat_qc_mod_protection_assert_idle(&p->protection);
  if (p->depth != 0 || qa_compat_qc_mod_input_get_active(&p->input))
    qa_range_error("Mod checkpoint requires an idle callback boundary");
  if (p->has_messages) qa_compat_qc_broadcast_messages_flush(&p->messages.messages);
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
  if (p->has_protection) qa_compat_qc_mod_protection_assert_idle(&p->protection);
  if (p->depth != 0 || qa_compat_qc_mod_input_get_active(&p->input))
    qa_range_error("Mod restore requires an idle callback boundary");
  qa_compat_qc_preflight_checkpoint(&p->machine, p->module, saved);
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
  if (p->has_owned_actors) qa_compat_qc_mod_actors_close(&p->owned_actors);
  if (p->has_release_subscription) {
    qa_session_actor_registry_off_release(p->services->actors,
                                          p->release_subscription);
    p->has_release_subscription = false;
  }
  if (p->has_messages) qa_compat_qc_mod_messages_close(&p->messages);
  source_physics_release_all(p);
  p->projection_count = 0;
}
/* ---------------- engine views ---------------- */
static const qa_session_mod_engine_scene *engine_scene(P *p) {
  const qa_session_mod_engine_services *engine = p->services->engine;
  return engine ? (const qa_session_mod_engine_scene *)engine->scene : NULL;
}
static const qa_session_mod_engine_physics *engine_physics(P *p) {
  const qa_session_mod_engine_services *engine = p->services->engine;
  return engine ? (const qa_session_mod_engine_physics *)engine->physics : NULL;
}
static void engine_emit(P *p, qa_content_id content, qa_app_sim_presentation_event event,
                        const qa_actor_id *recipient) {
  const qa_session_mod_engine_services *engine = p->services->engine;
  if (!engine || !engine->events.emit)
    qa_range_error("Source mod event requires destination event services");
  qa_session_mod_emit emit;
  memset(&emit, 0, sizeof emit);
  emit.event = event;
  emit.event.content = content;
  if (recipient) {
    emit.event.has_recipient = true;
    emit.event.recipient = *recipient;
  }
  engine->events.emit(engine->events.ctx, &emit);
}
static void engine_register_resource(P *p, qa_content_id content, const char *path,
                                     qa_resolved_resource_reference resource) {
  const qa_session_mod_engine_services *engine = p->services->engine;
  if (!engine || !engine->events.register_resource) return;
  qa_session_mod_resource_registration registration = {content, path, resource};
  engine->events.register_resource(engine->events.ctx, &registration);
}
static const qa_compat_qc_mod_media_resource *media_find(P *p, const char *name) {
  const qa_compat_qc_mod_media_resource *found = NULL;
  if (p->media)
    for (size_t i = 0; i < p->media->resource_count; i++)
      if (strcmp(p->media->resources[i].name, name) == 0)
        found = &p->media->resources[i];
  return found;
}
static void *owned_cell(P *p, size_t size) {
  if (p->owned_cell_count == p->owned_cell_capacity) {
    p->owned_cell_capacity = p->owned_cell_capacity ? p->owned_cell_capacity * 2 : 8;
    p->owned_cells = xrealloc(p->owned_cells, p->owned_cell_capacity * sizeof *p->owned_cells);
  }
  void *cell = xrealloc(NULL, size);
  memset(cell, 0, size);
  p->owned_cells[p->owned_cell_count++] = cell;
  return cell;
}
static qa_owned_actor owned_of_slot(P *p, int64_t slot, const char *message) {
  qa_actor_id id = qa_compat_qc_mod_provider_actor(
      p, qa_compat_qc_entity_memory_reference(&p->entities, slot));
  qa_owned_actor owned;
  if (!qa_session_actor_registry_resolve_owned(p->services->actors, id, &owned))
    qa_range_error("%s", message);
  return owned;
}
static void host_set(P *p, const char *name, qa_compat_qc_builtin fn) {
  for (size_t i = 0; i < p->host_count; i++)
    if (strcmp(p->host_table[i].name, name) == 0) {
      p->host_table[i].fn = fn;
      return;
    }
  if (p->host_count == sizeof p->host_table / sizeof p->host_table[0])
    qa_range_error("mod provider host table exceeded");
  p->host_table[p->host_count++] = (qa_compat_qc_host_binding){name, fn};
}

/* ---------------- spatial world ---------------- */
static bool spatial_model(void *ctx, const char *name, qa_compat_qc_spatial_model *out) {
  P *p = ctx;
  if (name[0] == '\0') {
    memset(out, 0, sizeof *out);
    return true;
  }
  if (!p->media) return false;
  const qa_compat_qc_mod_media_resource *resource = media_find(p, name);
  qa_compat_qc_precached_resource precached;
  bool has = qa_compat_qc_mod_provider_lookup(p, "model", name, &precached);
  if (!resource || !resource->has_bounds || !has) return false;
  out->index = precached.index;
  out->bounds = resource->model_bounds;
  return true;
}
static qa_owned_actor spatial_actor(void *ctx, int64_t slot) {
  return owned_of_slot(ctx, slot, "Spatial builtin references a released mod actor");
}
static int64_t reference_by_value(void *ctx, qa_actor_id actor) {
  return qa_compat_qc_mod_provider_reference(ctx, &actor);
}
static void spatial_link(void *ctx, int64_t slot) {
  P *p = ctx;
  qa_shared_body_table_link(p->services->bodies,
                            owned_of_slot(p, slot, "Cannot link a released mod actor"), NULL);
}

/* ---------------- world host (movement, pusher, aim) ---------------- */
static bool no_model(void *ctx, const char *name, qa_compat_qc_spatial_model *out) {
  (void)ctx; (void)name; (void)out;
  return false;
}
static qa_owned_actor movement_actor(void *ctx, int64_t slot) {
  return owned_of_slot(ctx, slot, "Movement references a released mod actor");
}
static void builtin_spawn(void *ctx, qa_compat_qc_machine *m) {
  P *p = ctx;
  p->owned_actors.spawn.fn(p->owned_actors.spawn.ctx, m);
}
static void builtin_remove(void *ctx, qa_compat_qc_machine *m) {
  P *p = ctx;
  p->owned_actors.remove.fn(p->owned_actors.remove.ctx, m);
}
typedef struct { P *p; const char *name; } MovementWrap;
static void builtin_movement(void *ctx, qa_compat_qc_machine *m) {
  MovementWrap *wrap = ctx;
  P *p = wrap->p;
  if (p->has_movement)
    for (size_t i = 0; i < p->movement_bindings.count; i++)
      if (strcmp(p->movement_bindings.entries[i].name, wrap->name) == 0) {
        qa_compat_qc_builtin fn = p->movement_bindings.entries[i].fn;
        fn.fn(fn.ctx, m);
        return;
      }
  qa_compat_qc_machine_fail(m, "Source movement service %s was not initialized", wrap->name);
}
static void builtin_aim(void *ctx, qa_compat_qc_machine *m) {
  P *p = ctx;
  p->aim_builtin.fn(p->aim_builtin.ctx, m);
}
static double aim_threshold(void *ctx) {
  return qa_cvar_registry_variable_value(((P *)ctx)->environment.cvars, "sv_aim");
}
static double aim_teamplay(void *ctx) {
  return qa_cvar_registry_variable_value(((P *)ctx)->environment.cvars, "teamplay");
}
static bool aim_no_aim(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  qa_mod_client_services *clients = p->services->clients;
  qa_client_id client;
  if (p->program->api.kind != QA_QUAKEC_API_Q1_QUAKEWORLD || !clients ||
      !clients->for_actor(clients->ctx, actor, &client))
    return false;
  qa_qw_info info;
  qa_qw_info_init(&info);
  qa_quakeworld_info_parse(clients->userinfo(clients->ctx, client), &info);
  const char *value = qa_qw_info_get(&info, "noaim");
  bool result = qa_native_atoi(value ? value : "0") > 0;
  qa_qw_info_destroy(&info);
  return result;
}
static const qa_compat_qc_aim_target_ref *aim_targets(void *ctx, size_t *out_count) {
  P *p = ctx;
  prepare_entities(p);
  size_t count = 0;
  for (int64_t slot = 1; slot < p->entities.count; slot++) {
    qa_actor_id actor;
    if (!projection_actor_at(p, slot, &actor) ||
        !qa_session_actor_registry_is_live(p->services->actors, actor))
      continue;
    if (count == p->aim_target_capacity) {
      p->aim_target_capacity = p->aim_target_capacity ? p->aim_target_capacity * 2 : 32;
      p->aim_targets = xrealloc(p->aim_targets, p->aim_target_capacity * sizeof *p->aim_targets);
    }
    p->aim_targets[count].actor = actor;
    p->aim_targets[count].reference = qa_compat_qc_entity_memory_reference(&p->entities, slot);
    count++;
  }
  *out_count = count;
  return p->aim_targets;
}
static void physics_touch_triggers(void *ctx, qa_owned_actor actor) {
  const qa_session_mod_engine_physics *physics = engine_physics(ctx);
  physics->touch_triggers(physics->ctx, actor);
}
static bool movement_actor_reference(void *ctx, int64_t reference, qa_actor_id *out) {
  P *p = ctx;
  qa_actor_id actor;
  if (!projection_actor_at(p, qa_compat_qc_entity_memory_slot(&p->entities, reference), &actor) ||
      !qa_session_actor_registry_is_live(p->services->actors, actor))
    return false;
  *out = actor;
  return true;
}

/* ---------------- pusher ---------------- */
static qa_q1_pusher_services pusher_physical(void *ctx, qa_compat_qc_pusher_projection projection) {
  P *p = ctx;
  const qa_session_mod_engine_physics *physics = engine_physics(p);
  memset(&p->pusher_env, 0, sizeof p->pusher_env);
  p->pusher_env.projection_ctx = projection.ctx;
  p->pusher_env.read = projection.read;
  p->pusher_env.write = projection.write;
  p->pusher_env.link = projection.link;
  p->pusher_env.blocked = projection.blocked;
  return physics->q1_pusher_services(physics->ctx, &p->pusher_env);
}
static bool pusher_foreign_read(void *ctx, qa_actor_id actor, qa_q1_physics_entity *out) {
  const qa_session_mod_engine_physics *physics = engine_physics(ctx);
  return physics->read_q1_pusher(physics->ctx, actor, out);
}
static void pusher_foreign_write(void *ctx, qa_q1_physics_entity entity) {
  const qa_session_mod_engine_physics *physics = engine_physics(ctx);
  physics->write_q1_pusher(physics->ctx, entity);
}
static double pusher_server_time(void *ctx) {
  P *p = ctx;
  return seconds_of(p->has_frame ? p->frame.time : p->services->time(p->services->ctx));
}
static void pusher_invoke(void *ctx, qa_owned_actor actor, const char *callback,
                          const qa_actor_id *other) {
  const qa_compat_qc_words * w;
  int64_t word;
  P *p = ctx;
  if (!p->has_frame) qa_range_error("Mod pusher callback needs an active source frame");
  if (strcmp(callback, "think") == 0) {
    if (!p->services->callbacks) qa_range_error("Mod pusher think requires shared callbacks");
    qa_actor_callback_table_think(p->services->callbacks, actor, p->frame);
    return;
  }
  qa_actor_source_result source = qa_session_actor_registry_source_of(p->services->actors, actor.id);
  if (source.has_value)
    invoke_owned(p,
                 (w = qa_compat_qc_entity_memory_at(&p->entities, source.slot), word = qa_compat_qc_machine_field_offset(&p->machine, "blocked"), qa_compat_qc_words_int(
                     w,
                     word)),
                 actor.id, other, p->frame.time);
}

/* ---------------- actor state ---------------- */
static bool state_source_slot(void *ctx, qa_actor_id actor, int64_t *out) {
  P *p = ctx;
  qa_actor_source_result source = qa_session_actor_registry_source_of(p->services->actors, actor);
  if (!source.has_value || !text_eq(source.provider, p->module.id)) return false;
  *out = source.slot;
  return true;
}
static bool state_reference(void *ctx, int64_t reference, qa_actor_id *out) {
  *out = qa_compat_qc_mod_provider_actor(ctx, reference);
  return true;
}
static bool state_is_client(void *ctx, qa_actor_id actor) {
  (void)ctx; (void)actor;
  return false;
}

/* ---------------- water transition ---------------- */
static void water_transition(P *p, qa_actor_id actor) {
  const qa_session_mod_engine_services *engine = p->services->engine;
  qa_actor_source_result source = qa_session_actor_registry_source_of(p->services->actors, actor);
  if (!engine || !source.has_value) return;
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_at(&p->entities, source.slot);
  int64_t watertype = qa_compat_qc_machine_field_offset(&p->machine, "watertype");
  int64_t waterlevel = qa_compat_qc_machine_field_offset(&p->machine, "waterlevel");
  qa_point_contents_query query;
  memset(&query, 0, sizeof query);
  query.point = qa_compat_qc_words_vector(words, qa_compat_qc_machine_field_offset(&p->machine, "origin"));
  query.target.is_model = false;
  query.policy.kind = "q1";
  query.policy.move = "normal";
  query.numeric = qa_q1_donor_profile;
  query.pass_actor = &actor;
  const qa_session_mod_engine_scene *scene = engine_scene(p);
  qa_point_contents_result contents = scene->queries.point_contents(scene->queries.ctx, query);
  if (!text_eq(contents.kind, "q1"))
    qa_range_error("QC mod water transition requires source Q1 contents");
  qa_q1_water_transition_result decision =
      qa_q1_water_transition(qa_compat_qc_words_float(words, watertype), contents.contents);
  if (decision.splash && p->media) {
    const char *path = "misc/h2ohit1.wav";
    const qa_compat_qc_mod_media_resource *asset = media_find(p, path);
    if (asset) {
      engine_register_resource(p, p->media->content, asset->resource.requested_path, asset->resource);
      qa_q1_event event;
      memset(&event, 0, sizeof event);
      event.kind = "sound";
      event.has_actor = true;
      event.actor = actor;
      event.path = path;
      event.sound_channel = "auto";
      event.volume = 1;
      event.attenuation = 1;
      qa_app_sim_presentation_event wrapped;
      memset(&wrapped, 0, sizeof wrapped);
      wrapped.kind = "q1";
      wrapped.q1 = &event;
      engine_emit(p, p->media->content, wrapped, NULL);
    }
  }
  qa_compat_qc_words_set_float(words, watertype, decision.water_type);
  qa_compat_qc_words_set_float(words, waterlevel, decision.water_level);
}

/* ---------------- source physics binding ---------------- */
struct qa_compat_qc_source_physics_cell {
  P *p;
  qa_owned_actor actor;
  int handle;
  qa_actor_id owner_storage, enemy_storage;
};
typedef struct qa_compat_qc_source_physics_cell PhysicsCell;
static bool physics_collision(void *ctx, qa_app_sim_shared_solid *out) {
  PhysicsCell *c = ctx;
  qa_compat_qc_shared_solid solid;
  if (!qa_compat_qc_actor_state_collision(&c->p->actor_state, c->actor, &solid)) return false;
  memset(out, 0, sizeof *out);
  out->solid = solid.solid;
  out->has_model = solid.has_model;
  out->model = solid.model;
  out->family = solid.family;
  if (solid.has_owner) {
    c->owner_storage = solid.owner;
    out->owner = &c->owner_storage;
  }
  out->has_monster = solid.has_monster;
  out->monster = solid.monster;
  out->has_dead_monster = solid.has_dead_monster;
  out->dead_monster = solid.dead_monster;
  out->q1_corpse = solid.q1_corpse;
  out->has_item = solid.has_item;
  out->item = solid.item;
  return true;
}
static bool physics_motion(void *ctx, qa_q2_motion *out) {
  PhysicsCell *c = ctx;
  qa_body_state_result body = qa_shared_body_table_read(c->p->services->bodies, c->actor.id);
  return body.has_value &&
         qa_compat_qc_actor_state_motion(&c->p->actor_state, c->actor, body.value, out);
}
static qa_app_sim_shared_physics_flags physics_flags(void *ctx) {
  PhysicsCell *c = ctx;
  qa_compat_qc_shared_physics_flags f =
      qa_compat_qc_actor_state_flags(&c->p->actor_state, c->actor);
  qa_app_sim_shared_physics_flags out;
  memset(&out, 0, sizeof out);
  out.has_always_touch = f.has_always_touch; out.always_touch = f.always_touch;
  out.has_fly = f.has_fly; out.fly = f.fly;
  out.has_swim = f.has_swim; out.swim = f.swim;
  out.has_partial_ground = f.has_partial_ground; out.partial_ground = f.partial_ground;
  out.has_dead = f.has_dead; out.dead = f.dead;
  out.has_player = f.has_player; out.player = f.player;
  out.has_water_level = f.has_water_level; out.water_level = f.water_level;
  out.has_water_type = f.has_water_type; out.water_type = f.water_type;
  out.has_enemy = f.has_enemy;
  if (f.has_enemy && !f.enemy_is_null) {
    c->enemy_storage = f.enemy;
    out.enemy = &c->enemy_storage;
  }
  out.has_delta_yaw = f.has_delta_yaw; out.delta_yaw = f.delta_yaw;
  out.has_team_slave = f.has_team_slave; out.team_slave = f.team_slave;
  { /* TS QuakeCActorState.flags() literal order. */
    static const unsigned char order[] = {QA_APP_SIM_FLAG_FLY, QA_APP_SIM_FLAG_SWIM, QA_APP_SIM_FLAG_PARTIAL_GROUND, QA_APP_SIM_FLAG_PLAYER,
                                          QA_APP_SIM_FLAG_WATER_LEVEL, QA_APP_SIM_FLAG_WATER_TYPE, QA_APP_SIM_FLAG_DEAD};
    qa_app_sim_physics_flags_order(&out, order, sizeof order);
  }
  return out;
}
static void physics_write_flags(void *ctx, qa_app_sim_shared_physics_flags f) {
  PhysicsCell *c = ctx;
  qa_compat_qc_shared_physics_flags out;
  memset(&out, 0, sizeof out);
  out.has_always_touch = f.has_always_touch; out.always_touch = f.always_touch;
  out.has_fly = f.has_fly; out.fly = f.fly;
  out.has_swim = f.has_swim; out.swim = f.swim;
  out.has_partial_ground = f.has_partial_ground; out.partial_ground = f.partial_ground;
  out.has_dead = f.has_dead; out.dead = f.dead;
  out.has_player = f.has_player; out.player = f.player;
  out.has_water_level = f.has_water_level; out.water_level = f.water_level;
  out.has_water_type = f.has_water_type; out.water_type = f.water_type;
  out.has_enemy = f.has_enemy;
  out.enemy_is_null = f.has_enemy && f.enemy == NULL;
  if (f.enemy) out.enemy = *f.enemy;
  out.has_delta_yaw = f.has_delta_yaw; out.delta_yaw = f.delta_yaw;
  out.has_team_slave = f.has_team_slave; out.team_slave = f.team_slave;
  qa_compat_qc_actor_state_write_flags(&c->p->actor_state, c->actor, out);
}
static void physics_write_angular(void *ctx, qa_vec3 value) {
  PhysicsCell *c = ctx;
  qa_compat_qc_actor_state_write_angular_velocity(&c->p->actor_state, c->actor, value);
}
static void physics_water(void *ctx) {
  PhysicsCell *c = ctx;
  water_transition(c->p, c->actor.id);
}
static void source_physics_unbind(P *p, qa_actor_id actor) {
  for (size_t i = 0; i < p->source_physics_count; i++)
    if (actor_eq(p->source_physics[i]->actor.id, actor)) {
      PhysicsCell *cell = p->source_physics[i];
      const qa_session_mod_engine_physics *physics = engine_physics(p);
      if (physics) physics->unbind_source(physics->ctx, cell->handle);
      free(cell);
      memmove(&p->source_physics[i], &p->source_physics[i + 1],
              (p->source_physics_count - i - 1) * sizeof *p->source_physics);
      p->source_physics_count--;
      return;
    }
}
static void source_physics_release_all(P *p) {
  while (p->source_physics_count > 0)
    source_physics_unbind(p, p->source_physics[0]->actor.id);
}

/* ---------------- owned actors ---------------- */
static qa_body_state_binding actors_body(void *ctx, int64_t slot);
static int64_t body_reference(void *ctx, qa_actor_id actor) {
  return qa_compat_qc_mod_provider_reference(ctx, &actor);
}
static bool body_actor(void *ctx, int64_t reference, qa_actor_id *out) {
  P *p = ctx;
  if (reference == 0) return world_actor(p, out);
  return movement_actor_reference(p, reference, out);
}
static qa_body_state_binding actors_body(void *ctx, int64_t slot) {
  P *p = ctx;
  qa_compat_qc_body_references references = {p, body_reference, body_actor};
  return qa_compat_qc_create_body_binding(p->program, &p->entities, slot, references,
                                          &p->body_states[slot]);
}
static bool actors_client_frame(void *ctx, int64_t slot, qa_frame_context frame) {
  P *p = ctx;
  return p->has_clients && qa_compat_qc_mod_client_bindings_frame(&p->clients, slot, frame);
}
static void actors_retired(void *ctx, qa_owned_actor actor) { source_physics_unbind(ctx, actor.id); }
static void actors_admitted(void *ctx, qa_owned_actor actor, int64_t slot) {
  (void)slot;
  P *p = ctx;
  if (p->has_combat) qa_compat_qc_mod_combat_admit(&p->combat, actor);
  source_physics_unbind(p, actor.id);
  const qa_session_mod_engine_physics *physics = engine_physics(p);
  if (!physics) return;
  PhysicsCell *cell = xrealloc(NULL, sizeof *cell);
  memset(cell, 0, sizeof *cell);
  cell->p = p;
  cell->actor = actor;
  qa_app_sim_source_body_physics source = {cell, physics_collision, physics_motion, physics_flags,
                                           physics_write_flags, physics_write_angular, physics_water};
  cell->handle = physics->bind_source(physics->ctx, actor, source);
  if (p->source_physics_count == p->source_physics_capacity) {
    p->source_physics_capacity = p->source_physics_capacity ? p->source_physics_capacity * 2 : 16;
    p->source_physics = xrealloc(p->source_physics,
                                 p->source_physics_capacity * sizeof *p->source_physics);
  }
  p->source_physics[p->source_physics_count++] = cell;
}
static void actors_touch(void *ctx, qa_owned_actor actor, qa_actor_id other) {
  P *p = ctx;
  qa_actor_source_result source = qa_session_actor_registry_source_of(p->services->actors, actor.id);
  if (!source.has_value) return;
  const qa_compat_qc_words * w = qa_compat_qc_entity_memory_at(&p->entities, source.slot);
  int64_t word = qa_compat_qc_machine_field_offset(&p->machine, "touch");
  int32_t index = qa_compat_qc_words_int(w, word);
  if (index != 0)
    invoke_owned(p, index, actor.id, &other,
                 p->has_frame ? p->frame.time : p->services->time(p->services->ctx));
}
/* TS invokeActorField. */
static void invoke_actor_field(P *p, qa_actor_id actor, const char *field, const qa_actor_id *other,
                               const qa_actor_id *activator) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(p->services->actors, actor);
  if (!source.has_value) return;
  const qa_compat_qc_words * w = qa_compat_qc_entity_memory_at(&p->entities, source.slot);
  int64_t word = qa_compat_qc_machine_field_offset(&p->machine, field);
  int32_t index = qa_compat_qc_words_int(w, word);
  if (index == 0) return;
  const qa_compat_qc_function *fn = qa_compat_qc_program_function_at(p->program, index);
  qa_source_time now = p->has_frame ? p->frame.time : p->services->time(p->services->ctx);
  qa_actor_id self_copy = actor, other_copy, activator_copy;
  if (other) other_copy = *other;
  if (activator) activator_copy = *activator;
  qa_mod_named_value globals[4] = {
      {"self", {"input", "self", 0, NULL, {0, 0, 0}}},
      {"other", {"input", "other", 0, NULL, {0, 0, 0}}},
      {"activator", {"input", "activator", 0, NULL, {0, 0, 0}}},
      {"time", {"input", "time", 0, NULL, {0, 0, 0}}}};
  qa_compat_qc_mod_input_entry inputs[4];
  memset(inputs, 0, sizeof inputs);
  inputs[0].name = "self"; inputs[0].value.kind = "actor"; inputs[0].value.actor_value = &self_copy;
  inputs[1].name = "other"; inputs[1].value.kind = "actor";
  inputs[1].value.actor_value = other ? &other_copy : NULL;
  inputs[2].name = "activator"; inputs[2].value.kind = "actor";
  inputs[2].value.actor_value = activator ? &activator_copy : NULL;
  inputs[3].name = "time"; inputs[3].value.kind = "float"; inputs[3].value.number_value = seconds_of(now);
  qa_compat_qc_mod_provider_invoke(p, (qa_mod_source_call){fn->name, NULL, 0, globals, 4}, inputs, 4, NULL);
}
static void actors_use(void *ctx, qa_owned_actor actor, const qa_actor_id *other,
                       const qa_actor_id *activator) {
  invoke_actor_field(ctx, actor.id, "use", other, activator);
}
static void actors_pain(void *ctx, qa_pain_reaction reaction) {
  P *p = ctx;
  if (p->has_combat) qa_compat_qc_mod_combat_pain(&p->combat, reaction);
}
static void actors_die(void *ctx, qa_death_reaction reaction) {
  P *p = ctx;
  if (p->has_combat) qa_compat_qc_mod_combat_die(&p->combat, reaction);
}
static void actors_remember(void *ctx, qa_actor_id actor, int64_t slot) {
  P *p = ctx;
  qa_actor_id previous;
  if (projection_actor_at(p, slot, &previous)) projection_delete(p, previous);
  projection_set(p, actor, slot);
}
static void actors_invoke(void *ctx, qa_owned_actor actor, int64_t function_index, qa_frame_context frame) {
  invoke_owned(ctx, function_index, actor.id, NULL, frame.time);
}
/* TS app/bootstrap/simulation/actor-execution.ts executeQuakeCPhysics.
 * The landed qa_app_sim_execute_quakec_physics is a host hook on the
 * runtime's own execution table; the mod provider runs the same
 * movetype dispatch against its engine physics pick. */
static void execute_physics(P *p, qa_owned_actor actor, double move, qa_frame_context frame,
                            void *think_ctx, void (*think)(void *)) {
  const qa_session_mod_engine_physics *physics = engine_physics(p);
  double elapsed = frame.elapsed.value;
  if (move == 7) {
    qa_q1_pusher_result result = qa_step_q1_pusher(
        (qa_q1_pusher_input){actor.id, elapsed, "translate"}, p->pusher_services);
    free(result.moved);
    return;
  }
  if (move == 4) {
    physics->step(physics->ctx, actor, elapsed);
    if (qa_session_actor_registry_is_live(p->services->actors, actor.id)) think(think_ctx);
    if (qa_session_actor_registry_is_live(p->services->actors, actor.id)) water_transition(p, actor.id);
    return;
  }
  if (move != 0 && move != 5 && move != 6 && move != 8 && move != 9 && move != 10)
    qa_range_error("Unsupported nonclient QuakeC movetype %s", QA_JS_NUMBER(move));
  think(think_ctx);
  if (!qa_session_actor_registry_is_live(p->services->actors, actor.id) || move == 0) return;
  if (move == 8) {
    qa_body_state_result body = qa_shared_body_table_read(p->services->bodies, actor.id);
    qa_q2_motion motion;
    if (!body.has_value ||
        !qa_compat_qc_actor_state_motion(&p->actor_state, actor, body.value, &motion))
      return;
    qa_body_state next = body.value;
    next.origin.x += body.value.velocity.x * elapsed;
    next.origin.y += body.value.velocity.y * elapsed;
    next.origin.z += body.value.velocity.z * elapsed;
    next.angles.x += motion.angular_velocity.x * elapsed;
    next.angles.y += motion.angular_velocity.y * elapsed;
    next.angles.z += motion.angular_velocity.z * elapsed;
    qa_shared_body_table_write(p->services->bodies, actor, next);
    qa_shared_body_table_link(p->services->bodies, actor, NULL);
    return;
  }
  physics->step(physics->ctx, actor, elapsed);
}
static void actors_step(void *ctx, qa_owned_actor actor, qa_frame_context frame, void *think_ctx,
                        void (*think)(void *)) {
  P *p = ctx;
  FrameRestore restore = {p, false, frame};
  qa_cleanup scope;
  qa_cleanup_push(&scope, restore_frame, &restore);
  p->frame = frame;
  p->has_frame = true;
  qa_actor_source_result source = qa_session_actor_registry_source_of(p->services->actors, actor.id);
  if (source.has_value) {
    const qa_compat_qc_words * w = qa_compat_qc_entity_memory_at(&p->entities, source.slot);
    int64_t word = qa_compat_qc_machine_field_offset(&p->machine, "movetype");
    double move = qa_compat_qc_words_float(w, word);
    if (move == 0) {
      think(think_ctx);
    } else {
      if (!engine_physics(p) || !p->has_pusher)
        qa_range_error("Moving mod actors require destination source physics");
      execute_physics(p, actor, move, frame, think_ctx, think);
    }
  }
  qa_cleanup_pop(&scope);
  p->has_frame = false;
}

/* ---------------- combat ---------------- */
static qa_actor_id combat_actor(void *ctx, int64_t reference) {
  return qa_compat_qc_mod_provider_actor(ctx, reference);
}
static double combat_invoke(void *ctx, qa_mod_source_call call,
                            const qa_compat_qc_mod_input_entry *inputs, size_t input_count) {
  return qa_compat_qc_mod_provider_invoke(ctx, call, inputs, input_count, NULL);
}
static const char *combat_player_team(void *ctx, qa_actor_id actor) {
  const qa_source_match_services *match = ((P *)ctx)->services->match;
  if (!match) return NULL;
  qa_source_match_player_result player = match->player(match->ctx, actor);
  return player.has_value ? player.value.team(player.value.ctx) : NULL;
}
static void combat_player_set_team(void *ctx, qa_actor_id actor, const char *team) {
  const qa_source_match_services *match = ((P *)ctx)->services->match;
  if (!match) return;
  qa_source_match_player_result player = match->player(match->ctx, actor);
  if (player.has_value) player.value.set_team(player.value.ctx, team);
}
static bool combat_slot_free(void *ctx, int64_t slot) {
  P *p = ctx;
  qa_owned_actor owned;
  return !qa_source_actor_slots_at(p->owned_actors.slots, slot, &owned);
}

/* ---------------- pickups ---------------- */
typedef struct {
  qa_shared_inventory_table *table;
  qa_owned_actor actor;
  int64_t id;
} PickupBindCell;
static void pickup_bind_remove(void *ctx) {
  PickupBindCell *cell = ctx;
  qa_shared_inventory_table_unbind_pickup(cell->table, cell->actor, cell->id);
}
static bool pickups_bind(void *ctx, qa_owned_actor actor, const qa_original_pickup_rule *rules,
                         size_t rule_count, void (**remove)(void *), void **remove_ctx) {
  P *p = ctx;
  PickupBindCell *cell = owned_cell(p, sizeof *cell);
  cell->table = p->services->inventory;
  cell->actor = actor;
  cell->id = qa_shared_inventory_table_bind_pickup(
      p->services->inventory, actor, (qa_inventory_pickup_binding){p->module.id, rules, rule_count});
  *remove = pickup_bind_remove;
  *remove_ctx = cell;
  return true;
}
static bool ops_resolve_owned(void *ctx, qa_actor_id actor, qa_owned_actor *out) {
  return qa_session_actor_registry_resolve_owned(((P *)ctx)->services->actors, actor, out);
}
static bool ops_client_of(void *ctx, qa_actor_id actor, qa_client_id *out) {
  qa_mod_client_services *clients = ((P *)ctx)->services->clients;
  return clients && clients->for_actor(clients->ctx, actor, out);
}
static bool ops_actor_of_client(void *ctx, qa_client_id client, qa_actor_id *out) {
  qa_mod_client_services *clients = ((P *)ctx)->services->clients;
  return clients && clients->actor(clients->ctx, client, out);
}
typedef struct {
  void *run_ctx;
  qa_original_pickup_decision (*run)(void *);
  qa_original_pickup_decision decision;
} PickupRun;
static double pickup_run_fn(void *ctx) {
  PickupRun *run = ctx;
  run->decision = run->run(run->run_ctx);
  return 0;
}
static qa_original_pickup_decision pickups_watch(void *ctx, qa_actor_id actor,
                                                 qa_original_pickup_execution observer, void *run_ctx,
                                                 qa_original_pickup_decision (*run)(void *)) {
  P *p = ctx;
  if (p->pickup_scope_count == p->pickup_scope_capacity) {
    p->pickup_scope_capacity = p->pickup_scope_capacity ? p->pickup_scope_capacity * 2 : 4;
    p->pickup_scopes = xrealloc(p->pickup_scopes, p->pickup_scope_capacity * sizeof *p->pickup_scopes);
  }
  size_t index = p->pickup_scope_count++;
  p->pickup_scopes[index].actor = actor;
  p->pickup_scopes[index].execution = observer;
  p->pickup_scopes[index].depth = p->depth + 1;
  PickupRun cell = {run_ctx, run, QA_ORIGINAL_PICKUP_REFUSED};
  qa_cleanup scope;
  qa_cleanup_push(&scope, pop_pickup_scope, p);
  if (p->has_protection) {
    qa_compat_qc_mod_protection_watch(&p->protection, actor, observer.observer, &cell, pickup_run_fn);
  } else {
    pickup_run_fn(&cell);
  }
  qa_cleanup_pop(&scope);
  p->pickup_scope_count--;
  return cell.decision;
}
static size_t protection_pickups(void *ctx, qa_actor_id actor, qa_protection_channel channel,
                                 const qa_original_pickup_rule **out, size_t capacity) {
  P *p = ctx;
  return p->has_pickups ? qa_compat_qc_mod_pickups_protection(&p->pickups, actor, channel, out, capacity) : 0;
}

/* ---------------- items ---------------- */
static qa_compat_qc_machine *items_machine(void *ctx) { return &((P *)ctx)->machine; }
typedef struct {
  void *state_ctx;
  void (*invoke)(void *, qa_item_id, const char *);
} ItemInvokeCell;
static void item_invoke_adapter(void *ctx, qa_item_id item, qa_source_item_action action) {
  ItemInvokeCell *cell = ctx;
  cell->invoke(cell->state_ctx, item, qa_source_item_action_name(action));
}
static qa_source_item_lease items_bind(void *ctx, qa_owned_actor actor, const qa_source_item_admission *items,
                                       size_t count, void *state_ctx,
                                       const qa_inventory_entry *(*read)(void *, size_t *),
                                       bool (*entry)(void *, qa_item_id, qa_inventory_entry *),
                                       void (*write)(void *, qa_inventory_entry),
                                       bool (*mutable_capacity)(void *, qa_item_id),
                                       void (*invoke_action)(void *, qa_item_id, const char *)) {
  P *p = ctx;
  ItemInvokeCell *cell = owned_cell(p, sizeof *cell);
  cell->state_ctx = state_ctx;
  cell->invoke = invoke_action;
  qa_inventory_item_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.owner = p->module.id;
  binding.items = items;
  binding.item_count = count;
  binding.state = (qa_inventory_state_binding){state_ctx, read, entry, write, mutable_capacity};
  binding.invoke_ctx = cell;
  binding.invoke = item_invoke_adapter;
  return qa_shared_inventory_table_bind_items(p->services->inventory, actor, binding);
}
static bool items_client_current(void *ctx, qa_actor_id actor) {
  qa_client_id client;
  qa_actor_id back;
  return ops_client_of(ctx, actor, &client) && ops_actor_of_client(ctx, client, &back) &&
         actor_eq(back, actor);
}
static bool items_weapon_bind(void *ctx, qa_owned_actor actor, qa_source_weapon_binding binding,
                              void (**remove)(void *), void **remove_ctx) {
  const qa_source_weapon_services *weapons = ((P *)ctx)->services->weapons;
  if (!weapons) return false;
  qa_source_weapon_unbind unbind = weapons->bind(weapons->ctx, actor, binding);
  *remove = unbind.invoke;
  *remove_ctx = unbind.ctx;
  return true;
}
static bool items_weapon_selected(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  return p->services->weapons &&
         p->services->weapons->selected(p->services->weapons->ctx, actor, p->module.id);
}
static double items_count(void *ctx, qa_actor_id actor, qa_item_id item) {
  return qa_shared_inventory_table_count(((P *)ctx)->services->inventory, actor, item);
}
static double items_seconds(void *ctx) {
  P *p = ctx;
  return seconds_of(p->services->time(p->services->ctx));
}
static bool items_media(void *ctx, const char *path, qa_resolved_resource_reference *out) {
  const qa_compat_qc_mod_media_resource *resource = media_find(ctx, path);
  if (!resource) return false;
  *out = resource->resource;
  return true;
}

/* ---------------- machine boundaries ---------------- */
static void qm_fail(void *ctx, const char *message) {
  qa_compat_qc_machine_fail(&((P *)ctx)->machine, "%s", message);
}
static void qm_execute(void *ctx, int64_t fn, int64_t argc) {
  qa_compat_qc_machine_execute(&((P *)ctx)->machine, fn, argc);
}
static double qm_execute_region(void *ctx, qa_q1qc_inline_region r, int64_t argc) {
  qa_compat_qc_inline_region region = {r.function_index, r.entry, r.exit, r.replaceable,
                                       r.has_standalone, r.standalone_saved, r.standalone_scope};
  return qa_compat_qc_machine_execute_region(&((P *)ctx)->machine, region, argc);
}
static const char *qm_strings_get(void *ctx, int64_t reference, char *buf, size_t size) {
  const char *s = qa_compat_qc_strings_get(&((P *)ctx)->machine.strings, reference);
  if (buf && size) {
    snprintf(buf, size, "%s", s);
    return buf;
  }
  return s;
}
static int64_t qm_strings_set_engine(void *ctx, const char *name, const char *text, size_t capacity) {
  return qa_compat_qc_strings_set_engine(&((P *)ctx)->machine.strings, name, text, (int64_t)capacity);
}
static bool build_qprogram(P *p) {
  qa_compat_qc_program *q = p->program;
  p->qglobals = calloc(q->global_count ? q->global_count : 1, sizeof *p->qglobals);
  p->qfields = calloc(q->field_count ? q->field_count : 1, sizeof *p->qfields);
  p->qfunctions = calloc(q->function_count ? q->function_count : 1, sizeof *p->qfunctions);
  p->qstatements = calloc(q->statement_count ? q->statement_count : 1, sizeof *p->qstatements);
  if (!p->qglobals || !p->qfields || !p->qfunctions || !p->qstatements) return false;
  for (size_t i = 0; i < q->global_count; i++)
    p->qglobals[i] = (qa_q1qc_definition){q->globals[i].type, q->globals[i].native_type, q->globals[i].save,
                                          q->globals[i].offset, q->globals[i].name};
  for (size_t i = 0; i < q->field_count; i++)
    p->qfields[i] = (qa_q1qc_definition){q->fields[i].type, q->fields[i].native_type, q->fields[i].save,
                                         q->fields[i].offset, q->fields[i].name};
  for (size_t i = 0; i < q->function_count; i++)
    p->qfunctions[i] = (qa_q1qc_function){q->functions[i].index, q->functions[i].first_statement,
                                          q->functions[i].parameter_start, q->functions[i].local_words,
                                          q->functions[i].name, q->functions[i].file,
                                          q->functions[i].parameter_sizes, q->functions[i].parameter_size_count,
                                          q->functions[i].named_builtin};
  for (size_t i = 0; i < q->statement_count; i++)
    p->qstatements[i] = (qa_q1qc_statement){(qa_q1qc_opcode)q->statements[i].opcode, q->statements[i].a,
                                            q->statements[i].b, q->statements[i].c};
  p->qprogram = (qa_q1qc_program){q->source, q->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD ? "q1-quakeworld" : "q1-netquake",
                                  p->qstatements, q->statement_count, p->qglobals, q->global_count,
                                  p->qfields, q->field_count, p->qfunctions, q->function_count,
                                  q->strings, q->string_length, q->initial_globals, q->initial_globals_length,
                                  q->entity_field_words, q->checksum, q->digest};
  return true;
}
static qa_q1qc_machine *q_machine(P *p) {
  qa_compat_qc_entity_memory *e = &p->entities;
  p->qentities = (qa_q1qc_entity_memory){e->layout.stride_bytes, e->layout.variables_offset_bytes,
                                         e->layout.field_words, e->capacity, e->count, e->bytes};
  p->qmachine.ctx = p;
  p->qmachine.program = &p->qprogram;
  p->qmachine.globals = (qa_q1qc_words){p->machine.globals.bytes, p->machine.globals.byte_length};
  p->qmachine.entities = &p->qentities;
  p->qmachine.numeric = *p->machine.numeric;
  p->qmachine.fail = qm_fail;
  p->qmachine.execute = qm_execute;
  p->qmachine.execute_region = qm_execute_region;
  p->qmachine.strings_get = qm_strings_get;
  p->qmachine.strings_set_engine = qm_strings_set_engine;
  return &p->qmachine;
}
/* Prepare chain across the machine and q1qc boundary types. */
typedef struct {
  P *p;
  qa_q1qc_prepare_fn first; void *first_ctx;
  qa_q1qc_prepare_fn second; void *second_ctx;
} PrepareCell;
static void prepare_adapter(void *ctx, qa_compat_qc_machine *machine) {
  (void)machine;
  PrepareCell *cell = ctx;
  qa_q1qc_machine *view = q_machine(cell->p);
  if (cell->first) cell->first(cell->first_ctx, view);
  if (cell->second) cell->second(cell->second_ctx, view);
}
typedef struct {
  P *p;
  qa_compat_qc_function_execution execute;
  qa_q1qc_call_site call;
  qa_q1qc_prepare_fn outer; void *outer_ctx;
} ExecBridge;
static void words_widen(const int32_t in[3], int64_t out[3]) {
  for (int i = 0; i < 3; i++) out[i] = in[i];
}
static void bridge_skip(void *ctx, const int32_t words[3]) {
  ExecBridge *b = ctx;
  int64_t wide[3];
  words_widen(words, wide);
  b->execute.skip(b->execute.ctx, wide);
}
static void bridge_cancel(void *ctx, const int32_t words[3]) {
  ExecBridge *b = ctx;
  int64_t wide[3];
  words_widen(words, wide);
  b->execute.cancel(b->execute.ctx, wide);
}
static void bridge_run(void *ctx, qa_q1qc_prepare_fn prepare, void *prepare_ctx) {
  ExecBridge *b = ctx;
  PrepareCell cell = {b->p, prepare, prepare_ctx, NULL, NULL};
  qa_compat_qc_builtin builtin = {&cell, prepare_adapter};
  b->execute.run(b->execute.ctx, prepare ? &builtin : NULL);
}
static void selected_run(void *ctx, qa_q1qc_prepare_fn prepare, void *prepare_ctx) {
  ExecBridge *b = ctx;
  PrepareCell cell = {b->p, b->outer, b->outer_ctx, prepare, prepare_ctx};
  qa_compat_qc_builtin builtin = {&cell, prepare_adapter};
  b->execute.run(b->execute.ctx, b->outer || prepare ? &builtin : NULL);
}
/* TS `wrapped`: running it enters the weapon boundary with `selected`. */
static void wrapped_run(void *ctx, qa_q1qc_prepare_fn prepare, void *prepare_ctx) {
  ExecBridge *b = ctx;
  b->outer = prepare;
  b->outer_ctx = prepare_ctx;
  qa_q1qc_function_execution selected = {b, selected_run, bridge_skip, bridge_cancel};
  b->p->weapon_functions.boundary.run(b->p->weapon_functions.boundary.ctx, b->call, selected);
}
static void function_boundary_run(void *ctx, qa_compat_qc_call_site call, qa_compat_qc_function_execution execute) {
  P *p = ctx;
  ExecBridge bridge = {p, execute, {call.function_index, call.caller, call.statement}, NULL, NULL};
  bool weapon = false;
  if (p->has_weapon_compose)
    for (size_t i = 0; i < p->weapon_functions.function_count; i++)
      if (p->weapon_functions.functions[i] == call.function_index) weapon = true;
  const qa_q1qc_function_boundary *damage =
      p->has_combat ? qa_q1_id1_damage_function_boundary(p->combat.damage) : NULL;
  if (!weapon) {
    if (!damage) {
      execute.run(execute.ctx, NULL);
      return;
    }
    damage->run(damage->ctx, bridge.call, (qa_q1qc_function_execution){&bridge, bridge_run, bridge_skip, bridge_cancel});
    return;
  }
  qa_q1qc_function_execution wrapped = {&bridge, wrapped_run, bridge_skip, bridge_cancel};
  if (damage) damage->run(damage->ctx, bridge.call, wrapped);
  else wrapped_run(&bridge, NULL, NULL);
}
static void weapon_inner_run(void *ctx, qa_q1qc_call_site call, qa_q1qc_function_execution execute) {
  (void)ctx; (void)call;
  execute.run(execute.ctx, NULL, NULL);
}
static void armor_regions_run(void *ctx, qa_q1qc_inline_region region, qa_q1qc_inline_continuation execute) {
  P *p = ctx;
  if (!p->has_combat) {
    execute.run(execute.ctx);
    return;
  }
  const qa_q1qc_inline_boundary *boundary = qa_q1_id1_damage_inline_boundary(p->combat.damage);
  boundary->run(boundary->ctx, region, execute);
}
static void inline_boundary_run(void *ctx, qa_compat_qc_inline_region r, qa_compat_qc_inline_continuation execute) {
  P *p = ctx;
  qa_q1qc_inline_region region = {r.function_index, r.entry, r.exit, r.replaceable, r.has_standalone,
                                  r.standalone_saved, r.standalone_scope};
  qa_q1qc_inline_continuation continuation = {execute.ctx, execute.run, execute.skip_to_join};
  const qa_q1qc_inline_boundary *active = p->has_weapon_compose ? &p->weapon_regions.boundary : &p->armor_regions;
  active->run(active->ctx, region, continuation);
}
/* TS `[...new Map([...primary, ...regions].map(s => [s.entry, s.region])).values()]`. */
static void add_armor_region(P *p, qa_q1qc_inline_region region, size_t capacity) {
  for (size_t i = 0; i < p->armor_region_count; i++)
    if (p->armor_region_list[i].entry == region.entry) {
      p->armor_region_list[i] = region;
      return;
    }
  if (p->armor_region_count == capacity) qa_range_error("mod provider region list exceeded");
  p->armor_region_list[p->armor_region_count++] = region;
}
static bool build_boundaries(P *p, qa_compat_qc_machine_options *mo) {
  qa_mod_callback_declaration declaration = p->declaration;
  size_t protection_count = 0;
  qa_q1qc_armor_stage **stages = qa_compat_qc_protection_regions(p->program, declaration, &protection_count);
  p->armor_region_list = xrealloc(NULL, (protection_count + 1) * sizeof *p->armor_region_list);
  if (declaration.combat) {
    qa_q1qc_armor_stage *primary = qa_q1qc_armor_stage_create(&p->qprogram, declaration.combat->armor_stage);
    if (primary) {
      add_armor_region(p, primary->region, protection_count + 1);
      qa_q1qc_armor_stage_destroy(primary);
    }
  }
  for (size_t i = 0; i < protection_count; i++)
    add_armor_region(p, stages[i]->region, protection_count + 1);
  qa_compat_qc_protection_regions_free(stages, protection_count);
  p->armor_regions = (qa_q1qc_inline_boundary){p, p->armor_region_list, p->armor_region_count, armor_regions_run};
  const qa_q1qc_inline_boundary *regions = &p->armor_regions;
  size_t function_count = 0;
  if (p->has_items && p->items.has_weapons) {
    p->weapon_inner = (qa_q1qc_function_boundary){p, NULL, 0, weapon_inner_run};
    qa_q1qc_weapon_compose_functions(&p->weapon_functions, &p->items.stage_binding, p->weapon_inner);
    qa_q1qc_weapon_compose_regions(&p->weapon_regions, &p->items.stage_binding, p->armor_regions);
    p->has_weapon_compose = true;
    regions = &p->weapon_regions.boundary;
  }
  if (declaration.combat) {
    p->boundary_functions = xrealloc(NULL, (p->program->function_count + 1) * sizeof *p->boundary_functions);
    for (size_t i = 0; i < p->program->function_count; i++) {
      const qa_compat_qc_function *fn = &p->program->functions[i];
      if (fn->index > 0 && fn->first_statement > 0 && !fn->named_builtin)
        p->boundary_functions[function_count++] = fn->index;
    }
  } else if (p->has_weapon_compose) {
    p->boundary_functions = xrealloc(NULL, (p->weapon_functions.function_count + 1) * sizeof *p->boundary_functions);
    for (size_t i = 0; i < p->weapon_functions.function_count; i++)
      p->boundary_functions[function_count++] = p->weapon_functions.functions[i];
  }
  if (function_count > 0) {
    p->function_boundary = (qa_compat_qc_function_boundary){p, p->boundary_functions, function_count, function_boundary_run};
    mo->function_boundary = &p->function_boundary;
  }
  if (regions->region_count > 0) {
    p->inline_regions = xrealloc(NULL, regions->region_count * sizeof *p->inline_regions);
    for (size_t i = 0; i < regions->region_count; i++) {
      qa_q1qc_inline_region r = regions->regions[i];
      p->inline_regions[i] = (qa_compat_qc_inline_region){r.function_index, r.entry, r.exit, r.replaceable,
                                                          r.has_standalone, r.standalone_saved, r.standalone_scope};
    }
    p->inline_boundary = (qa_compat_qc_inline_boundary){p, p->inline_regions, regions->region_count, inline_boundary_run};
    mo->inline_boundary = &p->inline_boundary;
  }
  return true;
}

/* ---------------- messages and presentation ---------------- */
static const qa_actor_id *message_players(void *ctx, size_t *out_count) {
  P *p = ctx;
  qa_mod_client_services *clients = p->services->clients;
  if (clients) {
    size_t count = 0;
    const qa_mod_client_identity *identities = clients->clients(clients->ctx, &count);
    if (count > p->players_capacity) {
      p->players_capacity = count;
      p->players_scratch = xrealloc(p->players_scratch, count * sizeof *p->players_scratch);
    }
    for (size_t i = 0; i < count; i++) p->players_scratch[i] = identities[i].actor;
    *out_count = count;
    return p->players_scratch;
  }
  const qa_session_mod_engine_services *engine = p->services->engine;
  if (engine && engine->presentation_players)
    return engine->presentation_players(engine->ctx, out_count);
  *out_count = 0;
  return NULL;
}
static bool message_loading(void *ctx) { return ((P *)ctx)->loading; }
static bool message_phs(void *ctx) {
  return qa_cvar_registry_variable_value(((P *)ctx)->environment.cvars, "sv_phs") != 0;
}
static bool message_client_is_player(void *ctx, qa_actor_id actor) {
  size_t count = 0;
  const qa_actor_id *players = message_players(ctx, &count);
  for (size_t i = 0; i < count; i++)
    if (actor_eq(players[i], actor)) return true;
  return false;
}
static qa_owned_actor presentation_actor(void *ctx, int64_t slot) {
  return owned_of_slot(ctx, slot, "Presentation references a released mod actor");
}
static bool presentation_slot_actor(void *ctx, int64_t slot, qa_actor_id *out) {
  P *p = ctx;
  qa_actor_id reference;
  bool has = slot == 0 ? world_actor(p, &reference) : projection_actor_at(p, slot, &reference);
  if (!has) return false;
  qa_owned_actor owned;
  if (!qa_session_actor_registry_resolve_owned(p->services->actors, reference, &owned)) return false;
  *out = owned.id;
  return true;
}
static qa_actor_id message_world_actor(void *ctx, int64_t slot) {
  return presentation_actor(ctx, slot).id;
}
static const char *message_resource_name(void *ctx, const char *kind, double index) {
  P *p = ctx;
  size_t kind_length = strlen(kind);
  for (size_t i = 0; i < p->precached_count; i++)
    if (strncmp(p->precached[i].key, kind, kind_length) == 0 &&
        p->precached[i].key[kind_length] == ':' && p->precached[i].value.index == index)
      return p->precached[i].key + kind_length + 1;
  qa_range_error("Unknown mod %s index %s", kind, QA_JS_NUMBER(index));
}
static bool message_body_origin(void *ctx, qa_actor_id actor, qa_vec3 *out) {
  qa_body_state_result body = qa_shared_body_table_read(((P *)ctx)->services->bodies, actor);
  if (!body.has_value) return false;
  *out = body.value.origin;
  return true;
}
static void read_camera(P *p, qa_actor_id actor, qa_vec3 *origin, qa_vec3 *angles) {
  const qa_session_mod_engine_services *engine = p->services->engine;
  const qa_session_mod_engine_camera *camera =
      engine ? engine->presentation_camera : NULL;
  if (!camera) qa_range_error("Source mod messages require destination presentation services");
  camera->read(camera->ctx, actor, origin, angles);
}
static bool message_camera_angles(void *ctx, qa_actor_id actor, qa_vec3 *out) {
  qa_vec3 origin;
  read_camera(ctx, actor, &origin, out);
  return true;
}
static void message_emit(void *ctx, qa_q1_event event, const qa_actor_id *recipient) {
  P *p = ctx;
  qa_app_sim_presentation_event wrapped;
  memset(&wrapped, 0, sizeof wrapped);
  wrapped.kind = "q1";
  wrapped.q1 = &event;
  engine_emit(p, p->media->content, wrapped, recipient);
}
static bool message_clients_for_actor(void *ctx, qa_actor_id actor) {
  qa_client_id client;
  return ops_client_of(ctx, actor, &client);
}
static qa_mod_client_unsubscribe message_subscribe(void *ctx, void (*on_event)(void *, qa_mod_client_event), void *event_ctx) {
  qa_mod_client_services *clients = ((P *)ctx)->services->clients;
  return clients->subscribe(clients->ctx, on_event, event_ctx);
}
static bool message_visibility(void *ctx, qa_q1_client_visibility_scene *out) {
  const qa_session_mod_engine_services *engine = ((P *)ctx)->services->engine;
  if (!engine || !engine->clients_visibility) return false;
  *out = *(const qa_q1_client_visibility_scene *)engine->clients_visibility;
  return true;
}
/* TS host(target): one live target per presentation (messages do not
 * reenter host construction). */
static const qa_actor_id *host_target(P *p) {
  return p->message_host.has_target ? &p->message_host.target : NULL;
}
static void host_emit(void *ctx, qa_q1_event event, const qa_actor_id *recipient) {
  P *p = ctx;
  message_emit(p, event, recipient ? recipient : host_target(p));
}
static void host_message(void *ctx, qa_network_event event, const qa_actor_id *target) {
  P *p = ctx;
  const qa_session_mod_engine_services *engine = p->services->engine;
  if (!engine || !engine->message)
    qa_range_error("Source mod message requires a destination message sink");
  engine->message(engine->ctx, event, target);
}
static bool host_muzzle(void *ctx, qa_actor_id actor, qa_vec3 *out_origin, qa_vec3 *out_angles) {
  P *p = ctx;
  if (!message_client_is_player(p, actor)) return false;
  qa_body_state_result body = qa_shared_body_table_read(p->services->bodies, actor);
  if (!body.has_value) return false;
  *out_origin = body.value.origin;
  qa_vec3 ignored;
  read_camera(p, actor, &ignored, out_angles);
  return true;
}
static qa_actor_id host_actor_of(void *ctx, int64_t slot) { return message_world_actor(ctx, slot); }
/* Local message host adapter (TS host() fields). */
static qa_actor_id app_actor(void *ctx, double slot) {
  P *p = ctx;
  if (p->message_host.has_fixed) return p->message_host.fixed;
  return message_world_actor(p, (int64_t)slot);
}
static void app_camera(void *ctx, qa_actor_id actor, qa_vec3 *origin, qa_vec3 *angles) {
  read_camera(ctx, actor, origin, angles);
}
static const char *app_sound(void *ctx, double index) { return message_resource_name(ctx, "sound", index); }
static const char *app_model(void *ctx, double index) { return message_resource_name(ctx, "model", index); }
static void app_emit(void *ctx, const qa_q1_event *event, const qa_actor_id *recipient) {
  message_emit(ctx, *event, recipient ? recipient : host_target(ctx));
}
static void app_message(void *ctx, qa_network_event event, const qa_actor_id *actor) {
  host_message(ctx, event, actor);
}
static void app_emit_source(P *p, qa_app_sim_presentation_event event, const qa_actor_id *recipient) {
  engine_emit(p, p->media->content, event, recipient);
}
static qa_app_sim_presentation_event app_event(const char *kind) {
  qa_app_sim_presentation_event event;
  memset(&event, 0, sizeof event);
  event.kind = kind;
  return event;
}
static void app_music(void *ctx, double track) {
  qa_app_sim_presentation_event event = app_event("music");
  event.music.kind = "cd-track";
  event.music.track = track;
  app_emit_source(ctx, event, host_target(ctx));
}
static void app_angles(void *ctx, qa_actor_id actor, qa_vec3 angles) {
  qa_app_sim_presentation_event event = app_event("view-reset");
  event.view_reset.reason = "source";
  event.view_reset.actor = actor;
  event.view_reset.angles = angles;
  app_emit_source(ctx, event, host_target(ctx));
}
static void app_pause(void *ctx, bool paused) {
  qa_app_sim_presentation_event event = app_event("music");
  event.music.kind = "pause";
  event.music.paused = paused;
  app_emit_source(ctx, event, host_target(ctx));
}
static void app_sky(void *ctx, const char *name, const qa_actor_id *recipient) {
  qa_app_sim_presentation_event event = app_event("q1-sky");
  event.q1_sky.kind = "skybox";
  event.q1_sky.name = name;
  app_emit_source(ctx, event, recipient);
}
static void app_client_metadata(void *ctx, qa_app_sim_q1_client_metadata_event metadata, const qa_actor_id *recipient) {
  qa_app_sim_presentation_event event = app_event("q1-client");
  event.q1_client = metadata;
  app_emit_source(ctx, event, recipient);
}
static void app_session(void *ctx, const char *kind, const qa_actor_id *recipient) {
  qa_app_sim_presentation_event event = app_event("q1-session");
  event.q1_session.kind = kind;
  app_emit_source(ctx, event, recipient);
}
/* TS prompt: Extract<Q1CompositionEvent, prompt|clear-prompt>, to the
 * prompt's actor. */
static void app_prompt(void *ctx, qa_app_sim_quakec_prompt value) {
  qa_comp_q1_event composition;
  memset(&composition, 0, sizeof composition);
  composition.actor = value.actor;
  qa_comp_q1_prompt_choice *choices = NULL;
  if (text_eq(value.kind, "prompt")) {
    composition.kind = QA_COMP_Q1_EVENT_PROMPT;
    composition.title = value.title;
    choices = value.choice_count ? calloc(value.choice_count, sizeof *choices) : NULL;
    if (value.choice_count && !choices) qa_range_error("Out of memory");
    for (size_t i = 0; i < value.choice_count; i++) {
      choices[i].label = value.labels[i];
      choices[i].impulse = value.impulses[i];
    }
    composition.choices = choices;
    composition.choice_count = value.choice_count;
  } else {
    composition.kind = QA_COMP_Q1_EVENT_CLEAR_PROMPT;
  }
  qa_app_sim_presentation_event event = app_event("q1-composition");
  event.q1_composition = &composition;
  app_emit_source(ctx, event, &value.actor);
  free(choices);
  qa_app_sim_quakec_prompt_free(&value);
}
/* TS fog: q1-composition addon fog {player: recipient, density, color,
 * duration: max(0, transitionSeconds), skyFactor: 0.5}. */
static void app_fog(void *ctx, qa_netquake_message value, const qa_actor_id *recipient) {
  qa_q1ad_event fog;
  memset(&fog, 0, sizeof fog);
  fog.kind = QA_Q1AD_EVENT_FOG;
  fog.has_player = recipient != NULL;
  if (recipient) fog.player = *recipient;
  fog.density = value.density;
  fog.color_vec = value.color;
  fog.duration = qa_js_max(0, value.transition_seconds);
  fog.sky_factor = 0.5;
  qa_comp_q1_event composition;
  memset(&composition, 0, sizeof composition);
  composition.kind = QA_COMP_Q1_EVENT_ADDON;
  composition.addon_event = &fog;
  qa_app_sim_presentation_event event = app_event("q1-composition");
  event.q1_composition = &composition;
  app_emit_source(ctx, event, recipient);
}
static void host_present_local(void *ctx, qa_netquake_message message, const qa_actor_id *target,
                               qa_compat_qc_local_messages state, const qa_actor_id *fixed_actor) {
  (void)state;
  P *p = ctx;
  size_t count = 0;
  const qa_actor_id *players = message_players(p, &count);
  const qa_session_mod_engine_services *engine = p->services->engine;
  if (!engine || !engine->presentation_map)
    qa_range_error("Source mod messages require destination presentation services");
  p->message_host.has_fixed = fixed_actor != NULL;
  if (fixed_actor) p->message_host.fixed = *fixed_actor;
  qa_app_sim_quakec_local_message_host host;
  memset(&host, 0, sizeof host);
  host.ctx = p;
  host.actor = app_actor;
  host.recipients = players;
  host.recipient_count = count;
  host.source_actor = message_world_actor(p, 0);
  host.map = engine->presentation_map;
  host.seconds = items_seconds(p);
  host.camera = app_camera;
  host.sound = app_sound;
  host.model = app_model;
  host.emit = app_emit;
  host.message = app_message;
  host.music = app_music;
  host.angles = app_angles;
  host.pause = app_pause;
  host.sky = app_sky;
  host.client_metadata = app_client_metadata;
  host.session = app_session;
  host.prompt = app_prompt;
  host.fog = app_fog;
  qa_app_sim_present_quakec_local_message(message, target, p->local_messages, host);
  p->message_host.has_fixed = false;
}
static qa_compat_qc_local_message_host message_host_of(void *ctx, const qa_actor_id *target, double seconds,
                                                       const qa_actor_id *recipients, size_t recipient_count,
                                                       qa_actor_id source_actor) {
  (void)source_actor;
  P *p = ctx;
  const qa_session_mod_engine_services *engine = p->services->engine;
  if (!engine || !engine->presentation_map)
    qa_range_error("Source mod messages require destination presentation services");
  p->message_host.has_target = target != NULL;
  if (target) p->message_host.target = *target;
  qa_compat_qc_local_message_host host;
  memset(&host, 0, sizeof host);
  host.ctx = p;
  host.recipients = recipients;
  host.recipient_count = recipient_count;
  host.map = engine->presentation_map;
  host.seconds = seconds;
  host.message = host_message;
  host.emit = host_emit;
  host.present_local = host_present_local;
  host.muzzle = host_muzzle;
  host.actor_of = host_actor_of;
  return host;
}
static qa_compat_qc_precached_resource presentation_precache(void *ctx, const char *kind, const char *name) {
  qa_compat_qc_precached_resource resource;
  if (!qa_compat_qc_mod_provider_lookup(ctx, kind, name, &resource))
    qa_range_error("Mod resource was not prepared: %s/%s", kind, name);
  return resource;
}
static bool presentation_lookup(void *ctx, const char *kind, const char *name, qa_compat_qc_precached_resource *out) {
  return qa_compat_qc_mod_provider_lookup(ctx, kind, name, out);
}
static void presentation_print(void *ctx, const char *text) {
  const qa_session_mod_engine_services *engine = ((P *)ctx)->services->engine;
  if (engine && engine->print) engine->print(engine->ctx, text);
}
static void presentation_broadcast_print(void *ctx, const char *text, double level) {
  P *p = ctx;
  qa_compat_qc_routed_message entry;
  memset(&entry, 0, sizeof entry);
  entry.message.kind = "print";
  entry.message.text = (char *)text;
  entry.message.level = level;
  qa_compat_qc_message_destination destination;
  memset(&destination, 0, sizeof destination);
  destination.kind = QA_QC_DEST_BROADCAST;
  destination.reliable = true;
  p->messages.qw_routes.route(p->messages.qw_routes.ctx, &entry, 1, destination);
}
static void presentation_message(void *ctx, qa_network_event event, qa_actor_id actor) {
  const qa_session_mod_engine_services *engine = ((P *)ctx)->services->engine;
  engine->message(engine->ctx, event, &actor);
}
static void presentation_emit(void *ctx, qa_content_id content, qa_q1_event event) {
  qa_app_sim_presentation_event wrapped;
  memset(&wrapped, 0, sizeof wrapped);
  wrapped.kind = "q1";
  wrapped.q1 = &event;
  engine_emit(ctx, content, wrapped, NULL);
}
static void presentation_register_resource(void *ctx, qa_content_id content, const char *path, qa_resolved_resource_reference resource) {
  engine_register_resource(ctx, content, path, resource);
}

/* ---------------- quakeworld builtins ---------------- */
static void builtin_infokey(void *ctx, qa_compat_qc_machine *vm) {
  P *p = ctx;
  int32_t reference = qa_compat_qc_machine_arg_int(vm, 0);
  const char *key = qa_compat_qc_machine_arg_string(vm, 1);
  char *owned = NULL;
  const char *value = "";
  if (reference == 0) {
    owned = qa_cvar_registry_variable_string(p->environment.cvars, key);
    if (!owned) qa_range_error("mod provider allocation failed");
    value = owned;
    if (value[0] == '\0') {
      const char *local = qa_cvar_registry_local_info_get(p->environment.cvars, key);
      value = local != NULL ? local : "";
    }
  } else {
    qa_actor_id actor;
    int64_t slot;
    if (projection_actor_at(p, qa_compat_qc_entity_memory_slot(vm->entities, reference), &actor) &&
        client_slot_of(p, actor, &slot)) {
      owned = qa_compat_qc_mod_client_bindings_userinfo(&p->clients, actor, key);
      if (!owned) qa_range_error("mod provider allocation failed");
      value = owned;
    }
  }
  char name[256];
  snprintf(name, sizeof name, "mod-infokey:%d:%s", reference, key);
  int64_t capacity = (int64_t)strlen(value) + 1;
  if (capacity < 1024) capacity = 1024;
  qa_compat_qc_machine_return_int(vm, (double)qa_compat_qc_strings_set_engine(&vm->strings, name, value, capacity));
  free(owned);
}
typedef struct { P *p; const char *name; } ClientMessageWrap;
static void builtin_client_message(void *ctx, qa_compat_qc_machine *vm) {
  ClientMessageWrap *wrap = ctx;
  P *p = wrap->p;
  qa_actor_id actor = qa_compat_qc_mod_provider_actor(p, qa_compat_qc_machine_arg_int(vm, 0));
  const qa_session_mod_engine_services *engine = p->services->engine;
  int64_t slot;
  if (!client_slot_of(p, actor, &slot)) {
    if (engine && engine->print) {
      char text[64];
      snprintf(text, sizeof text, "tried to %s to a non-client\n", wrap->name);
      engine->print(engine->ctx, text);
    }
    return;
  }
  if (!engine || !engine->message)
    qa_compat_qc_machine_fail(vm, "Mod client message requires a destination message sink");
  bool sprint = strcmp(wrap->name, "sprint") == 0;
  char *text = qa_compat_qc_machine_var_string(vm, sprint ? 2 : 1);
  if (!text) qa_range_error("mod provider allocation failed");
  qa_network_event event;
  memset(&event, 0, sizeof event);
  event.text = text;
  if (sprint) {
    event.kind = QA_NETWORK_EVENT_PRINT;
    event.level = trunc(qa_compat_qc_machine_arg_float(vm, 1));
  } else {
    event.kind = strcmp(wrap->name, "centerprint") == 0 ? QA_NETWORK_EVENT_CENTER_PRINT : QA_NETWORK_EVENT_COMMAND_TEXT;
  }
  engine->message(engine->ctx, event, &actor);
  free(text);
}

/* ---------------- clientPresentation and presentations ---------------- */
typedef struct { P *p; int64_t slot; } ViewCell;
static bool view_read(void *ctx, qa_actor_id target, qa_vec3 *origin, qa_vec3 *angles) {
  P *p = ((ViewCell *)ctx)->p;
  int64_t slot = projection_slot(p, target);
  if (slot < 0 || !qa_session_actor_registry_is_live(p->services->actors, target)) return false;
  qa_compat_qc_words *value = qa_compat_qc_entity_memory_at(&p->entities, slot);
  *origin = qa_compat_qc_words_vector(value, qa_compat_qc_machine_field_offset(&p->machine, "origin"));
  *angles = qa_compat_qc_words_vector(value, qa_compat_qc_machine_field_offset(&p->machine, "angles"));
  return true;
}
static qa_vec3 view_offset(void *ctx, qa_actor_id actor) {
  (void)actor;
  ViewCell *cell = ctx;
  const qa_compat_qc_words * w = qa_compat_qc_entity_memory_at(&cell->p->entities, cell->slot);
  int64_t word = qa_compat_qc_machine_field_offset(&cell->p->machine, "view_ofs");
  return qa_compat_qc_words_vector(w, word);
}
static bool client_frame(void *ctx, qa_actor_id actor, qa_mod_client_presentation_frame *out) {
  P *p = ctx;
  if (p->closed || p->depth != 0)
    qa_range_error("QC client presentation requires an idle live source");
  const qa_qc_mod_client_presentation *declaration = p->declaration.client_presentation;
  int64_t slot = projection_slot(p, actor);
  if (!declaration || slot < 0 || !qa_session_actor_registry_is_live(p->services->actors, actor) ||
      !p->has_clients || !qa_compat_qc_mod_client_bindings_admitted(&p->clients, actor))
    return false;
  if (!p->has_messages) qa_range_error("QC client presentation has no source messages");
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_at(&p->entities, slot);
  memset(out, 0, sizeof *out);
  out->kind = QA_MOD_CLIENT_PRESENTATION_QUAKEC;
  if (!text_eq(declaration->hud, "none")) {
    out->has_quakec_hud = true;
    out->quakec_health = qa_compat_qc_words_float(words, qa_compat_qc_machine_field_offset(&p->machine, "health"));
    out->quakec_armor = qa_compat_qc_words_float(words, qa_compat_qc_machine_field_offset(&p->machine, "armorvalue"));
  }
  if (!text_eq(declaration->view, "none")) {
    ViewCell cell = {p, slot};
    qa_app_sim_quakec_local_view_source source = {&cell, view_read, view_offset};
    out->has_quakec_view = qa_app_sim_quakec_local_view(actor, p->local_messages, source, &out->quakec_view);
  }
  return true;
}
static void client_assert_current(void *ctx) {
  if (((P *)ctx)->closed) qa_range_error("QC client presentation is retired");
}
qa_mod_client_presentation_source qa_compat_qc_mod_provider_client_presentation(P *p) {
  p->client_source.ctx = p;
  p->client_source.generation = (double)p->presentation_generation;
  p->client_source.frame = client_frame;
  p->client_source.assert_current = client_assert_current;
  p->has_client_source = true;
  return p->client_source;
}
static double optional_scalar(P *p, qa_compat_qc_words *words, const char *name) {
  const qa_compat_qc_definition *field = qa_compat_qc_program_field_named(p->program, name);
  return field ? qa_compat_qc_words_float(words, field->offset) : 0;
}
const qa_app_simulation_presentation *qa_compat_qc_mod_provider_presentations(P *p, size_t *out_count) {
  size_t owned_count = 0;
  const qa_owned_actor *owned = qa_session_actor_registry_owned_by(p->services->actors, p->module.id, &owned_count);
  if (owned_count > p->presentation_capacity) {
    p->presentation_capacity = owned_count;
    p->presentation_payloads = xrealloc(p->presentation_payloads, owned_count * sizeof *p->presentation_payloads);
  }
  size_t count = 0;
  for (size_t i = 0; i < owned_count; i++) {
    qa_actor_source_result source = qa_session_actor_registry_source_of(p->services->actors, owned[i].id);
    if (!source.has_value) continue;
    qa_compat_qc_words *words = qa_compat_qc_entity_memory_at(&p->entities, source.slot);
    const char *path = qa_compat_qc_strings_get(
        &p->machine.strings,
        qa_compat_qc_words_int(words, qa_compat_qc_machine_field_offset(&p->machine, "model")));
    if (path[0] == '\0') continue;
    const qa_compat_qc_mod_media_resource *asset = media_find(p, path);
    if (!asset || !p->media) qa_range_error("Mod model was not prepared: %s", path);
    engine_register_resource(p, p->media->content, asset->resource.requested_path, asset->resource);
    double frame = optional_scalar(p, words, "frame");
    double alpha = optional_scalar(p, words, "alpha");
    double scale = optional_scalar(p, words, "scale");
    qa_app_simulation_presentation *payload = &p->presentation_payloads[count];
    memset(payload, 0, sizeof *payload);
    payload->actor = owned[i].id;
    payload->content = p->media->content;
    payload->family = QA_GAME_FAMILY_Q1;
    payload->path = path;
    payload->frame = frame;
    payload->old_frame = frame;
    payload->skin = optional_scalar(p, words, "skin");
    payload->effects = optional_scalar(p, words, "effects");
    payload->render_flags = 0;
    payload->origin = qa_compat_qc_words_vector(words, qa_compat_qc_machine_field_offset(&p->machine, "origin"));
    payload->angles = qa_compat_qc_words_vector(words, qa_compat_qc_machine_field_offset(&p->machine, "angles"));
    payload->scale = scale == 0 ? 1 : scale;
    payload->has_alpha = true;
    payload->alpha = alpha == 0 ? 1 : qa_js_max(0, qa_js_min(1, alpha));
    payload->visible = true;
    payload->view_weapon = false;
    count++;
  }
  *out_count = count;
  return p->presentation_payloads;
}

static double protection_invoke(void *ctx, qa_mod_source_call call, const qa_compat_qc_mod_input_entry *inputs,
                                size_t input_count, const qa_q1qc_armor_stage *region) {
  return qa_compat_qc_mod_provider_invoke(ctx, call, inputs, input_count, region);
}
static qa_q1_visibility_client engine_client_at(void *ctx, int64_t slot) {
  const qa_session_mod_engine_services *engine = ((P *)ctx)->services->engine;
  return *(const qa_q1_visibility_client *)engine->clients_at(engine->ctx, (double)slot);
}
/* ---------------- constructor ---------------- */
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
  p->message_host.p = p;
  p->host_state = (qa_compat_qc_executor_host){p, host_checkpoint, host_restore};
  qa_compat_qc_validate_mod(program, declaration);
  p->objectives = qa_mod_source_objectives_create(
      module.id, declaration.objectives, declaration.objective_count, services->match,
      (qa_mod_objective_storage){p, objectives_current, objectives_location,
                                 objectives_read_scalar, objectives_write_scalar,
                                 objectives_read_actor, objectives_write_actor,
                                 objectives_invoke, objectives_seconds});
  if (!p->objectives) return false;
  if (declaration.actor_field_count > 0) {
    p->fields = xrealloc(NULL, declaration.actor_field_count * sizeof *p->fields);
    p->field_count = declaration.actor_field_count;
    for (size_t i = 0; i < p->field_count; i++) {
      const qa_compat_qc_definition *field =
          qa_compat_qc_program_field_named(program, declaration.actor_fields[i].field);
      if (!field) qa_range_error("Missing validated actor field %s", declaration.actor_fields[i].field);
      p->fields[i].declaration = declaration.actor_fields[i];
      p->fields[i].offset = field->offset;
      p->fields[i].words = text_eq(field->type, "vector") ? 3 : 1;
      if (text_eq(declaration.actor_fields[i].binding, "client-input")) p->input_field_count++;
    }
  }
  if (p->input_field_count > 0) {
    p->input_fields = xrealloc(NULL, p->input_field_count * sizeof *p->input_fields);
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
    qa_range_error("QuakeC component clients require destination client services");
  qa_compat_qc_entity_layout layout = qa_compat_qc_classic_entity_layout(program);
  int64_t maximum = declaration.clients ? (int64_t)declaration.clients->maximum : 0;
  if (!qa_compat_qc_entity_memory_init(&p->entities, layout, 8192, maximum + 1)) return false;
  p->body_states = calloc((size_t)p->entities.capacity, sizeof *p->body_states);
  if (!p->body_states || !build_qprogram(p)) return false;
  const qa_session_mod_engine_services *engine = services->engine;
  if (!env_clients && engine && engine->has_clients && engine->clients_visibility) {
    p->engine_clients.ctx = p;
    p->engine_clients.maximum = engine->clients_maximum;
    p->engine_clients.visibility = *(const qa_q1_client_visibility_scene *)engine->clients_visibility;
    p->engine_clients.at = engine_client_at;
    env_clients = &p->engine_clients;
  }
  if (!qa_compat_qc_mod_environment_init(&p->environment, services, program, declaration, p, reference_actor, env_clients))
    return false;
  /* TS host Map, in TS set order. */
  const qa_session_mod_engine_physics *physics = engine_physics(p);
  const qa_session_mod_engine_scene *scene = engine_scene(p);
  if (physics) {
    static const char *const names[4] = {"walkmove", "movetogoal", "changeyaw", "checkbottom"};
    for (int i = 0; i < 4; i++) {
      p->movement_wraps[i].p = p;
      p->movement_wraps[i].name = names[i];
      host_set(p, names[i], (qa_compat_qc_builtin){&p->movement_wraps[i], builtin_movement});
    }
  }
  host_set(p, "spawn", (qa_compat_qc_builtin){p, builtin_spawn});
  host_set(p, "remove", (qa_compat_qc_builtin){p, builtin_remove});
  if (scene) {
    p->spatial_world = (qa_compat_qc_spatial_world){p, program, &p->entities, scene->queries, qa_q1_donor_profile,
                                                    spatial_model, spatial_actor, reference_by_value,
                                                    builtin_is_free_entity, prepare_entities, spatial_link};
    qa_compat_qc_spatial_bindings spatial = qa_compat_qc_create_spatial_bindings(&p->spatial_world);
    for (size_t i = 0; i < spatial.count; i++) host_set(p, spatial.entries[i].name, spatial.entries[i].fn);
    host_set(p, "aim", (qa_compat_qc_builtin){p, builtin_aim});
  }
  if (engine && scene && media) {
    p->presentation_world = (qa_compat_qc_presentation_world){p, program, &p->entities, p->host_table, 0,
                                                              presentation_actor, presentation_slot_actor};
    p->local_messages = qa_app_sim_quakec_local_messages_create();
    if (!p->local_messages) return false;
    qa_compat_qc_local_message_app_adapter_init(&p->local_adapter, p->local_messages);
    qa_compat_qc_mod_messages_operations ops;
    memset(&ops, 0, sizeof ops);
    ops.ctx = p;
    ops.loading = message_loading;
    ops.phs = message_phs;
    /* TS publishesKills: declaration.objectives?.some(objective => objective.role === "borrowed" && objective.writable) !== true */
    for (size_t i = 0; i < declaration.objective_count; i++)
      if (!declaration.objectives[i].is_owned && declaration.objectives[i].writable) ops.withholds_kills = true;
    ops.client_is_player = message_client_is_player;
    ops.players = message_players;
    ops.world_actor = message_world_actor;
    ops.resource_name = message_resource_name;
    ops.body_origin = message_body_origin;
    ops.camera_angles = message_camera_angles;
    ops.host_ctx = p;
    ops.host = message_host_of;
    ops.emit = message_emit;
    ops.clients_for_actor = message_clients_for_actor;
    ops.has_clients = services->clients != NULL;
    ops.subscribe_clients = services->clients ? message_subscribe : NULL;
    ops.visibility = message_visibility;
    ops.time_seconds = items_seconds;
    if (!qa_compat_qc_mod_messages_init(&p->messages, &p->presentation_world, services->actors, media->content, ops, p->local_adapter.ops))
      return false;
    p->has_messages = true;
    for (size_t i = 0; i < p->messages.messages.host_count; i++)
      host_set(p, p->messages.messages.host_entries[i].name, p->messages.messages.host_entries[i].fn);
    qa_compat_qc_presentation_services presentation;
    memset(&presentation, 0, sizeof presentation);
    presentation.ctx = p;
    if (p->messages.is_quakeworld) presentation.qw = &p->messages.qw_routes;
    else presentation.nq = &p->messages.nq_routes;
    presentation.content = media->content;
    presentation.precache = presentation_precache;
    presentation.lookup = presentation_lookup;
    presentation.loading = message_loading;
    presentation.print = presentation_print;
    presentation.broadcast_print = p->messages.is_quakeworld ? presentation_broadcast_print : NULL;
    presentation.print_broadcasts_to_clients = true;
    presentation.message = engine->message ? presentation_message : NULL;
    presentation.emit = presentation_emit;
    presentation.register_resource = presentation_register_resource;
    qa_compat_qc_presentation_bindings bindings =
        qa_compat_qc_create_presentation_bindings(&p->presentation_world, presentation, &p->presentation_state);
    for (size_t i = 0; i < bindings.count; i++) host_set(p, bindings.entries[i].name, bindings.entries[i].fn);
  }
  for (size_t i = 0; i < p->environment.host_count; i++)
    host_set(p, p->environment.host_entries[i].name, p->environment.host_entries[i].fn);
  if (declaration.clients && program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD) {
    host_set(p, "infokey", (qa_compat_qc_builtin){p, builtin_infokey});
    if (!p->has_messages) {
      static const char *const names[3] = {"sprint", "centerprint", "stuffcmd"};
      for (int i = 0; i < 3; i++) {
        ClientMessageWrap *wrap = owned_cell(p, sizeof *wrap);
        wrap->p = p;
        wrap->name = names[i];
        host_set(p, names[i], (qa_compat_qc_builtin){wrap, builtin_client_message});
      }
    }
  }
  host_set(p, "localcmd", (qa_compat_qc_builtin){p, builtin_localcmd});
  for (size_t i = 0; i < extra_host_count; i++) host_set(p, extra_host[i].name, extra_host[i].fn);
  p->presentation_world.host_count = p->host_count;
  if (declaration.items && !media)
    qa_range_error("QC source items require their prepared content owner");
  if (declaration.items) {
    qa_compat_qc_mod_items_operations ops;
    memset(&ops, 0, sizeof ops);
    ops.ctx = p;
    ops.machine = items_machine;
    ops.reference = reference_by_value;
    ops.actor = combat_actor;
    ops.invoke = combat_invoke;
    ops.bind_items = items_bind;
    ops.client_is_current = items_client_current;
    ops.weapon_bind = services->weapons ? items_weapon_bind : NULL;
    ops.weapon_selected = items_weapon_selected;
    ops.inventory_count = items_count;
    ops.time_seconds = items_seconds;
    ops.resolve_owned = ops_resolve_owned;
    ops.media_content = media->content;
    ops.media_resource = items_media;
    if (!qa_compat_qc_mod_items_init(&p->items, declaration.items, module.id, ops, program)) return false;
    p->has_items = true;
  }
  p->numeric = qa_create_numeric_operations(&qa_q1_donor_profile);
  {
    qa_compat_qc_builtin_services builtin_services;
    memset(&builtin_services, 0, sizeof builtin_services);
    builtin_services.kind = qa_compat_qc_host_kind_for_program(program);
    builtin_services.random = random ? &random->source : NULL;
    builtin_services.ctx = p;
    builtin_services.is_free_entity = builtin_is_free_entity;
    builtin_services.prepare_entities = prepare_entities;
    builtin_services.host = p->host_table;
    builtin_services.host_count = p->host_count;
    if (!qa_compat_qc_create_builtins(builtin_services, &p->registry)) return false;
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
    if (!build_boundaries(p, &mo)) return false;
    if (!qa_compat_qc_machine_init(&p->machine, mo)) return false;
  }
  if (declaration.protection_count > 0 || declaration.protection) {
    qa_compat_qc_mod_protection_operations ops = {p, &p->machine, reference_by_value, combat_actor,
                                                  protection_invoke, protection_pickups};
    qa_compat_qc_mod_protection_init(&p->protection, declaration, module.id, services, ops);
    p->has_protection = true;
  }
  if (declaration.pickups) {
    qa_compat_qc_mod_pickups_operations ops = {p, combat_invoke, pickups_watch, pickups_bind,
                                               ops_resolve_owned, ops_client_of, ops_actor_of_client};
    qa_compat_qc_mod_pickups_init(&p->pickups, declaration, module.id, services, ops);
    p->has_pickups = true;
  }
  qa_compat_qc_mod_input_init(&p->input, &p->machine, p->input_fields, p->input_field_count, p,
                              input_reference_fn, input_live_fn);
  {
    bool has_claim = services->clients && services->clients->claim_outputs;
    const qa_mod_client_output_declaration *outputs = declaration.clients ? declaration.clients->outputs : NULL;
    size_t output_count = declaration.clients ? declaration.clients->output_count : 0;
    p->client_outputs = qa_source_mod_client_outputs_create(
        module.id, outputs, output_count, has_claim, has_claim ? outputs_claim : NULL, p,
        outputs_scalar, outputs_vector, p);
    if (!p->client_outputs) return false;
  }
  bool has_think = false;
  for (size_t i = 0; i < p->field_count; i++)
    if (text_eq(p->fields[i].declaration.binding, "think")) has_think = true;
  if (declaration.clients && services->clients) {
    qa_compat_qc_mod_client_input_ops *input_ops = owned_cell(p, sizeof *input_ops);
    input_ops->ctx = p;
    input_ops->open = input_open;
    input_ops->invoke = input_invoke;
    input_ops->output = input_output;
    qa_compat_qc_mod_client_operations operations;
    memset(&operations, 0, sizeof operations);
    operations.ctx = p;
    operations.services = services->clients;
    operations.declaration = declaration.clients;
    operations.project = clients_project;
    operations.release = clients_release;
    operations.invoke = clients_invoke;
    operations.think = has_think ? run_client_think : NULL;
    operations.reserve = clients_reserve;
    operations.admitted = clients_admitted;
    operations.input = input_ops;
    qa_compat_qc_mod_client_bindings_init(&p->clients, operations);
    p->has_clients = true;
  }
  qa_compat_qc_mod_environment_initialize_globals(&p->environment, &p->machine);
  qa_compat_qc_actor_state_init(
      &p->actor_state,
      (qa_compat_qc_actor_state_options){&p->machine,
                                         media && strstr(media->content, ":rerelease:") != NULL,
                                         p, state_source_slot, state_reference, state_is_client});
  {
    const qa_compat_qc_definition *think = qa_compat_qc_program_field_named(program, "think");
    const qa_compat_qc_definition *nextthink = qa_compat_qc_program_field_named(program, "nextthink");
    qa_compat_qc_mod_actor_options options;
    memset(&options, 0, sizeof options);
    options.machine = &p->machine;
    options.provider = module.id;
    options.services = services;
    options.think = think ? think->offset : -1;
    options.nextthink = nextthink ? nextthink->offset : -1;
    options.first_dynamic_slot = maximum + 1;
    options.ctx = p;
    options.client_frame =
        declaration.clients && (declaration.clients->frame_count != 0 || has_think) ? actors_client_frame : NULL;
    options.body = actors_body;
    options.admitted = actors_admitted;
    options.retired = actors_retired;
    options.step = actors_step;
    options.touch = actors_touch;
    options.use = actors_use;
    options.pain = actors_pain;
    options.die = actors_die;
    options.remember = actors_remember;
    options.invoke = actors_invoke;
    if (!qa_compat_qc_mod_actors_init(&p->owned_actors, options)) return false;
    p->has_owned_actors = true;
  }
  if (declaration.combat) {
    qa_compat_qc_mod_combat_options options;
    memset(&options, 0, sizeof options);
    options.program = program;
    options.module = module;
    options.declaration = (qa_mod_qc_combat *)declaration.combat;
    options.services = services;
    options.machine = &p->machine;
    options.slots = p->owned_actors.slots;
    options.slots_options = p->owned_actors.slots_options;
    options.ctx = p;
    options.actor = combat_actor;
    options.reference = reference_actor;
    options.invoke = combat_invoke;
    options.player_team = combat_player_team;
    options.player_set_team = combat_player_set_team;
    options.slot_free = combat_slot_free;
    if (!qa_compat_qc_mod_combat_init(&p->combat, options)) return false;
    p->has_combat = true;
  }
  if (scene) {
    qa_compat_qc_world_host_options options;
    memset(&options, 0, sizeof options);
    options.program = program;
    options.entities = &p->entities;
    options.actors = services->actors;
    options.slots = p->owned_actors.slots;
    options.slots_options = p->owned_actors.slots_options;
    options.bodies = services->bodies;
    options.scene = scene->queries;
    options.numeric = qa_q1_donor_profile;
    options.ctx = p;
    options.model = no_model;
    options.foreign_reference = reference_by_value;
    options.actor_at = movement_actor;
    if (!qa_compat_qc_world_host_init(&p->world_host, options)) return false;
    p->has_world_host = true;
    p->aim_state.ctx = p;
    p->aim_state.aim_threshold = aim_threshold;
    p->aim_state.teamplay = aim_teamplay;
    p->aim_state.targets = aim_targets;
    p->aim_state.no_aim = aim_no_aim;
    p->aim_builtin = qa_compat_qc_create_aim_binding(&p->world_host, &p->aim_state);
    if (physics) {
      qa_compat_qc_movement_services movement;
      memset(&movement, 0, sizeof movement);
      movement.scene = scene->queries;
      movement.random = random->source;
      movement.ctx = p;
      movement.touch_triggers = physics_touch_triggers;
      movement.actor_reference = movement_actor_reference;
      p->movement_bindings = qa_compat_qc_create_movement_bindings(&p->world_host, movement, &p->movement_state);
      p->has_movement = true;
      p->pusher_state.ctx = p;
      p->pusher_state.physical = pusher_physical;
      p->pusher_state.foreign_read = pusher_foreign_read;
      p->pusher_state.foreign_write = pusher_foreign_write;
      p->pusher_state.touch_triggers = physics_touch_triggers;
      p->pusher_state.server_time = pusher_server_time;
      p->pusher_state.invoke = pusher_invoke;
      p->pusher_services = qa_compat_qc_create_pusher_services(&p->world_host, &p->machine, &p->pusher_state);
      p->has_pusher = true;
    }
  }
  p->release_subscription = qa_session_actor_registry_on_release(services->actors, on_actor_release, p);
  p->has_release_subscription = true;
  return true;
}
void qa_compat_qc_mod_provider_destroy(P *p) {
  qa_compat_qc_mod_provider_close(p);
  if (p->has_world_host) qa_compat_qc_world_host_destroy(&p->world_host);
  if (p->has_combat) qa_compat_qc_mod_combat_destroy(&p->combat);
  if (p->has_owned_actors) qa_compat_qc_mod_actors_destroy(&p->owned_actors);
  if (p->has_pickups) qa_compat_qc_mod_pickups_destroy(&p->pickups);
  if (p->has_protection) qa_compat_qc_mod_protection_destroy(&p->protection);
  if (p->has_messages) qa_compat_qc_mod_messages_destroy(&p->messages);
  if (p->local_messages) {
    qa_compat_qc_local_message_app_adapter_destroy(&p->local_adapter);
    qa_app_sim_quakec_local_messages_destroy(p->local_messages);
  }
  if (p->has_weapon_compose) {
    qa_q1qc_weapon_function_composed_destroy(&p->weapon_functions);
    qa_q1qc_weapon_region_composed_destroy(&p->weapon_regions);
  }
  if (p->has_items) qa_compat_qc_mod_items_destroy(&p->items);
  if (p->program) {
    qa_compat_qc_machine_destroy(&p->machine);
    qa_compat_qc_registry_destroy(&p->registry);
    qa_compat_qc_mod_environment_destroy(&p->environment);
    qa_compat_qc_entity_memory_destroy(&p->entities);
  }
  qa_mod_source_objectives_destroy(p->objectives);
  qa_source_mod_client_outputs_destroy(p->client_outputs);
  if (p->has_clients) qa_compat_qc_mod_client_bindings_destroy(&p->clients);
  qa_compat_qc_mod_input_destroy(&p->input);
  if (p->has_checkpoint) qa_compat_qc_machine_snapshot_destroy(&p->snapshot);
  free(p->host_state_bytes);
  free(p->host_encoded);
  free(p->objective_location);
  free(p->fields);
  free(p->input_fields);
  free(p->projections);
  free(p->free_slots);
  free(p->retired);
  free(p->pickup_scopes);
  free(p->match_cells);
  for (size_t i = 0; i < p->precached_count; i++) free(p->precached[i].key);
  free(p->precached);
  for (size_t i = 0; i < p->owned_cell_count; i++) free(p->owned_cells[i]);
  free(p->owned_cells);
  free(p->qglobals);
  free(p->qfields);
  free(p->qfunctions);
  free(p->qstatements);
  free(p->body_states);
  free(p->boundary_functions);
  free(p->inline_regions);
  free(p->armor_region_list);
  free(p->players_scratch);
  free(p->aim_targets);
  free(p->source_physics);
  free(p->presentation_payloads);
  memset(p, 0, sizeof *p);
}
