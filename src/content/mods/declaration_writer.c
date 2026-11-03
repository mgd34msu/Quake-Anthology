/*
 * The TS value of a gameplay-mod declaration (see declaration_writer.h). Each
 * writer follows the reader that builds the object in TS: key order is the
 * order of the reader's object literal (spreads included), optional members are
 * written only when the reader kept them. GPL-2.0-or-later.
 */
#include "qa/content/mods/declaration_writer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/held_weapon.h"
#include "qa/contracts/mod_client_outputs.h"
#include "qa/contracts/native_mod_items.h"
#include "qa/contracts/native_mod_region.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/qc_weapon_stage.h"
#include "qa/contracts/qvm_combat.h"
#include "qa/contracts/qvm_mod_actor_frame.h"
#include "qa/contracts/qvm_mod_items.h"
#include "qa/contracts/qvm_mod_presentation.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/source_items.h"
#include "qa/contracts/source_match.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

/* --- tree construction (out of memory aborts: TS has no OOM path) --- */
static qa_save_value *checked(qa_save_value *value) {
  if (value == NULL) qa_range_error("Out of memory");
  return value;
}
static qa_save_value *obj(void) { return checked(qa_save_value_object()); }
static qa_save_value *arr(void) { return checked(qa_save_value_array()); }
static qa_save_value *num(double value) { return checked(qa_save_value_number(value)); }
static qa_save_value *str(const char *value) { return checked(qa_save_value_string(value)); }
static qa_save_value *null_value(void) { return checked(qa_save_value_null()); }
static qa_save_value *boolean(bool value) { return checked(qa_save_value_boolean(value)); }
static qa_save_value *str_or_null(const char *value) { return value == NULL ? null_value() : str(value); }
static void put(qa_save_value *object, const char *key, qa_save_value *child) {
  if (!qa_save_value_set(object, key, child)) {
    qa_save_value_free(child);
    qa_range_error("Out of memory");
  }
}
static void push(qa_save_value *array, qa_save_value *child) {
  if (!qa_save_value_push(array, child)) {
    qa_save_value_free(child);
    qa_range_error("Out of memory");
  }
}
static void put_num(qa_save_value *o, const char *key, double value) { put(o, key, num(value)); }
static void put_str(qa_save_value *o, const char *key, const char *value) { put(o, key, str(value)); }
static void put_bool(qa_save_value *o, const char *key, bool value) { put(o, key, boolean(value)); }
static void put_str_or_null(qa_save_value *o, const char *key, const char *value) { put(o, key, str_or_null(value)); }

typedef qa_save_value *(*element_writer)(const void *element);
static qa_save_value *list(const void *items, size_t count, size_t size, element_writer write) {
  qa_save_value *out = arr();
  for (size_t i = 0; i < count; i++) push(out, write((const char *)items + i * size));
  return out;
}
static qa_save_value *numbers(const double *values, size_t count) {
  qa_save_value *out = arr();
  for (size_t i = 0; i < count; i++) push(out, num(values[i]));
  return out;
}
static qa_save_value *strings(const char *const *values, size_t count) {
  qa_save_value *out = arr();
  for (size_t i = 0; i < count; i++) push(out, str(values[i]));
  return out;
}
static qa_save_value *vector(qa_vec3 v) {
  qa_save_value *o = obj();
  put_num(o, "x", v.x);
  put_num(o, "y", v.y);
  put_num(o, "z", v.z);
  return o;
}
static qa_save_value *program(qa_mod_program p) {
  qa_save_value *o = obj();
  put_str(o, "path", p.path);
  put_str(o, "digest", p.digest);
  return o;
}
static qa_save_value *named_strings(const qa_mod_named_string *rows, size_t count) {
  qa_save_value *out = arr();
  for (size_t i = 0; i < count; i++) {
    qa_save_value *o = obj();
    put_str(o, "name", rows[i].name);
    put_str(o, "value", rows[i].value);
    push(out, o);
  }
  return out;
}
static qa_save_value *statement(const qa_qc_statement *s) {
  qa_save_value *o = obj();
  put_num(o, "opcode", s->opcode);
  put_num(o, "a", s->a);
  put_num(o, "b", s->b);
  put_num(o, "c", s->c);
  return o;
}
static qa_save_value *statement_element(const void *e) { return statement(e); }
static qa_save_value *statements(const qa_qc_statement *rows, size_t count) {
  return list(rows, count, sizeof *rows, statement_element);
}

/* --- shared readers' shapes --- */
/* readModSourceValue / the qvm and native `value` readers. */
static qa_save_value *callback_value(const qa_mod_callback_value *v) {
  qa_save_value *o = obj();
  put_str(o, "kind", v->kind);
  if (strcmp(v->kind, "input") == 0) put_str(o, "name", v->name);
  else if (strcmp(v->kind, "float") == 0) put_num(o, "value", v->number_value);
  else if (strcmp(v->kind, "vector") == 0) put(o, "value", vector(v->vector_value));
  else put_str(o, "value", v->string_value);
  return o;
}
/* readSourceTeamValues. */
static qa_save_value *team_values(const qa_source_team_value *rows, size_t count) {
  qa_save_value *out = arr();
  for (size_t i = 0; i < count; i++) {
    qa_save_value *o = obj();
    put_num(o, "value", rows[i].value);
    put_str_or_null(o, "team", rows[i].team);
    push(out, o);
  }
  return out;
}
/* readSourceObjectives: { id, state: { storage, values }, carrier, target } &
 * owned { role, campaignGate, botGoal, change } | borrowed { role, writable }. */
static qa_save_value *objectives(const qa_source_objective_declaration *rows, size_t count,
                                 element_writer scalar, element_writer reference, element_writer call) {
  qa_save_value *out = arr();
  for (size_t i = 0; i < count; i++) {
    const qa_source_objective_declaration *d = &rows[i];
    qa_save_value *o = obj();
    put_str(o, "id", d->id);
    qa_save_value *state = obj();
    put(state, "storage", scalar(d->state_storage));
    qa_save_value *values = arr();
    for (size_t j = 0; j < d->state_value_count; j++) {
      qa_save_value *v = obj();
      put_num(v, "value", d->state_values[j].value);
      put_str(v, "stage", d->state_values[j].stage);
      put_bool(v, "complete", d->state_values[j].complete);
      push(values, v);
    }
    put(state, "values", values);
    put(o, "state", state);
    put(o, "carrier", d->carrier == NULL ? null_value() : reference(d->carrier));
    put(o, "target", d->target == NULL ? null_value() : reference(d->target));
    if (d->is_owned) {
      put_str(o, "role", "owned");
      put_bool(o, "campaignGate", d->campaign_gate);
      put_bool(o, "botGoal", d->bot_goal);
      put(o, "change", d->change == NULL ? null_value() : call(d->change));
    } else {
      put_str(o, "role", "borrowed");
      put_bool(o, "writable", d->writable);
    }
    push(out, o);
  }
  return out;
}
/* readClientOutputDeclarations. */
static qa_save_value *client_outputs(const qa_mod_client_output_declaration *rows, size_t count,
                                     element_writer scalar, element_writer vector_writer) {
  static const char *const modes[] = {"normal", "noclip", "freeze"};
  qa_save_value *out = arr();
  for (size_t i = 0; i < count; i++) {
    const qa_mod_client_output_declaration *d = &rows[i];
    qa_save_value *o = obj();
    switch (d->kind) {
      case QA_MOD_CLIENT_OUTPUT_BODY_SHAPE:
        put_str(o, "kind", "body-shape");
        put(o, "min", vector_writer(d->min_vector));
        put(o, "max", vector_writer(d->max_vector));
        break;
      case QA_MOD_CLIENT_OUTPUT_VIEW_OFFSET:
        put_str(o, "kind", "view-offset");
        if (d->height == NULL) put(o, "field", vector_writer(d->field));
        else put(o, "height", scalar(d->height));
        break;
      case QA_MOD_CLIENT_OUTPUT_MOVEMENT_MODE:
      case QA_MOD_CLIENT_OUTPUT_STANCE: {
        bool mode = d->kind == QA_MOD_CLIENT_OUTPUT_MOVEMENT_MODE;
        put_str(o, "kind", mode ? "movement-mode" : "stance");
        put(o, "field", scalar(d->field));
        if (d->has_mask) put_num(o, "mask", d->mask);
        qa_save_value *values = arr();
        size_t n = mode ? d->mode_value_count : d->stance_value_count;
        for (size_t j = 0; j < n; j++) {
          qa_save_value *v = obj();
          if (mode) {
            put_num(v, "value", d->mode_values[j].value);
            put_str(v, "mode", modes[d->mode_values[j].mode]);
          } else {
            put_num(v, "value", d->stance_values[j].value);
            put_bool(v, "crouched", d->stance_values[j].crouched);
          }
          push(values, v);
        }
        put(o, "values", values);
        break;
      }
    }
    push(out, o);
  }
  return out;
}
/* readModClientInput: { scope, phase, calls } and `outputs` when declared. */
static qa_save_value *client_input(const qa_mod_client_input_binding *rows, size_t count,
                                   element_writer call, element_writer output) {
  qa_save_value *out = arr();
  for (size_t i = 0; i < count; i++) {
    const qa_mod_client_input_binding *b = &rows[i];
    qa_save_value *o = obj();
    put_str(o, "scope", b->scope);
    put_str(o, "phase", b->phase);
    put(o, "calls", list(b->calls, b->call_count, b->call_elem_size, call));
    if (b->output_elem_size != 0) put(o, "outputs", list(b->outputs, b->output_count, b->output_elem_size, output));
    push(out, o);
  }
  return out;
}
/* readModPickupRule: { id, offered, writes, operation }. */
static qa_save_value *pickup_rule(const qa_mod_pickup_rule *rule, element_writer call) {
  qa_save_value *o = obj();
  put_str(o, "id", rule->id);
  put(o, "offered", strings(rule->offered, rule->offered_count));
  qa_save_value *writes = arr();
  for (size_t i = 0; i < rule->writes.count; i++) {
    const qa_pickup_write *w = &rule->writes.values[i];
    qa_save_value *v = obj();
    if (w->kind == QA_PICKUP_RESOURCE_PROTECTION) {
      put_str(v, "kind", "protection");
      put_str(v, "channel", w->channel == QA_PROTECTION_REGULAR ? "regular" : "powered");
    } else {
      put_str(v, "kind", "inventory");
      put_str(v, "item", w->item);
      put_str(v, "fields", w->fields);
    }
    push(writes, v);
  }
  put(o, "writes", writes);
  qa_save_value *operation = obj();
  if (rule->operation.is_gate_then_grant) {
    put_str(operation, "kind", "gate-then-grant");
    put(operation, "gate", call(rule->operation.gate));
    put(operation, "grant", call(rule->operation.grant));
    put_str(operation, "grantAccepts", rule->operation.grant_accepts);
  } else {
    put_str(operation, "kind", "boolean-grant");
    put(operation, "grant", call(rule->operation.grant));
  }
  put(o, "operation", operation);
  return o;
}
/* readItemIconDeclaration. */
static qa_save_value *item_icon(const qa_source_item_icon_declaration *icon) {
  qa_save_value *o = obj();
  put_str(o, "kind", icon->kind);
  if (strcmp(icon->kind, "shader") == 0) {
    put_str(o, "name", icon->name);
    return o;
  }
  put_str(o, "path", icon->path);
  if (strcmp(icon->kind, "wad-picture") == 0) put_str(o, "lump", icon->lump);
  return o;
}
/* readHeldWeaponDeclaration. */
static qa_save_value *held_weapon(const qa_held_weapon_declaration *held) {
  qa_save_value *o = obj();
  if (!held->is_model) {
    put_str(o, "kind", "none");
    return o;
  }
  put_str(o, "kind", "model");
  const qa_held_weapon_model *m = &held->model;
  qa_save_value *model = obj();
  put_str(model, "path", m->path);
  put_num(model, "referenceFrame", m->reference_frame);
  qa_save_value *grip = obj();
  put(grip, "origin", vector(m->grip.origin));
  qa_save_value *axis = arr();
  for (size_t i = 0; i < 3; i++) push(axis, vector(m->grip.axis.vectors[i]));
  put(grip, "axis", axis);
  put(grip, "scale", vector(m->grip.scale));
  put(model, "grip", grip);
  if (m->has_digest) put_str(model, "digest", m->digest);
  if (m->fallback != NULL) put_str(model, "fallback", m->fallback);
  if (m->part != NULL) {
    qa_save_value *part = obj();
    put(part, "digests", strings(m->part->digests.values, m->part->digests.count));
    put(part, "vertices", numbers(m->part->vertices.values, m->part->vertices.count));
    put(model, "part", part);
  }
  put(o, "model", model);
  return o;
}
/* The item definition row the three item readers share:
 * { item, label, icon?, admission, actions?, kind } and, for weapons, held? and ammo. */
static qa_save_value *item_definition(qa_item_id item, const char *label, bool has_icon,
                                      const qa_source_item_icon_declaration *icon, const char *admission,
                                      bool has_actions, const void *use, const void *drop, bool is_weapon,
                                      const qa_source_weapon_item *weapon, element_writer call) {
  qa_save_value *o = obj();
  put_str(o, "item", item);
  put_str(o, "label", label);
  if (has_icon) put(o, "icon", icon == NULL ? null_value() : item_icon(icon));
  put_str(o, "admission", admission);
  if (has_actions) {
    qa_save_value *actions = obj();
    if (use != NULL) put(actions, "use", call(use));
    if (drop != NULL) put(actions, "drop", call(drop));
    put(o, "actions", actions);
  }
  if (!is_weapon) {
    put_str(o, "kind", "counter");
    return o;
  }
  put_str(o, "kind", "weapon");
  if (weapon->held != NULL) put(o, "held", held_weapon(weapon->held));
  put_str_or_null(o, "ammo", weapon->ammo);
  return o;
}
/* The admission object of protection rows. */
static qa_save_value *admission(const qa_mod_protection_admission *a) {
  qa_save_value *o = obj();
  put_str(o, "kind", a->kind);
  if (strcmp(a->kind, "replace-primary") == 0) put_str(o, "owner", a->owner);
  return o;
}
/* ModCallbackBinding members, written before the call members. */
static void binding_members(qa_save_value *o, const qa_mod_callback_binding *b) {
  put_str(o, "id", b->id);
  put_str(o, "operation", b->operation);
  put_str(o, "stage", b->stage);
  if (strcmp(b->stage, "observe") != 0) put_str(o, "result", b->result);
}

/* ===================== QuakeC (callbacks.ts) ===================== */
static void qc_call_members(qa_save_value *o, const qa_mod_source_call *c) {
  put_str(o, "function", c->function);
  qa_save_value *arguments = arr();
  for (size_t i = 0; i < c->argument_count; i++) push(arguments, callback_value(&c->arguments[i]));
  put(o, "arguments", arguments);
  qa_save_value *globals = arr();
  for (size_t i = 0; i < c->global_count; i++) {
    qa_save_value *g = obj();
    put_str(g, "name", c->globals[i].name);
    put(g, "value", callback_value(&c->globals[i].value));
    push(globals, g);
  }
  put(o, "globals", globals);
}
static qa_save_value *qc_call(const qa_mod_source_call *c) {
  qa_save_value *o = obj();
  qc_call_members(o, c);
  return o;
}
static qa_save_value *qc_call_element(const void *e) { return qc_call(e); }
static qa_save_value *qc_calls(const qa_mod_source_call *rows, size_t count) {
  return list(rows, count, sizeof *rows, qc_call_element);
}
static qa_save_value *qc_objective_storage(const void *e) {
  const qa_qc_mod_objective_storage *s = e;
  if (!s->is_entity_field) return str(s->name);
  qa_save_value *o = obj();
  put_str(o, "kind", "entity-field");
  put_str(o, "global", s->global);
  put(o, "indirections", strings(s->indirections.values, s->indirections.count));
  put_str(o, "field", s->field);
  return o;
}
static qa_save_value *qc_actor_field(const void *e) {
  const qa_mod_actor_field *f = e;
  qa_save_value *o = obj();
  put_str(o, "field", f->field);
  put_str(o, "binding", f->binding);
  const char *b = f->binding;
  if (strcmp(b, "team") == 0) {
    put(o, "values", team_values(f->team_values, f->team_value_count));
  } else if (strcmp(b, "client-input") == 0) {
    put_str(o, "input", f->input);
    put_str(o, "update", f->update);
    if (f->has_scale) put_num(o, "scale", f->scale);
  } else if (strcmp(b, "client-flags") == 0) {
    if (f->has_grounded) put_bool(o, "grounded", true);
    if (f->has_private_mask) put_num(o, "privateMask", f->private_mask);
  } else if (strcmp(b, "userinfo") == 0) {
    put_str(o, "key", f->key);
  } else if (strcmp(b, "inventory") == 0) {
    put_str(o, "item", f->item);
  } else if (strcmp(b, "constant") == 0) {
    put(o, "value", callback_value(&f->constant_value));
  }
  return o;
}
static qa_save_value *qc_callback(const void *e) {
  const qa_mod_callback *c = e;
  qa_save_value *o = obj();
  /* { id, function, arguments, globals, operation, stage, result? } */
  put_str(o, "id", c->binding.id);
  qc_call_members(o, &c->call);
  put_str(o, "operation", c->binding.operation);
  put_str(o, "stage", c->binding.stage);
  if (strcmp(c->binding.stage, "observe") != 0) put_str(o, "result", c->binding.result);
  return o;
}
static qa_save_value *console_value(const qa_mod_console_value *v) {
  qa_save_value *o = obj();
  put_str(o, "kind", v->kind);
  if (strcmp(v->kind, "float") == 0) put_num(o, "value", v->number_value);
  else if (strcmp(v->kind, "string") == 0) put_str(o, "value", v->string_value);
  else if (strcmp(v->kind, "vector") == 0) put(o, "value", vector(v->vector_value));
  else if (strcmp(v->kind, "argument") == 0) {
    put_num(o, "index", v->index);
    put_str(o, "type", v->type);
  }
  return o;
}
static qa_save_value *qc_command(const void *e) {
  const qa_mod_console_command *c = e;
  qa_save_value *o = obj();
  put_str(o, "name", c->name);
  put_str(o, "function", c->function);
  qa_save_value *arguments = arr();
  for (size_t i = 0; i < c->argument_count; i++) push(arguments, console_value(&c->arguments[i]));
  put(o, "arguments", arguments);
  qa_save_value *globals = arr();
  for (size_t i = 0; i < c->global_count; i++) {
    qa_save_value *g = obj();
    put_str(g, "name", c->globals[i].name);
    put(g, "value", console_value(&c->globals[i].value));
    push(globals, g);
  }
  put(o, "globals", globals);
  return o;
}
static qa_save_value *qc_armor_stage(const qa_mod_qc_armor_stage *s) {
  qa_save_value *o = obj();
  put_str(o, "function", s->function);
  put_num(o, "entry", s->entry);
  put_num(o, "exit", s->exit);
  put_num(o, "target", s->target);
  put_num(o, "damage", s->damage);
  put_num(o, "saved", s->saved);
  if (s->regular_scale != NULL) {
    qa_save_value *rows = arr();
    for (size_t i = 0; i < s->regular_scale_count; i++) {
      qa_save_value *r = obj();
      put_str(r, "caller", s->regular_scale[i].caller);
      put_num(r, "statement", s->regular_scale[i].statement);
      put_num(r, "scale", s->regular_scale[i].scale);
      push(rows, r);
    }
    put(o, "regularScale", rows);
  }
  qa_save_value *flags = obj();
  if (!s->flags.is_bits) {
    put_str(flags, "kind", "none");
  } else {
    put_str(flags, "kind", "bits");
    put_num(flags, "word", s->flags.word);
    put_num(flags, "noArmor", s->flags.no_armor);
    put_num(flags, "noPowerArmor", s->flags.no_power_armor);
    put_num(flags, "noRegularArmor", s->flags.no_regular_armor);
    put_num(flags, "energy", s->flags.energy);
  }
  put(o, "flags", flags);
  put(o, "statements", statements(s->statements, s->statement_count));
  return o;
}
static qa_save_value *qc_protection(const void *e) {
  const qa_mod_qc_protection *p = e;
  qa_save_value *o = obj();
  put_str(o, "id", p->id);
  qa_save_value *absorb = obj();
  put_str(absorb, "kind", p->absorb_is_region ? "region" : "function");
  put(absorb, "call", qc_call(&p->absorb_call));
  if (p->absorb_is_region) put(absorb, "stage", qc_armor_stage(&p->absorb_stage));
  put(o, "absorb", absorb);
  if (p->admission.kind != NULL) put(o, "admission", admission(&p->admission));
  qa_save_value *flags = obj();
  put_num(flags, "noArmor", p->flags.no_armor);
  put_num(flags, "noPowerArmor", p->flags.no_power_armor);
  put_num(flags, "noRegularArmor", p->flags.no_regular_armor);
  put_num(flags, "energy", p->flags.energy);
  put_num(flags, "radius", p->flags.radius);
  put(o, "flags", flags);
  bool regular = strcmp(p->channel, "regular") == 0;
  put_str(o, "channel", p->channel);
  qa_save_value *storage = obj();
  if (regular) {
    put_str(storage, "points", p->storage_points);
    put_str_or_null(storage, "item", p->storage_item);
  } else {
    put_str(storage, "cells", p->storage_cells);
    put_str(storage, "kind", p->storage_kind);
  }
  if (p->has_selection) {
    qa_save_value *selection = obj();
    put_str(selection, "field", p->selection_field);
    if (p->has_selection_mask) put_num(selection, "mask", p->selection_mask);
    qa_save_value *values = arr();
    size_t n = regular ? p->item_value_count : p->kind_value_count;
    for (size_t i = 0; i < n; i++) {
      qa_save_value *v = obj();
      if (regular) {
        put_num(v, "value", p->item_values[i].value);
        put_str_or_null(v, "item", p->item_values[i].item);
      } else {
        put_num(v, "value", p->kind_values[i].value);
        put_str(v, "kind", p->kind_values[i].kind);
      }
      push(values, v);
    }
    put(selection, "values", values);
    put(storage, "selection", selection);
  }
  put(o, "storage", storage);
  return o;
}
static qa_save_value *qc_pickup(const void *e) { return pickup_rule(e, qc_call_element); }
static qa_save_value *qc_weapon_values(const char *field, const qa_mod_qc_weapon_value *rows, size_t count) {
  qa_save_value *o = obj();
  put_str(o, "field", field);
  qa_save_value *values = arr();
  for (size_t i = 0; i < count; i++) {
    qa_save_value *v = obj();
    put_num(v, "value", rows[i].value);
    put_str(v, "item", rows[i].item);
    push(values, v);
  }
  put(o, "values", values);
  return o;
}
/* readQcWeaponStageDeclaration. */
static qa_save_value *qc_weapon_stage(const qa_qc_weapon_stage_declaration *s) {
  qa_save_value *o = obj();
  put_str(o, "dispatcher", s->dispatcher);
  put(o, "continuations", strings(s->continuations.values, s->continuations.count));
  qa_save_value *repeats = arr();
  for (size_t i = 0; i < s->repeat_count; i++) {
    const qa_qc_weapon_stage_repeat *r = &s->repeats[i];
    qa_save_value *v = obj();
    put_str(v, "function", r->function);
    put_num(v, "entry", r->entry);
    put_num(v, "exit", r->exit);
    qa_save_value *result = obj();
    put_num(result, "word", r->result_word);
    put_num(result, "value", r->result_value);
    put(v, "result", result);
    put(v, "statements", statements(r->statements, r->statement_count));
    push(repeats, v);
  }
  put(o, "repeats", repeats);
  return o;
}
static qa_save_value *qc_items(const qa_mod_qc_items *items) {
  qa_save_value *o = obj();
  qa_save_value *definitions = arr();
  for (size_t i = 0; i < items->definition_count; i++) {
    const qa_mod_qc_item_definition *d = &items->definitions[i];
    push(definitions, item_definition(d->item, d->label, d->has_icon, d->icon, d->admission, d->has_actions,
                                      d->action_use, d->action_drop, d->is_weapon, &d->weapon, qc_call_element));
  }
  put(o, "definitions", definitions);
  qa_save_value *storage = arr();
  for (size_t i = 0; i < items->storage_count; i++) {
    const qa_mod_qc_item_storage *s = &items->storage[i];
    qa_save_value *v = obj();
    if (strcmp(s->kind, "bits") == 0) {
      put_str(v, "kind", "bits");
      put_str(v, "field", s->field);
      put_num(v, "privateMask", s->private_mask);
      qa_save_value *masks = arr();
      for (size_t j = 0; j < s->item_count; j++) {
        qa_save_value *m = obj();
        put_str(m, "item", s->items[j].item);
        put_num(m, "mask", s->items[j].mask);
        push(masks, m);
      }
      put(v, "items", masks);
    } else {
      put_str(v, "kind", "counter");
      put_str(v, "field", s->field);
      put_str(v, "item", s->item);
      qa_save_value *capacity = obj();
      if (s->capacity.is_constant) {
        put_str(capacity, "kind", "constant");
        put_num(capacity, "value", s->capacity.value);
      } else {
        put_str(capacity, "kind", "field");
        put_str(capacity, "field", s->capacity.field);
      }
      put(v, "capacity", capacity);
    }
    push(storage, v);
  }
  put(o, "storage", storage);
  if (items->weapons != NULL) {
    const qa_mod_qc_weapons *w = items->weapons;
    qa_save_value *weapons = obj();
    put(weapons, "stage", qc_weapon_stage(w->stage));
    put(weapons, "selected", qc_weapon_values(w->selected_field, w->selected_values, w->selected_value_count));
    qa_save_value *select = qc_weapon_values(w->select_field, w->select_values, w->select_value_count);
    put(select, "call", qc_call(&w->select_call));
    put(weapons, "select", select);
    put(weapons, "resume", qc_calls(w->resume, w->resume_count));
    qa_save_value *model = obj();
    put_str(model, "field", w->model_field);
    put_str(model, "frame", w->model_frame);
    put(weapons, "model", model);
    put(o, "weapons", weapons);
  }
  return o;
}
static qa_save_value *qc_combat(const qa_mod_qc_combat *c) {
  qa_save_value *o = obj();
  put(o, "damage", qc_call(&c->damage));
  if (c->damage_scale != NULL) {
    const qa_mod_qc_damage_scale *s = c->damage_scale;
    qa_save_value *scale = obj();
    if (s->kind != NULL) put_str(scale, "kind", s->kind);
    put_str(scale, "function", s->function);
    put_num(scale, "entry", s->entry);
    put_num(scale, "exit", s->exit);
    put_num(scale, "damage", s->damage);
    put(scale, "statements", statements(s->statements, s->statement_count));
    put(o, "damageScale", scale);
  }
  if (c->armor_stage != NULL) put(o, "armorStage", qc_armor_stage(c->armor_stage));
  if (c->empty_armor != NULL) {
    qa_save_value *empty = obj();
    put_str(empty, "item", c->empty_armor->item);
    put_num(empty, "absorption", c->empty_armor->absorption);
    put(o, "emptyArmor", empty);
  }
  return o;
}
static qa_save_value *string_element(const void *e) { return str(*(const char *const *)e); }
static qa_save_value *qc_input_output(const void *e) {
  const qa_mod_qc_input_output *out = e;
  qa_save_value *o = obj();
  if (!out->is_handler) {
    put_str(o, "kind", "field");
    put_str(o, "field", out->field);
    return o;
  }
  put_str(o, "kind", "handler");
  put_str(o, "function", out->function);
  put(o, "inputs", strings(out->inputs, out->input_count));
  return o;
}

qa_save_value *qa_quakec_mod_declaration_value(const qa_mod_callback_declaration *d) {
  qa_save_value *o = obj();
  put_num(o, "version", (double)d->version);
  put_str(o, "runtime", d->runtime);
  put(o, "program", program(d->program));
  if (d->objectives != NULL)
    put(o, "objectives", objectives(d->objectives, d->objective_count, qc_objective_storage, qc_objective_storage, qc_call_element));
  put(o, "actorFields", list(d->actor_fields, d->actor_field_count, sizeof *d->actor_fields, qc_actor_field));
  put(o, "callbacks", list(d->callbacks, d->callback_count, sizeof *d->callbacks, qc_callback));
  if (d->client_presentation != NULL) {
    qa_save_value *presentation = obj();
    put_str(presentation, "hud", d->client_presentation->hud);
    put_str(presentation, "view", d->client_presentation->view);
    put(o, "clientPresentation", presentation);
  }
  if (d->clients != NULL) {
    const qa_mod_qc_clients *c = d->clients;
    qa_save_value *clients = obj();
    put_num(clients, "maximum", c->maximum);
    if (c->outputs != NULL) put(clients, "outputs", client_outputs(c->outputs, c->output_count, string_element, string_element));
    put(clients, "admit", qc_calls(c->admit, c->admit_count));
    put(clients, "userinfo", qc_calls(c->userinfo, c->userinfo_count));
    put(clients, "disconnect", qc_calls(c->disconnect, c->disconnect_count));
    if (c->frame != NULL) put(clients, "frame", qc_calls(c->frame, c->frame_count));
    if (c->input != NULL) put(clients, "input", client_input(c->input, c->input_count, qc_call_element, qc_input_output));
    put(o, "clients", clients);
  }
  if (d->initialize != NULL) put(o, "initialize", qc_calls(d->initialize, d->initialize_count));
  if (d->frame != NULL) put(o, "frame", qc_call(d->frame));
  if (d->cvars != NULL) put(o, "cvars", named_strings(d->cvars, d->cvar_count));
  if (d->commands != NULL) put(o, "commands", list(d->commands, d->command_count, sizeof *d->commands, qc_command));
  if (d->protection != NULL) put(o, "protection", list(d->protection, d->protection_count, sizeof *d->protection, qc_protection));
  if (d->pickups != NULL) put(o, "pickups", list(d->pickups, d->pickup_count, sizeof *d->pickups, qc_pickup));
  if (d->items != NULL) put(o, "items", qc_items(d->items));
  if (d->combat != NULL) put(o, "combat", qc_combat(d->combat));
  return o;
}

/* ===================== QVM (qvm-callbacks.ts) ===================== */
static qa_save_value *qvm_argument(const qa_qvm_mod_value *v) {
  qa_save_value *o = obj();
  put_str(o, "kind", v->kind);
  if (strcmp(v->kind, "actor") == 0) {
    put_str(o, "record", v->record);
    put_str(o, "input", v->input);
  } else if (strcmp(v->kind, "client") == 0) {
    put_str(o, "input", v->input);
  } else if (strcmp(v->kind, "time") == 0) {
    put_str(o, "input", v->time_input);
    put_str(o, "units", v->units);
    put_str(o, "encoding", v->encoding);
  } else if (strcmp(v->kind, "address") == 0) {
    put_num(o, "value", v->address_value);
  } else {
    put(o, "value", callback_value(&v->value));
  }
  return o;
}
static qa_save_value *qvm_globals(const qa_qvm_mod_global_value *rows, size_t count) {
  qa_save_value *out = arr();
  for (size_t i = 0; i < count; i++) {
    qa_save_value *g = obj();
    put_num(g, "address", rows[i].address);
    put(g, "value", qvm_argument(&rows[i].value));
    push(out, g);
  }
  return out;
}
static void qvm_call_members(qa_save_value *o, const qa_qvm_mod_source_call *c) {
  put_num(o, "entry", c->entry);
  qa_save_value *arguments = arr();
  for (size_t i = 0; i < c->argument_count; i++) push(arguments, qvm_argument(&c->arguments[i]));
  put(o, "arguments", arguments);
  put(o, "globals", qvm_globals(c->globals, c->global_count));
  put_str(o, "returns", c->returns);
}
static qa_save_value *qvm_call(const qa_qvm_mod_source_call *c) {
  qa_save_value *o = obj();
  qvm_call_members(o, c);
  return o;
}
static qa_save_value *qvm_call_element(const void *e) { return qvm_call(e); }
static qa_save_value *qvm_calls(const qa_qvm_mod_source_call *rows, size_t count) {
  return list(rows, count, sizeof *rows, qvm_call_element);
}
static qa_save_value *qvm_actor_field(const void *e) {
  const qa_qvm_mod_actor_field *f = e;
  qa_save_value *o = obj();
  put_num(o, "offset", f->offset);
  put_str(o, "binding", f->binding);
  const char *b = f->binding;
  if (strcmp(b, "record") == 0) {
    put_str(o, "record", f->record);
    return o;
  }
  if (strcmp(b, "constant-vector") == 0) {
    put(o, "value", vector(f->constant_vector));
    return o;
  }
  if (strcmp(b, "private") == 0) {
    put_num(o, "byteLength", f->byte_length);
    return o;
  }
  if (f->access != NULL) put_str(o, "access", f->access);
  if (strcmp(b, "team") == 0) {
    put_str(o, "encoding", f->encoding);
    put(o, "values", team_values(f->team_values, f->team_value_count));
  } else if (strcmp(b, "score") == 0 || strcmp(b, "health") == 0) {
    put_str(o, "encoding", f->encoding);
  } else if (strcmp(b, "inventory") == 0) {
    put_str(o, "encoding", f->encoding);
    put_str(o, "item", f->item);
  } else if (strcmp(b, "constant") == 0) {
    put_str(o, "encoding", f->encoding);
    put_num(o, "value", f->constant_value);
  }
  return o;
}
static qa_save_value *qvm_protection_scalar(const qa_qvm_mod_protection_scalar *s) {
  qa_save_value *o = obj();
  put_str(o, "record", s->record);
  put_num(o, "offset", s->offset);
  put_str(o, "encoding", s->encoding);
  return o;
}
static qa_save_value *qvm_protection_scalar_element(const void *e) { return qvm_protection_scalar(e); }
static qa_save_value *qvm_record_offset(const void *e) {
  const qa_qvm_mod_record_offset *r = e;
  qa_save_value *o = obj();
  put_str(o, "record", r->record);
  put_num(o, "offset", r->offset);
  return o;
}
static qa_save_value *qvm_protection_selection(const qa_qvm_mod_protection_selection *s, bool powered) {
  qa_save_value *o = obj();
  put(o, "field", qvm_protection_scalar(&s->field));
  put(o, "mask", s->has_mask ? num(s->mask) : null_value());
  qa_save_value *values = arr();
  for (size_t i = 0; i < s->value_count; i++) {
    qa_save_value *v = obj();
    put_num(v, "value", s->values[i].value);
    if (powered) put_str(v, "selected", s->values[i].selected_kind);
    else put_str_or_null(v, "selected", s->values[i].selected_item);
    push(values, v);
  }
  put(o, "values", values);
  return o;
}
static qa_save_value *qvm_protection(const void *e) {
  const qa_qvm_mod_protection *p = e;
  qa_save_value *o = obj();
  put_str(o, "id", p->id);
  put(o, "admission", admission(&p->admission));
  put(o, "absorb", qvm_call(&p->absorb));
  qa_save_value *flags = obj();
  put_num(flags, "noArmor", p->flags.no_armor);
  put_num(flags, "noPowerArmor", p->flags.no_power_armor);
  put_num(flags, "noRegularArmor", p->flags.no_regular_armor);
  put_num(flags, "energy", p->flags.energy);
  put_num(flags, "radius", p->flags.radius);
  put(o, "flags", flags);
  put_str(o, "channel", p->channel);
  qa_save_value *storage = obj();
  if (strcmp(p->channel, "regular") == 0) {
    put(storage, "points", qvm_protection_scalar(&p->storage_points));
    put_str_or_null(storage, "item", p->storage_item);
    if (p->has_selection) put(storage, "selection", qvm_protection_selection(&p->selection, false));
  } else {
    put(storage, "cells", qvm_protection_scalar(&p->storage_cells));
    put(storage, "selection", qvm_protection_selection(&p->selection, true));
  }
  put(o, "storage", storage);
  return o;
}
static qa_save_value *qvm_combat_call(const qa_qvm_combat_call *c) {
  qa_save_value *o = obj();
  qa_save_value *roles = obj();
  for (size_t i = 0; i < c->role_count; i++) put_num(roles, c->roles[i].role, c->roles[i].index);
  put(o, "roles", roles);
  qa_save_value *extras = arr();
  for (size_t i = 0; i < c->extra_count; i++) {
    qa_save_value *x = obj();
    put_num(x, "index", c->extras[i].index);
    put_str(x, "kind", c->extras[i].kind);
    put_num(x, "value", c->extras[i].value);
    push(extras, x);
  }
  put(o, "extras", extras);
  return o;
}
static qa_save_value *qvm_combat(const qa_qvm_mod_combat *c) {
  qa_save_value *o = obj();
  put_str(o, "abi", c->abi);
  put_num(o, "entry", c->fields.entry);
  put_num(o, "health", c->fields.health);
  put_num(o, "takedamage", c->fields.takedamage);
  put_num(o, "flags", c->fields.flags);
  put_num(o, "godmode", c->fields.godmode);
  put_num(o, "noKnockback", c->fields.no_knockback);
  put(o, "globals", qvm_globals(c->fields.globals, c->fields.global_count));
  if (c->fields.client == NULL) {
    put(o, "client", null_value());
  } else {
    const qa_qvm_mod_combat_client *k = c->fields.client;
    qa_save_value *client = obj();
    put_num(client, "pointer", k->pointer);
    put_str(client, "record", k->record);
    put_num(client, "health", k->health);
    put_num(client, "armor", k->armor);
    put_num(client, "protection", k->protection);
    put_num(client, "team", k->team);
    put(o, "client", client);
  }
  if (strcmp(c->abi, "q3-g-damage") == 0) return o;
  qa_save_value *calls = obj();
  put(calls, "damage", qvm_combat_call(&c->calls.damage));
  put(calls, "touch", qvm_combat_call(&c->calls.touch));
  put(calls, "use", qvm_combat_call(&c->calls.use));
  put(calls, "pain", qvm_combat_call(&c->calls.pain));
  put(calls, "die", qvm_combat_call(&c->calls.die));
  put(o, "calls", calls);
  qa_save_value *flags = obj();
  put_num(flags, "radius", c->damage_flags.radius);
  put_num(flags, "noArmor", c->damage_flags.no_armor);
  put_num(flags, "noKnockback", c->damage_flags.no_knockback);
  put_num(flags, "noProtection", c->damage_flags.no_protection);
  put_num(flags, "noTeamProtection", c->damage_flags.no_team_protection);
  put(o, "damageFlags", flags);
  qa_save_value *mass = obj();
  if (c->mass.is_constant) {
    put_str(mass, "kind", "constant");
    put_num(mass, "value", c->mass.value);
  } else {
    put_str(mass, "kind", "entity");
    put_num(mass, "offset", c->mass.offset);
    put_str(mass, "storage", c->mass.storage);
  }
  put(o, "mass", mass);
  qa_save_value *teams = arr();
  for (size_t i = 0; i < c->team_count; i++) {
    qa_save_value *t = obj();
    put_num(t, "value", c->teams[i].value);
    put_str(t, "team", c->teams[i].team);
    push(teams, t);
  }
  put(o, "teams", teams);
  return o;
}
/* inputPointer / qvm-items pointer: { indirections, offset, kind, index | address }. */
static qa_save_value *qvm_pointer(const qa_qvm_mod_input_pointer *p) {
  qa_save_value *o = obj();
  put(o, "indirections", numbers(p->indirections.values, p->indirections.count));
  put_num(o, "offset", p->offset);
  if (p->is_global) {
    put_str(o, "kind", "global");
    put_num(o, "address", p->address);
  } else {
    put_str(o, "kind", "argument");
    put_num(o, "index", p->index);
  }
  return o;
}
static qa_save_value *qvm_objective_address(const void *e) {
  const qa_qvm_mod_objective_address *a = e;
  return a->is_global_pointer ? qvm_pointer(&a->pointer) : num(a->address);
}
static qa_save_value *qvm_objective_scalar(const void *e) {
  const qa_qvm_mod_objective_scalar *s = e;
  qa_save_value *o = obj();
  if (s->address_is_global_pointer) {
    qa_qvm_mod_input_pointer pointer;
    memset(&pointer, 0, sizeof pointer);
    pointer.is_global = true;
    pointer.address = s->address;
    pointer.indirections = s->indirections;
    pointer.offset = s->offset;
    put(o, "address", qvm_pointer(&pointer));
  } else {
    put_num(o, "address", s->address);
  }
  put_str(o, "encoding", s->encoding);
  return o;
}
static qa_save_value *qvm_input_output(const void *e) {
  const qa_qvm_mod_input_output *out = e;
  qa_save_value *o = obj();
  if (strcmp(out->kind, "field") == 0) {
    put_str(o, "kind", "field");
    put_str(o, "record", out->record);
    put_num(o, "offset", out->offset);
    qa_save_value *value = obj();
    put_str(value, "input", out->value_input);
    if (strcmp(out->value_input, "view-angles") != 0) {
      put_str(value, "encoding", out->value_encoding);
      put_num(value, "scale", out->value_scale);
    }
    put(o, "value", value);
    return o;
  }
  put_num(o, "entry", out->entry);
  qa_save_value *actor = obj();
  put_str(actor, "record", out->actor_record);
  put(actor, "pointer", qvm_pointer(&out->actor_pointer));
  put(o, "actor", actor);
  put_str(o, "kind", out->kind);
  if (strcmp(out->kind, "command") == 0) {
    put(o, "command", qvm_pointer(&out->command));
    put(o, "inputs", strings(out->inputs, out->input_count));
    return o;
  }
  put(o, "inputs", strings(out->inputs, out->input_count));
  if (out->has_returns) {
    qa_save_value *returns = obj();
    put_str(returns, "encoding", out->returns_encoding);
    put_num(returns, "value", out->returns_value);
    put(o, "returns", returns);
  }
  return o;
}
static qa_save_value *qvm_source_actors(const qa_qvm_mod_source_actors *a) {
  qa_save_value *o = obj();
  put_num(o, "allocate", a->allocate);
  qa_save_value *release = obj();
  put_num(release, "entry", a->release.entry);
  put_num(release, "argument", a->release.argument);
  put(o, "release", release);
  if (a->initial_stores.values != NULL) put(o, "initialStores", numbers(a->initial_stores.values, a->initial_stores.count));
  put_num(o, "inuse", a->inuse);
  put_num(o, "eventEntityType", a->event_entity_type);
  put(o, "update", a->update == NULL ? null_value() : qvm_call(a->update));
  if (a->frame != NULL) {
    const qa_qvm_mod_actor_frame *f = a->frame;
    qa_save_value *frame = obj();
    put(frame, "call", qvm_call(&f->call));
    qa_save_value *clock = obj();
    put_num(clock, "address", f->clock.address);
    put_num(clock, "store", f->clock.store);
    put_num(clock, "argument", f->clock.argument);
    put(frame, "clock", clock);
    qa_save_value *owned = arr();
    for (size_t i = 0; i < f->owned_count; i++) {
      qa_save_value *v = obj();
      put_num(v, "instruction", f->owned[i].instruction);
      put_num(v, "localInstruction", f->owned[i].local_instruction);
      push(owned, v);
    }
    put(frame, "owned", owned);
    qa_save_value *end = obj();
    put_num(end, "instruction", f->end.instruction);
    put_bool(end, "completedTaken", f->end.completed_taken);
    put(frame, "end", end);
    put(o, "frame", frame);
  }
  if (a->callbacks != NULL) {
    const qa_qvm_mod_source_callbacks *c = a->callbacks;
    qa_save_value *callbacks = obj();
    put(callbacks, "touch", c->has_touch ? num(c->touch) : null_value());
    put(callbacks, "use", c->has_use ? num(c->use) : null_value());
    put(callbacks, "pain", c->has_pain ? num(c->pain) : null_value());
    put(callbacks, "die", c->has_die ? num(c->die) : null_value());
    put(o, "callbacks", callbacks);
  }
  return o;
}
/* qvm-items.ts */
static qa_save_value *qvm_item_field(const qa_qvm_item_field *f) {
  qa_save_value *o = obj();
  put_str(o, "record", f->record);
  put_num(o, "offset", f->offset);
  return o;
}
static qa_save_value *qvm_item_test(const void *e) {
  const qa_qvm_item_test *t = e;
  qa_save_value *o = obj();
  put(o, "field", qvm_item_field(&t->field));
  put(o, "mask", t->has_mask ? num(t->mask) : null_value());
  put_str(o, "comparison", t->comparison);
  put_num(o, "value", t->value);
  return o;
}
static qa_save_value *qvm_weapon_actor(const qa_qvm_weapon_actor *a) {
  qa_save_value *o = obj();
  put_str(o, "record", a->record);
  put(o, "pointer", qvm_pointer(&a->pointer));
  return o;
}
static qa_save_value *qvm_predicates(const qa_qvm_weapon_predicate *rows, size_t count) {
  qa_save_value *out = arr();
  for (size_t i = 0; i < count; i++) {
    qa_save_value *v = obj();
    put_num(v, "instruction", rows[i].instruction);
    put_bool(v, "unselected", rows[i].unselected);
    push(out, v);
  }
  return out;
}
static qa_save_value *qvm_item_storage(const qa_qvm_item_storage *s) {
  qa_save_value *o = obj();
  put_str(o, "kind", s->kind);
  put(o, "field", qvm_item_field(&s->field));
  if (strcmp(s->kind, "counter") == 0) {
    put_str(o, "item", s->item);
    const qa_qvm_item_capacity *c = &s->capacity;
    qa_save_value *capacity = obj();
    put_str(capacity, "kind", c->kind);
    if (strcmp(c->kind, "constant") == 0) {
      put_num(capacity, "value", c->value);
    } else if (strcmp(c->kind, "field") == 0) {
      put(capacity, "field", qvm_item_field(&c->field));
    } else {
      put_num(capacity, "instruction", c->instruction);
      qa_save_value *overrides = arr();
      for (size_t i = 0; i < c->override_count; i++) {
        qa_save_value *v = obj();
        put_num(v, "address", c->overrides[i].address);
        put_str(v, "comparison", c->overrides[i].comparison);
        put_num(v, "value", c->overrides[i].value);
        put_num(v, "instruction", c->overrides[i].instruction);
        push(overrides, v);
      }
      put(capacity, "overrides", overrides);
    }
    put(o, "capacity", capacity);
  } else {
    put_num(o, "privateMask", s->private_mask);
    qa_save_value *masks = arr();
    for (size_t i = 0; i < s->item_count; i++) {
      qa_save_value *m = obj();
      put_str(m, "item", s->items[i].item);
      put_num(m, "mask", s->items[i].mask);
      push(masks, m);
    }
    put(o, "items", masks);
  }
  return o;
}
static qa_save_value *qvm_items(const qa_qvm_mod_items *items) {
  qa_save_value *o = obj();
  qa_save_value *definitions = arr();
  for (size_t i = 0; i < items->definition_count; i++) {
    const qa_qvm_mod_item_definition *d = &items->definitions[i];
    push(definitions, item_definition(d->item, d->label, d->has_icon, d->icon, d->admission, d->has_actions,
                                      d->action_use, d->action_drop, d->is_weapon, &d->weapon, qvm_call_element));
  }
  put(o, "definitions", definitions);
  qa_save_value *storage = arr();
  for (size_t i = 0; i < items->storage_count; i++) push(storage, qvm_item_storage(&items->storage[i]));
  put(o, "storage", storage);
  if (items->weapons != NULL) {
    const qa_qvm_mod_weapons *w = items->weapons;
    const qa_qvm_weapon_stage *s = &w->stage;
    qa_save_value *weapons = obj();
    qa_save_value *input = obj();
    put_num(input, "entry", w->input.entry);
    put(input, "clock", qvm_item_field(&w->input.clock));
    put(weapons, "input", input);
    qa_save_value *stage = obj();
    qa_save_value *dispatcher = obj();
    put_num(dispatcher, "entry", s->dispatcher.entry);
    put(dispatcher, "actor", qvm_weapon_actor(&s->dispatcher.actor));
    put(stage, "dispatcher", dispatcher);
    put(stage, "predicates", qvm_predicates(s->predicates, s->predicate_count));
    put(stage, "settled", list(s->settled, s->settled_count, sizeof *s->settled, qvm_item_test));
    qa_save_value *selection = obj();
    put(selection, "field", qvm_item_field(&s->selection.field));
    qa_save_value *values = arr();
    for (size_t i = 0; i < s->selection.value_count; i++) {
      qa_save_value *v = obj();
      put_num(v, "value", s->selection.values[i].value);
      put_str(v, "item", s->selection.values[i].item);
      push(values, v);
    }
    put(selection, "values", values);
    put(stage, "selection", selection);
    qa_save_value *request = obj();
    put_num(request, "entry", s->request.entry);
    put_num(request, "argument", s->request.argument);
    put(request, "accepted", list(s->request.accepted, s->request.accepted_count, sizeof *s->request.accepted, qvm_item_test));
    put(stage, "request", request);
    const qa_qvm_weapon_continuation *k = &s->continuation;
    qa_save_value *continuation = obj();
    put_num(continuation, "entry", k->entry);
    put(continuation, "actor", qvm_weapon_actor(&k->actor));
    put_num(continuation, "instruction", k->instruction);
    put_bool(continuation, "originalTaken", k->original_taken);
    put(continuation, "when", list(k->when, k->when_count, sizeof *k->when, qvm_item_test));
    put(continuation, "predicates", qvm_predicates(k->predicates, k->predicate_count));
    qa_save_value *projection = obj();
    put(projection, "movement", qvm_pointer(&k->projection.movement));
    put_num(projection, "byteLength", k->projection.byte_length);
    put_num(projection, "minimum", k->projection.minimum);
    put_num(projection, "maximum", k->projection.maximum);
    put(projection, "viewHeight", qvm_item_field(&k->projection.view_height));
    put(projection, "ground", qvm_item_field(&k->projection.ground));
    put(continuation, "projection", projection);
    qa_save_value *calls = arr();
    for (size_t i = 0; i < k->call_count; i++) {
      qa_save_value *v = obj();
      put_num(v, "instruction", k->calls[i].instruction);
      put(v, "call", qvm_call(&k->calls[i].call));
      push(calls, v);
    }
    put(continuation, "calls", calls);
    put(stage, "continuation", continuation);
    put(weapons, "stage", stage);
    put(o, "weapons", weapons);
  }
  return o;
}
/* qvm-presentation.ts */
static qa_save_value *presentation_call(const void *e) {
  const qa_qvm_presentation_call *c = e;
  qa_save_value *o = obj();
  put_num(o, "entry", c->entry);
  qa_save_value *arguments = arr();
  for (size_t i = 0; i < c->argument_count; i++) {
    const qa_qvm_presentation_argument *a = &c->arguments[i];
    qa_save_value *v = obj();
    put_str(v, "kind", a->kind);
    if (strcmp(a->kind, "source") == 0) put_str(v, "value", a->source_value);
    else put_num(v, "value", a->number_value);
    push(arguments, v);
  }
  put(o, "arguments", arguments);
  if (c->when != NULL) put_str(o, "when", c->when);
  return o;
}
static qa_save_value *presentation_calls(const qa_qvm_presentation_call *rows, size_t count) {
  return list(rows, count, sizeof *rows, presentation_call);
}
static qa_save_value *presentation_program(const qa_qvm_presentation_program *p) {
  qa_save_value *o = obj();
  put_str(o, "path", p->path);
  put_str(o, "digest", p->digest);
  put_str(o, "abiProfile", p->abi_profile);
  return o;
}
static void presentation_base(qa_save_value *o, const qa_qvm_presentation_base *b) {
  put_num(o, "version", (double)b->version);
  put(o, "gameplay", presentation_program(&b->gameplay));
  put(o, "cgame", presentation_program(&b->cgame));
  if (b->hud != NULL) {
    qa_save_value *hud = obj();
    put_str(hud, "mode", b->hud->mode);
    put(hud, "frame", presentation_calls(b->hud->frame, b->hud->frame_count));
    put(o, "hud", hud);
  }
  put(o, "initialize", presentation_calls(b->initialize, b->initialize_count));
  put(o, "refresh", presentation_calls(b->refresh, b->refresh_count));
  put(o, "frame", presentation_calls(b->frame, b->frame_count));
}
static void view_lists(qa_save_value *storage, qa_double_view angles, qa_double_view axis) {
  if (angles.values != NULL) put(storage, "viewAngles", numbers(angles.values, angles.count));
  if (axis.values != NULL) put(storage, "viewAxis", numbers(axis.values, axis.count));
}
static qa_save_value *qvm_presentation(const qa_qvm_mod_presentation_declaration *p) {
  qa_save_value *o = obj();
  if (p->is_scene) {
    const qa_qvm_scene_presentation *s = p->as.scene;
    presentation_base(o, &s->base);
    put_str(o, "runtime", s->runtime);
    qa_save_value *cvars = arr();
    for (size_t i = 0; i < s->cvar_count; i++) {
      qa_save_value *v = obj();
      put_str(v, "name", s->cvars[i].name);
      put_str(v, "value", s->cvars[i].value);
      push(cvars, v);
    }
    put(o, "cvars", cvars);
    qa_save_value *storage = obj();
    put_num(storage, "gameState", s->storage.game_state);
    put(storage, "time", numbers(s->storage.time.values, s->storage.time.count));
    put(storage, "frameTime", numbers(s->storage.frame_time.values, s->storage.frame_time.count));
    put(storage, "viewOrigin", numbers(s->storage.view_origin.values, s->storage.view_origin.count));
    view_lists(storage, s->storage.view_angles, s->storage.view_axis);
    put_num(storage, "serverCommandSequence", s->storage.server_command_sequence);
    qa_save_value *centities = obj();
    put_num(centities, "address", s->storage.centities.address);
    put_num(centities, "stride", s->storage.centities.stride);
    put_num(centities, "capacity", s->storage.centities.capacity);
    put_num(centities, "state", s->storage.centities.state);
    put_num(centities, "previousEvent", s->storage.centities.previous_event);
    put_num(centities, "snapshotTime", s->storage.centities.snapshot_time);
    put(storage, "centities", centities);
    put(o, "storage", storage);
    put(o, "snapshots", presentation_calls(s->snapshots, s->snapshot_count));
    put_num(o, "eventEntityType", s->event_entity_type);
    qa_save_value *check = obj();
    put_num(check, "entry", s->event_check.entry);
    put_num(check, "centityArgument", s->event_check.centity_argument);
    put(o, "eventCheck", check);
    qa_save_value *body = obj();
    qa_save_value *player = obj();
    put_num(player, "entry", s->body.player.entry);
    put_num(player, "centityArgument", s->body.player.centity_argument);
    put(body, "player", player);
    qa_save_value *mesh = obj();
    put_num(mesh, "entry", s->body.mesh.entry);
    put_num(mesh, "entityArgument", s->body.mesh.entity_argument);
    put_num(mesh, "stateArgument", s->body.mesh.state_argument);
    put_num(mesh, "shaderOffset", s->body.mesh.shader_offset);
    if (s->body.mesh.parts != NULL) {
      qa_save_value *parts = arr();
      for (size_t i = 0; i < s->body.mesh.part_count; i++) {
        qa_save_value *v = obj();
        put_num(v, "call", s->body.mesh.parts[i].call);
        put_str(v, "part", s->body.mesh.parts[i].part);
        push(parts, v);
      }
      put(mesh, "parts", parts);
    }
    put(body, "mesh", mesh);
    put(o, "body", body);
    return o;
  }
  const qa_qvm_player_event_presentation *e = p->as.player_events;
  presentation_base(o, &e->base);
  put_str(o, "runtime", e->runtime);
  qa_save_value *storage = obj();
  put_num(storage, "gameState", e->storage.game_state);
  put_num(storage, "playerState", e->storage.player_state);
  qa_save_value *snapshot = obj();
  put_str(snapshot, "kind", "synthetic-player-event");
  put_num(snapshot, "address", e->storage.snapshot.address);
  put(snapshot, "pointers", numbers(e->storage.snapshot.pointers.values, e->storage.snapshot.pointers.count));
  put(storage, "snapshot", snapshot);
  qa_save_value *centities = obj();
  put_num(centities, "address", e->storage.centities.address);
  put_num(centities, "stride", e->storage.centities.stride);
  put_num(centities, "capacity", e->storage.centities.capacity);
  put_num(centities, "state", e->storage.centities.state);
  put_num(centities, "origin", e->storage.centities.origin);
  put(storage, "centities", centities);
  put(storage, "time", numbers(e->storage.time.values, e->storage.time.count));
  put(storage, "frameTime", numbers(e->storage.frame_time.values, e->storage.frame_time.count));
  put(storage, "viewOrigin", numbers(e->storage.view_origin.values, e->storage.view_origin.count));
  view_lists(storage, e->storage.view_angles, e->storage.view_axis);
  put(o, "storage", storage);
  put(o, "project", presentation_calls(e->project, e->project_count));
  put(o, "event", presentation_call(&e->event));
  return o;
}
static qa_save_value *qvm_pickup(const void *e) {
  const qa_qvm_mod_pickup *p = e;
  qa_save_value *o = pickup_rule(&p->rule, qvm_call_element);
  qa_save_value *context = arr();
  for (size_t i = 0; i < p->context_count; i++) {
    qa_save_value *v = obj();
    put_str(v, "record", p->context[i].record);
    put_num(v, "offset", p->context[i].offset);
    put(v, "value", qvm_argument(&p->context[i].value));
    push(context, v);
  }
  put(o, "context", context);
  return o;
}
static qa_save_value *qvm_actor_record(const void *e) {
  const qa_qvm_mod_actor_record *r = e;
  qa_save_value *o = obj();
  put_str(o, "id", r->id);
  put_num(o, "address", r->address);
  put_num(o, "stride", r->stride);
  put_num(o, "capacity", r->capacity);
  put(o, "fields", list(r->fields, r->field_count, sizeof *r->fields, qvm_actor_field));
  return o;
}
static qa_save_value *qvm_callback(const void *e) {
  const qa_qvm_mod_callback *c = e;
  qa_save_value *o = obj();
  binding_members(o, &c->binding);
  qvm_call_members(o, &c->call);
  return o;
}

qa_save_value *qa_qvm_mod_declaration_value(const qa_qvm_mod_callback_declaration *d) {
  qa_save_value *o = obj();
  put_num(o, "version", (double)d->version);
  put_str(o, "runtime", d->runtime);
  put(o, "program", program(d->program));
  put_str(o, "abiProfile", d->abi_profile);
  if (d->presentation != NULL) put(o, "presentation", qvm_presentation(d->presentation));
  put_str_or_null(o, "spawnEntities", d->spawn_entities);
  if (d->clients != NULL) {
    const qa_qvm_mod_clients *c = d->clients;
    qa_save_value *clients = obj();
    put_num(clients, "maximum", c->maximum);
    if (c->outputs != NULL)
      put(clients, "outputs", client_outputs(c->outputs, c->output_count, qvm_protection_scalar_element, qvm_record_offset));
    put(clients, "records", strings(c->records.values, c->records.count));
    put_str(clients, "playerStateRecord", c->player_state_record);
    put(clients, "admit", qvm_calls(c->admit, c->admit_count));
    put(clients, "userinfo", qvm_calls(c->userinfo, c->userinfo_count));
    put(clients, "disconnect", qvm_calls(c->disconnect, c->disconnect_count));
    if (c->frame != NULL) put(clients, "frame", qvm_calls(c->frame, c->frame_count));
    if (c->input != NULL) put(clients, "input", client_input(c->input, c->input_count, qvm_call_element, qvm_input_output));
    put(o, "clients", clients);
  }
  put_str_or_null(o, "entityRecord", d->entity_record);
  if (d->source_actors != NULL) put(o, "sourceActors", qvm_source_actors(d->source_actors));
  if (d->combat != NULL) put(o, "combat", qvm_combat(d->combat));
  if (d->protection != NULL) put(o, "protection", list(d->protection, d->protection_count, sizeof *d->protection, qvm_protection));
  if (d->items != NULL) put(o, "items", qvm_items(d->items));
  if (d->pickups != NULL) put(o, "pickups", list(d->pickups, d->pickup_count, sizeof *d->pickups, qvm_pickup));
  if (d->objectives != NULL)
    put(o, "objectives", objectives(d->objectives, d->objective_count, qvm_objective_scalar, qvm_objective_address, qvm_call_element));
  put(o, "actorRecords", list(d->actor_records, d->actor_record_count, sizeof *d->actor_records, qvm_actor_record));
  put(o, "initialize", qvm_calls(d->initialize, d->initialize_count));
  put(o, "callbacks", list(d->callbacks, d->callback_count, sizeof *d->callbacks, qvm_callback));
  return o;
}

/* ===================== native (native-callbacks.ts) ===================== */
static qa_save_value *native_address(const qa_native_mod_address *a) {
  qa_save_value *o = obj();
  put_num(o, "rva", a->rva);
  put(o, "indirections", numbers(a->indirections.values, a->indirections.count));
  return o;
}
static qa_save_value *native_address_element(const void *e) { return native_address(e); }
static qa_save_value *native_entry(const qa_native_mod_entry *e) {
  qa_save_value *o = obj();
  put_str(o, "kind", e->kind);
  if (strcmp(e->kind, "rva") == 0) put_num(o, "rva", e->rva);
  else put_str(o, "name", e->name);
  return o;
}
static qa_save_value *native_argument(const qa_native_mod_value *v) {
  qa_save_value *o = obj();
  put_str(o, "kind", v->kind);
  const char *k = v->kind;
  if (strcmp(k, "user-command") == 0) return o;
  if (strcmp(k, "client") == 0 || strcmp(k, "userinfo") == 0) {
    put_str(o, "input", v->input);
  } else if (strcmp(k, "actor") == 0) {
    put_str(o, "record", v->record);
    put_str(o, "input", v->input);
  } else if (strcmp(k, "time") == 0) {
    put_str(o, "input", v->time_input);
    put_str(o, "units", v->units);
    put_str(o, "encoding", v->encoding);
  } else if (strcmp(k, "address") == 0) {
    put(o, "value", v->address_value == NULL ? null_value() : native_address(v->address_value));
  } else {
    put(o, "value", callback_value(&v->value));
  }
  return o;
}
static qa_save_value *native_arguments(const qa_native_mod_value *rows, size_t count) {
  qa_save_value *out = arr();
  for (size_t i = 0; i < count; i++) push(out, native_argument(&rows[i]));
  return out;
}
static qa_save_value *native_globals(const qa_native_mod_global_value *rows, size_t count) {
  qa_save_value *out = arr();
  for (size_t i = 0; i < count; i++) {
    qa_save_value *g = obj();
    put(g, "address", native_address(&rows[i].address));
    put(g, "value", native_argument(&rows[i].value));
    push(out, g);
  }
  return out;
}
static void native_call_members(qa_save_value *o, const qa_native_mod_source_call *c) {
  put(o, "entry", native_entry(&c->entry));
  put(o, "arguments", native_arguments(c->arguments, c->argument_count));
  put(o, "globals", native_globals(c->globals, c->global_count));
  put_str(o, "returns", c->returns);
  if (c->skips != NULL) {
    qa_save_value *skips = arr();
    for (size_t i = 0; i < c->skip_count; i++) {
      qa_save_value *v = obj();
      put_num(v, "entry", c->skips[i].entry);
      put_num(v, "join", c->skips[i].join);
      push(skips, v);
    }
    put(o, "skips", skips);
  }
}
static qa_save_value *native_call(const qa_native_mod_source_call *c) {
  qa_save_value *o = obj();
  native_call_members(o, c);
  return o;
}
static qa_save_value *native_call_element(const void *e) { return native_call(e); }
static qa_save_value *native_calls(const qa_native_mod_source_call *rows, size_t count) {
  return list(rows, count, sizeof *rows, native_call_element);
}
static qa_save_value *native_input_output(const void *e) {
  const qa_native_mod_input_output *out = e;
  qa_save_value *o = obj();
  put_str(o, "kind", out->kind);
  if (strcmp(out->kind, "field") == 0) {
    put_str(o, "record", out->record);
    put_num(o, "offset", out->offset);
    return o;
  }
  put(o, "entry", native_entry(&out->entry));
  put(o, "arguments", native_arguments(out->arguments, out->argument_count));
  put(o, "inputs", strings(out->inputs, out->input_count));
  return o;
}
static qa_save_value *native_actor_field(const void *e) {
  const qa_native_mod_actor_field *f = e;
  qa_save_value *o = obj();
  put_num(o, "offset", f->offset);
  put_str(o, "binding", f->binding);
  const char *b = f->binding;
  if (strcmp(b, "address") == 0) {
    put(o, "value", f->address_value == NULL ? null_value() : native_address(f->address_value));
  } else if (strcmp(b, "team") == 0) {
    put_str(o, "encoding", f->encoding);
    put(o, "values", team_values(f->team_values, f->team_value_count));
  } else if (strcmp(b, "score") == 0 || strcmp(b, "health") == 0) {
    put_str(o, "encoding", f->encoding);
  } else if (strcmp(b, "inventory") == 0 || strcmp(b, "inventory-capacity") == 0) {
    put_str(o, "encoding", f->encoding);
    put_str(o, "item", f->item);
  } else if (strcmp(b, "record") == 0) {
    put_str(o, "record", f->qvm_field.record);
  } else if (strcmp(b, "constant") == 0) {
    put_str(o, "encoding", f->encoding);
    put_num(o, "value", f->constant_value);
  } else if (strcmp(b, "constant-vector") == 0) {
    put(o, "value", vector(f->qvm_field.constant_vector));
  } else if (strcmp(b, "private") == 0) {
    put_num(o, "byteLength", f->qvm_field.byte_length);
  }
  return o;
}
static qa_save_value *native_armor_field(const qa_native_mod_armor_field *f) {
  qa_save_value *o = obj();
  put_str(o, "record", f->record);
  put_num(o, "offset", f->field.offset);
  put_str(o, "encoding", f->field.encoding);
  return o;
}
static qa_save_value *native_armor_field_element(const void *e) { return native_armor_field(e); }
static qa_save_value *native_record_offset(const void *e) {
  const qa_native_mod_record_offset *r = e;
  qa_save_value *o = obj();
  put_str(o, "record", r->record);
  put_num(o, "offset", r->offset);
  return o;
}
static qa_save_value *native_scalar(const qa_native_mod_scalar_field *s) {
  qa_save_value *o = obj();
  put_num(o, "offset", s->offset);
  put_str(o, "encoding", s->encoding);
  return o;
}
static qa_save_value *native_armor_selection(const qa_native_mod_armor_selection *s) {
  qa_save_value *o = obj();
  put_str(o, "kind", s->is_enum ? "enum" : "positive");
  put(o, "field", native_armor_field(&s->field));
  if (s->is_enum) {
    put_num(o, "value", s->value);
    put_num(o, "none", s->none);
  }
  return o;
}
static qa_save_value *native_regular_armor(const void *e) {
  const qa_native_mod_regular_armor_item *r = e;
  qa_save_value *o = obj();
  put_str_or_null(o, "item", r->item);
  put(o, "selection", native_armor_selection(&r->selection));
  put(o, "points", native_armor_field(&r->points));
  return o;
}
static qa_save_value *native_power_armor(const void *e) {
  const qa_native_mod_power_armor_item *p = e;
  qa_save_value *o = obj();
  put_str(o, "item", p->item);
  put_str(o, "kind", p->kind);
  put(o, "selection", native_armor_selection(&p->selection));
  put(o, "cells", native_armor_field(&p->cells));
  if (p->enabled == NULL) {
    put(o, "enabled", null_value());
  } else {
    qa_save_value *enabled = obj();
    put(enabled, "field", native_armor_field(&p->enabled->field));
    put_num(enabled, "mask", p->enabled->mask);
    put(o, "enabled", enabled);
  }
  return o;
}
static qa_save_value *native_armor(const qa_native_mod_armor *a) {
  qa_save_value *o = obj();
  put_str(o, "kind", a->kind);
  if (strcmp(a->kind, "none") == 0) return o;
  if (strcmp(a->kind, "source") == 0) {
    put(o, "regular", list(a->regular, a->regular_count, sizeof *a->regular, native_regular_armor));
  } else {
    qa_save_value *regular = arr();
    for (size_t i = 0; i < a->q2_regular_count; i++) {
      const qa_native_mod_q2_regular_armor_item *r = &a->q2_regular[i];
      qa_save_value *v = obj();
      put_str(v, "item", r->item);
      put(v, "selection", native_armor_selection(&r->selection));
      put(v, "points", native_armor_field(&r->points));
      put_num(v, "normalProtection", r->normal_protection);
      put_num(v, "energyProtection", r->energy_protection);
      push(regular, v);
    }
    put(o, "regular", regular);
  }
  put(o, "power", list(a->power, a->power_count, sizeof *a->power, native_power_armor));
  return o;
}
static qa_save_value *native_region_location(const qa_native_mod_region_location *l) {
  qa_save_value *o = obj();
  switch (l->kind) {
    case QA_NATIVE_MOD_LOCATION_SIMD:
      put_str(o, "kind", "simd");
      put_num(o, "index", l->index);
      put_num(o, "offset", l->offset);
      break;
    case QA_NATIVE_MOD_LOCATION_STACK:
      put_str(o, "kind", "stack");
      put_num(o, "offset", l->offset);
      break;
    case QA_NATIVE_MOD_LOCATION_REGISTER:
      put_str(o, "kind", "register");
      put_str(o, "register", l->reg);
      break;
  }
  put_str(o, "storage", l->storage);
  return o;
}
static qa_save_value *native_protection(const void *e) {
  const qa_native_mod_protection_definition *p = e;
  qa_save_value *o = obj();
  put_str(o, "id", p->id);
  if (p->admission.kind != NULL) put(o, "admission", admission(&p->admission));
  put_str(o, "channel", p->channel);
  bool regular = strcmp(p->channel, "regular") == 0;
  if (regular) put(o, "storage", list(p->regular_storage, p->regular_storage_count, sizeof *p->regular_storage, native_regular_armor));
  else put(o, "storage", list(p->power_storage, p->power_storage_count, sizeof *p->power_storage, native_power_armor));
  qa_save_value *absorb = obj();
  const char *abi = p->absorb_abi;
  if (strcmp(abi, "source-call") == 0) {
    put_str(absorb, "abi", abi);
    put(absorb, "call", native_call(&p->absorb_call));
  } else if (strcmp(abi, "source-region") == 0) {
    const qa_native_mod_protection_region *r = p->absorb_region;
    put_str(absorb, "abi", abi);
    put(absorb, "call", native_call(&r->call));
    qa_save_value *frame = obj();
    put_num(frame, "entry", r->frame.entry);
    put_num(frame, "exit", r->frame.exit);
    put_num(frame, "stackBytes", r->frame.stack_bytes);
    put_num(frame, "argumentBytes", r->frame.argument_bytes);
    put(absorb, "frame", frame);
    put_num(absorb, "entry", r->entry);
    put_num(absorb, "join", r->join);
    put(absorb, "result", native_region_location(&r->result));
    qa_save_value *inputs = arr();
    for (size_t i = 0; i < r->input_count; i++) {
      qa_save_value *v = obj();
      put(v, "target", native_region_location(&r->inputs[i].target));
      put(v, "value", native_argument(&r->inputs[i].value));
      push(inputs, v);
    }
    put(absorb, "inputs", inputs);
  } else {
    put(absorb, "entry", native_entry(&p->absorb_q2.entry));
    put_str(absorb, "flags", p->absorb_q2.flags);
    if (p->absorb_q2.globals != NULL) put(absorb, "globals", native_globals(p->absorb_q2.globals, p->absorb_q2.global_count));
    put_str(absorb, "abi", abi);
    if (regular) put_num(absorb, "sparks", p->absorb_sparks);
  }
  put(o, "absorb", absorb);
  return o;
}
static qa_save_value *native_source_actors(const qa_native_mod_source_actors *a) {
  qa_save_value *o = obj();
  put(o, "allocate", native_entry(&a->allocate));
  put(o, "release", native_entry(&a->release));
  qa_save_value *update = obj();
  put(update, "entry", native_entry(&a->update.entry));
  put_str(update, "returns", a->update.returns);
  put(o, "update", update);
  put_num(o, "frameSeconds", a->frame_seconds);
  qa_save_value *clock = arr();
  for (size_t i = 0; i < a->clock_count; i++) {
    qa_save_value *v = obj();
    put(v, "address", native_address(&a->clock[i].address));
    put_str(v, "input", a->clock[i].input);
    put_str(v, "encoding", a->clock[i].encoding);
    put_str(v, "units", a->clock[i].units);
    push(clock, v);
  }
  put(o, "clock", clock);
  if (a->callbacks != NULL) {
    const qa_native_mod_source_callbacks *c = a->callbacks;
    qa_save_value *callbacks = obj();
    put_str(callbacks, "abi", c->abi);
    put(callbacks, "touch", c->has_touch ? num(c->touch) : null_value());
    put(callbacks, "pain", c->has_pain ? num(c->pain) : null_value());
    put(callbacks, "die", c->has_die ? num(c->die) : null_value());
    put(o, "callbacks", callbacks);
  }
  if (a->combat != NULL) {
    const qa_native_mod_combat *c = a->combat;
    qa_save_value *combat = obj();
    qa_save_value *damage = obj();
    put(damage, "entry", native_entry(&c->damage.entry));
    put_str(damage, "abi", c->damage.abi);
    put(combat, "damage", damage);
    qa_save_value *causes = obj();
    put_str(causes, "edition", c->causes.edition);
    if (strcmp(c->causes.edition, "classic") == 0) put_str(causes, "game", c->causes.game);
    put(combat, "causes", causes);
    put(combat, "health", native_scalar(&c->health));
    put(combat, "mass", native_scalar(&c->mass));
    put(combat, "takedamage", native_scalar(&c->takedamage));
    qa_save_value *flags = native_scalar(&c->flags.field);
    put_num(flags, "invulnerable", c->flags.invulnerable);
    put_num(flags, "noKnockback", c->flags.no_knockback);
    put(combat, "flags", flags);
    put(combat, "armor", native_armor(&c->armor));
    if (c->deferred != NULL) {
      const qa_native_mod_deferred_damage *d = c->deferred;
      qa_save_value *deferred = obj();
      put(deferred, "process", native_entry(&d->process));
      put_num(deferred, "attacker", d->attacker);
      put_num(deferred, "inflictor", d->inflictor);
      put(deferred, "blood", native_scalar(&d->blood));
      put(deferred, "knockback", native_scalar(&d->knockback));
      put_num(deferred, "point", d->point);
      put_num(deferred, "mod", d->mod);
      put_num(deferred, "receipt", d->receipt);
      put(combat, "deferred", deferred);
    }
    put(o, "combat", combat);
  }
  qa_save_value *fields = obj();
  put_num(fields, "velocity", a->fields.velocity);
  put_num(fields, "ground", a->fields.ground);
  put(fields, "use", a->fields.has_use ? num(a->fields.use) : null_value());
  put_num(fields, "think", a->fields.think);
  qa_save_value *nextthink = obj();
  put_num(nextthink, "offset", a->fields.nextthink.offset);
  put_str(nextthink, "encoding", a->fields.nextthink.encoding);
  put_str(nextthink, "units", a->fields.nextthink.units);
  put(fields, "nextthink", nextthink);
  put(o, "fields", fields);
  return o;
}
static qa_save_value *native_item_pointer(const qa_native_item_pointer *p) {
  qa_save_value *o = obj();
  put_str(o, "record", p->record);
  put_num(o, "offset", p->offset);
  return o;
}
static qa_save_value *native_item_test(const qa_native_item_test *t) {
  qa_save_value *o = obj();
  put_str(o, "kind", t->kind);
  if (strcmp(t->kind, "pointer") == 0) {
    put(o, "field", native_item_pointer(&t->pointer_field));
    put(o, "value", t->pointer_value == NULL ? null_value() : native_address(t->pointer_value));
    return o;
  }
  put(o, "field", native_armor_field(&t->field));
  put(o, "mask", t->has_mask ? num(t->mask) : null_value());
  put_str(o, "comparison", t->comparison);
  put_num(o, "value", t->scalar_value);
  return o;
}
static qa_save_value *native_test_rows(const qa_native_item_test_row *rows, size_t count) {
  qa_save_value *out = arr();
  for (size_t i = 0; i < count; i++) {
    qa_save_value *row = arr();
    for (size_t j = 0; j < rows[i].count; j++) push(row, native_item_test(&rows[i].tests[j]));
    push(out, row);
  }
  return out;
}
static qa_save_value *native_items(const qa_native_mod_items *items) {
  qa_save_value *o = obj();
  qa_save_value *definitions = arr();
  for (size_t i = 0; i < items->definition_count; i++) {
    const qa_native_mod_item_definition *d = &items->definitions[i];
    push(definitions, item_definition(d->item, d->label, d->has_icon, d->icon, d->admission, d->has_actions,
                                      d->action_use, d->action_drop, d->is_weapon, &d->weapon, native_call_element));
  }
  put(o, "definitions", definitions);
  qa_save_value *storage = arr();
  for (size_t i = 0; i < items->storage_count; i++) {
    const qa_native_item_storage *s = &items->storage[i];
    qa_save_value *v = obj();
    if (strcmp(s->kind, "bits") == 0) {
      put_str(v, "kind", "bits");
      put(v, "field", native_armor_field(&s->field));
      put_num(v, "privateMask", s->private_mask);
      qa_save_value *masks = arr();
      for (size_t j = 0; j < s->item_count; j++) {
        qa_save_value *m = obj();
        put_str(m, "item", s->items[j].item);
        put_num(m, "mask", s->items[j].mask);
        push(masks, m);
      }
      put(v, "items", masks);
    } else {
      put_str(v, "kind", "counter");
      put(v, "field", native_armor_field(&s->field));
      put_str(v, "item", s->item);
      const qa_native_item_capacity *c = &s->capacity;
      qa_save_value *capacity = obj();
      put_str(capacity, "kind", c->kind);
      if (strcmp(c->kind, "constant") == 0) {
        put_num(capacity, "value", c->value);
      } else if (strcmp(c->kind, "field") == 0) {
        put(capacity, "field", native_armor_field(&c->field));
      } else {
        put(capacity, "address", native_address(&c->address));
        put_str(capacity, "encoding", c->encoding);
      }
      put(v, "capacity", capacity);
    }
    push(storage, v);
  }
  put(o, "storage", storage);
  if (items->weapons != NULL) {
    const qa_native_weapon_stage *w = items->weapons;
    qa_save_value *weapons = obj();
    qa_save_value *dispatcher = obj();
    put(dispatcher, "entry", native_entry(&w->dispatcher.entry));
    put_str(dispatcher, "record", w->dispatcher.record);
    put_num(dispatcher, "argument", w->dispatcher.argument);
    put_num(dispatcher, "arguments", w->dispatcher.arguments);
    put(weapons, "dispatcher", dispatcher);
    qa_save_value *decisions = arr();
    for (size_t i = 0; i < w->decision_count; i++) {
      const qa_native_weapon_decision *d = &w->decisions[i];
      qa_save_value *v = obj();
      put_num(v, "entry", d->entry);
      put_num(v, "join", d->join);
      qa_save_value *fields = arr();
      for (size_t j = 0; j < d->field_count; j++) {
        qa_save_value *f = obj();
        put(f, "field", native_armor_field(&d->fields[j].field));
        put_num(f, "clearMask", d->fields[j].clear_mask);
        push(fields, f);
      }
      put(v, "fields", fields);
      push(decisions, v);
    }
    put(weapons, "decisions", decisions);
    if (w->committed_input != NULL) put(weapons, "committedInput", native_test_rows(w->committed_input, w->committed_input_row_count));
    put(weapons, "continuations", native_test_rows(w->continuations, w->continuation_row_count));
    put(weapons, "settled", native_test_rows(w->settled, w->settled_row_count));
    qa_save_value *selection = obj();
    put(selection, "active", native_item_pointer(&w->selection.active));
    put(selection, "pending", w->selection.pending == NULL ? null_value() : native_item_pointer(w->selection.pending));
    qa_save_value *values = arr();
    for (size_t i = 0; i < w->selection.value_count; i++) {
      const qa_native_weapon_selection_value *s = &w->selection.values[i];
      qa_save_value *v = obj();
      put_str(v, "item", s->item);
      put(v, "address", native_address(&s->address));
      put(v, "request", native_call(&s->request));
      push(values, v);
    }
    put(selection, "values", values);
    put(weapons, "selection", selection);
    put(o, "weapons", weapons);
  }
  return o;
}
static qa_save_value *native_pickup(const void *e) {
  const qa_native_mod_pickup *p = e;
  qa_save_value *o = pickup_rule(&p->rule, native_call_element);
  qa_save_value *context = arr();
  for (size_t i = 0; i < p->context_count; i++) {
    qa_save_value *v = obj();
    put_str(v, "record", p->context[i].record);
    put_num(v, "offset", p->context[i].offset);
    put(v, "value", native_argument(&p->context[i].value));
    push(context, v);
  }
  put(o, "context", context);
  return o;
}
static qa_save_value *native_objective_scalar(const void *e) {
  const qa_native_mod_objective_scalar *s = e;
  qa_save_value *o = obj();
  put(o, "address", native_address(&s->address));
  put_str(o, "encoding", s->encoding);
  return o;
}
static qa_save_value *native_actor_record(const void *e) {
  const qa_native_mod_actor_record *r = e;
  qa_save_value *o = obj();
  put_str(o, "id", r->id);
  qa_save_value *base = obj();
  put_str(base, "kind", r->base.kind);
  if (strcmp(r->base.kind, "address") == 0) {
    put_num(base, "rva", r->base.address.rva);
    put(base, "indirections", numbers(r->base.address.indirections.values, r->base.address.indirections.count));
  }
  put(o, "base", base);
  put_num(o, "stride", r->stride);
  put_num(o, "firstSlot", r->first_slot);
  put_num(o, "capacity", r->capacity);
  put(o, "fields", list(r->fields, r->field_count, sizeof *r->fields, native_actor_field));
  return o;
}
static qa_save_value *native_callback(const void *e) {
  const qa_native_mod_callback *c = e;
  qa_save_value *o = obj();
  binding_members(o, &c->binding);
  native_call_members(o, &c->call);
  return o;
}
static qa_save_value *native_clients(const qa_native_mod_clients *c) {
  qa_save_value *o = obj();
  put_num(o, "maximum", c->maximum);
  put(o, "records", strings(c->records.values, c->records.count));
  qa_save_value *admit = arr();
  for (size_t i = 0; i < c->admit_count; i++) {
    qa_save_value *v = native_call(&c->admit[i].call);
    put_str(v, "accepts", c->admit[i].accepts);
    push(admit, v);
  }
  put(o, "admit", admit);
  put(o, "userinfo", native_calls(c->userinfo, c->userinfo_count));
  put(o, "disconnect", native_calls(c->disconnect, c->disconnect_count));
  put(o, "command", native_calls(c->command, c->command_count));
  if (c->input != NULL) put(o, "input", client_input(c->input, c->input_count, native_call_element, native_input_output));
  if (c->frame != NULL) put(o, "frame", native_calls(c->frame, c->frame_count));
  if (c->end_frame != NULL) put(o, "endFrame", native_calls(c->end_frame, c->end_frame_count));
  if (c->outputs != NULL)
    put(o, "outputs", client_outputs(c->outputs, c->output_count, native_armor_field_element, native_record_offset));
  if (c->pose != NULL) {
    qa_save_value *pose = obj();
    put(pose, "viewHeight", native_armor_field(&c->pose->view_height));
    qa_save_value *crouched = obj();
    put(crouched, "field", native_armor_field(&c->pose->crouched.field));
    put_num(crouched, "mask", c->pose->crouched.mask);
    put(pose, "crouched", crouched);
    put(o, "pose", pose);
  }
  if (c->input_fields != NULL) {
    qa_save_value *fields = arr();
    for (size_t i = 0; i < c->input_field_count; i++) {
      qa_save_value *v = obj();
      put_str(v, "record", c->input_fields[i].record);
      put_num(v, "offset", c->input_fields[i].offset);
      put(v, "value", native_argument(&c->input_fields[i].value));
      push(fields, v);
    }
    put(o, "inputFields", fields);
  }
  return o;
}

qa_save_value *qa_native_mod_declaration_value(const qa_native_mod_declaration *d) {
  qa_save_value *o = obj();
  put_num(o, "version", (double)d->version);
  put_str(o, "runtime", d->runtime);
  if (d->items != NULL) put(o, "items", native_items(d->items));
  put(o, "program", program(d->program));
  qa_save_value *target = obj();
  qa_save_value *api = obj();
  put_str(api, "kind", d->target.api.kind == QA_Q2_GAME_API_CLASSIC ? "q2-classic-game" : "q2-rerelease-game");
  put_num(api, "version", (double)d->target.api.version);
  put(target, "api", api);
  qa_save_value *abi = obj();
  put_str(abi, "kind", d->target.abi.kind);
  put_str(abi, "image", d->target.abi.image);
  put_num(abi, "pointerBytes", (double)d->target.abi.pointer_bytes);
  put_str(abi, "call", d->target.abi.call);
  put(target, "abi", abi);
  put(o, "target", target);
  if (d->client_presentation != NULL) {
    qa_save_value *presentation = obj();
    put_str(presentation, "hud", d->client_presentation->hud);
    put_str(presentation, "view", d->client_presentation->view);
    put(o, "clientPresentation", presentation);
  }
  if (d->source_actors != NULL) put(o, "sourceActors", native_source_actors(d->source_actors));
  if (d->protection != NULL) put(o, "protection", list(d->protection, d->protection_count, sizeof *d->protection, native_protection));
  if (d->pickups != NULL) put(o, "pickups", list(d->pickups, d->pickup_count, sizeof *d->pickups, native_pickup));
  if (d->clients != NULL) put(o, "clients", native_clients(d->clients));
  put(o, "cvars", named_strings(d->cvars, d->cvar_count));
  put_str_or_null(o, "spawnEntities", d->spawn_entities);
  put_str_or_null(o, "entityRecord", d->entity_record);
  if (d->objectives != NULL)
    put(o, "objectives", objectives(d->objectives, d->objective_count, native_objective_scalar, native_address_element, native_call_element));
  put(o, "actorRecords", list(d->actor_records, d->actor_record_count, sizeof *d->actor_records, native_actor_record));
  put(o, "initialize", native_calls(d->initialize, d->initialize_count));
  put(o, "project", native_calls(d->project, d->project_count));
  put(o, "release", native_calls(d->release, d->release_count));
  put(o, "callbacks", list(d->callbacks, d->callback_count, sizeof *d->callbacks, native_callback));
  return o;
}

qa_save_value *qa_gameplay_mod_declaration_value(qa_mod_declaration declaration) {
  switch (declaration.runtime) {
    case QA_MOD_RUNTIME_QUAKEC: return qa_quakec_mod_declaration_value(declaration.as.quakec);
    case QA_MOD_RUNTIME_QVM: return qa_qvm_mod_declaration_value(declaration.as.qvm);
    case QA_MOD_RUNTIME_NATIVE: return qa_native_mod_declaration_value(declaration.as.native);
  }
  qa_range_error("Unknown gameplay mod runtime");
}

/* ===================== JSON.stringify ===================== */
typedef struct json_text { char *data; size_t length, capacity; } json_text;
static void text_add(json_text *t, const char *bytes, size_t length) {
  if (t->length + length + 1 > t->capacity) {
    size_t capacity = t->capacity == 0 ? 256 : t->capacity;
    while (capacity < t->length + length + 1) capacity *= 2;
    char *grown = realloc(t->data, capacity);
    if (grown == NULL) qa_range_error("Out of memory");
    t->data = grown;
    t->capacity = capacity;
  }
  memcpy(t->data + t->length, bytes, length);
  t->length += length;
  t->data[t->length] = '\0';
}
static void text_cstr(json_text *t, const char *s) { text_add(t, s, strlen(s)); }
/* JSON.stringify's QuoteJSONString over the UTF-16 string the C bytes encode:
 * C keeps lone surrogates as WTF-8 (ED A0..BF xx), which TS escapes. */
static void quote(json_text *t, const char *text) {
  static const char hex[] = "0123456789abcdef";
  text_add(t, "\"", 1);
  const unsigned char *p = (const unsigned char *)text;
  while (*p != '\0') {
    unsigned char c = *p;
    char escape[7];
    switch (c) {
      case '"': text_cstr(t, "\\\""); p++; continue;
      case '\\': text_cstr(t, "\\\\"); p++; continue;
      case '\b': text_cstr(t, "\\b"); p++; continue;
      case '\f': text_cstr(t, "\\f"); p++; continue;
      case '\n': text_cstr(t, "\\n"); p++; continue;
      case '\r': text_cstr(t, "\\r"); p++; continue;
      case '\t': text_cstr(t, "\\t"); p++; continue;
      default: break;
    }
    if (c < 0x20) {
      escape[0] = '\\'; escape[1] = 'u'; escape[2] = '0'; escape[3] = '0';
      escape[4] = hex[c >> 4]; escape[5] = hex[c & 15]; escape[6] = '\0';
      text_cstr(t, escape);
      p++;
      continue;
    }
    if (c == 0xed && p[1] >= 0xa0 && p[1] <= 0xbf && p[2] >= 0x80 && p[2] <= 0xbf) {
      unsigned unit = 0xd000u | ((unsigned)(p[1] & 0x3f) << 6) | (unsigned)(p[2] & 0x3f);
      escape[0] = '\\'; escape[1] = 'u';
      escape[2] = hex[(unit >> 12) & 15]; escape[3] = hex[(unit >> 8) & 15];
      escape[4] = hex[(unit >> 4) & 15]; escape[5] = hex[unit & 15]; escape[6] = '\0';
      text_cstr(t, escape);
      p += 3;
      continue;
    }
    text_add(t, (const char *)p, 1);
    p++;
  }
  text_add(t, "\"", 1);
}
static void stringify(json_text *t, const qa_save_value *v) {
  char buffer[64];
  switch (v->kind) {
    case QA_SAVE_NULL: text_cstr(t, "null"); return;
    case QA_SAVE_BOOLEAN: text_cstr(t, v->boolean ? "true" : "false"); return;
    case QA_SAVE_NUMBER:
      qa_net_json_number(v->number, buffer, sizeof buffer);
      text_cstr(t, buffer);
      return;
    case QA_SAVE_STRING: quote(t, v->text); return;
    case QA_SAVE_ARRAY:
      text_add(t, "[", 1);
      for (size_t i = 0; i < v->item_count; i++) {
        if (i > 0) text_add(t, ",", 1);
        /* JSON.stringify writes undefined array slots as null. */
        if (v->items[i]->kind == QA_SAVE_UNDEFINED) text_cstr(t, "null");
        else stringify(t, v->items[i]);
      }
      text_add(t, "]", 1);
      return;
    case QA_SAVE_OBJECT: {
      text_add(t, "{", 1);
      bool first = true;
      for (size_t i = 0; i < v->entry_count; i++) {
        /* JSON.stringify drops undefined members. */
        if (v->entries[i].value->kind == QA_SAVE_UNDEFINED) continue;
        if (!first) text_add(t, ",", 1);
        first = false;
        quote(t, v->entries[i].key);
        text_add(t, ":", 1);
        stringify(t, v->entries[i].value);
      }
      text_add(t, "}", 1);
      return;
    }
    case QA_SAVE_UNDEFINED:
    case QA_SAVE_BIGINT:
    case QA_SAVE_BYTES:
      break;
  }
  /* JSON.stringify throws on bigint; byte arrays are not declaration data. */
  qa_range_error("Do not know how to serialize a BigInt");
}
char *qa_json_stringify_value(const qa_save_value *value, size_t *out_length) {
  json_text t = {NULL, 0, 0};
  stringify(&t, value);
  if (out_length != NULL) *out_length = t.length;
  return t.data;
}
char *qa_gameplay_mod_declaration_json(qa_mod_declaration declaration) {
  qa_save_value *value = qa_gameplay_mod_declaration_value(declaration);
  char *text = qa_json_stringify_value(value, NULL);
  qa_save_value_free(value);
  return text;
}
