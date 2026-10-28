/* Port of src/app/bootstrap/simulation/weapon-slot.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/weapon_slot.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#define MAX_BINDINGS 16
typedef struct binding {
  bool used; int handle;
  qa_source_weapon_handoff handoff;
  bool (*current)(void *ctx); void *current_ctx;
  qa_source_weapon_presentation (*read)(void *ctx); void *read_ctx; bool has_read;
} binding;
struct qa_app_sim_weapon_slot {
  qa_source_weapon_handoff primary; bool has_primary;
  qa_app_sim_equipment_weapon_handoff equipment; bool has_equipment;
  qa_source_weapon_handoff equipment_handoff;
  qa_app_sim_weapon_slot_state current; bool restore_pending;
  bool has_activation; qa_app_sim_weapon_slot_state activation_state;
  qa_source_weapon_request activation_request; bool activation_cancelling;
  binding bindings[MAX_BINDINGS]; int next_handle;
  bool (*actor_current)(void *ctx); void *actor_current_ctx;
  qa_source_weapon_presentation presentations[MAX_BINDINGS]; size_t presentation_count;
};
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static bool actor_current(qa_app_sim_weapon_slot *s) { return s->actor_current == NULL || s->actor_current(s->actor_current_ctx); }
static binding *find_binding(qa_app_sim_weapon_slot *s, qa_provider_id provider) {
  for (int i = 0; i < MAX_BINDINGS; i++)
    if (s->bindings[i].used && is(s->bindings[i].handoff.provider, provider)) {
      binding *b = &s->bindings[i];
      return (b->current == NULL || b->current(b->current_ctx)) ? b : NULL;
    }
  return NULL;
}
static void add_binding(qa_app_sim_weapon_slot *s, qa_source_weapon_handoff h, bool (*cur)(void *), void *cctx,
                        qa_source_weapon_presentation (*read)(void *), void *rctx, bool has_read) {
  for (int i = 0; i < MAX_BINDINGS; i++) if (!s->bindings[i].used) {
    s->bindings[i] = (binding){true, s->next_handle++, h, cur, cctx, read, rctx, has_read};
    return;
  }
  qa_range_error("Out of memory");
}
static bool equip_accepts(void *ctx, qa_item_id item) { qa_app_sim_weapon_slot *s = (qa_app_sim_weapon_slot *)ctx; return is(item, s->equipment.weapon.item); }
static bool equip_select(void *ctx, qa_item_id item) { return equip_accepts(ctx, item); }
static void equip_holster(void *ctx) { qa_app_sim_weapon_slot *s = (qa_app_sim_weapon_slot *)ctx; if (s->equipment.holster) s->equipment.holster(s->equipment.ctx); }
static bool equip_holstered(void *ctx) { qa_app_sim_weapon_slot *s = (qa_app_sim_weapon_slot *)ctx; return s->equipment.is_holstered && s->equipment.is_holstered(s->equipment.ctx); }
static bool equip_resume(void *ctx, qa_item_id item) { (void)item; qa_app_sim_weapon_slot *s = (qa_app_sim_weapon_slot *)ctx; if (s->equipment.resume) s->equipment.resume(s->equipment.ctx); return true; }

static qa_app_sim_weapon_slot_state restore_state(qa_app_sim_weapon_slot *s, qa_app_sim_weapon_slot_restore_state state) {
  if (is(state.kind, "active") || is(state.kind, "switching") || is(state.kind, "activating")) return state;
  if (is(state.kind, "primary")) { qa_app_sim_weapon_slot_state o = state; o.kind = "active"; o.provider = s->primary.provider; return o; }
  if (is(state.kind, "equipment")) {
    if (!s->has_equipment) qa_range_error("Saved equipment weapon has no owner");
    qa_app_sim_weapon_slot_state o = state; o.kind = "active"; o.provider = s->equipment.weapon.provider; return o;
  }
  if (is(state.kind, "holstering-primary")) { qa_app_sim_weapon_slot_state o = state; o.kind = "switching"; o.from = s->primary.provider; return o; }
  if (!s->has_equipment) qa_range_error("Saved equipment transition has no owner");
  qa_app_sim_weapon_slot_state o = state; o.kind = "switching"; o.from = s->equipment.weapon.provider; return o;
}
qa_app_sim_weapon_slot *qa_app_sim_weapon_slot_create(qa_source_weapon_handoff primary,
    const qa_app_sim_equipment_weapon_handoff *equipment,
    const qa_app_sim_weapon_slot_restore_state *restored,
    bool (*actor_current_fn)(void *ctx), void *actor_current_ctx) {
  qa_app_sim_weapon_slot *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->primary = primary; s->has_primary = true;
  s->actor_current = actor_current_fn; s->actor_current_ctx = actor_current_ctx;
  add_binding(s, primary, actor_current_fn, actor_current_ctx, NULL, NULL, false);
  if (equipment != NULL) {
    if (is(equipment->weapon.provider, primary.provider)) qa_range_error("Equipment and primary weapon owners overlap");
    s->equipment = *equipment; s->has_equipment = true;
    s->equipment_handoff = (qa_source_weapon_handoff){0};
    s->equipment_handoff.ctx = s;
    s->equipment_handoff.provider = equipment->weapon.provider;
    s->equipment_handoff.accepts = equip_accepts; s->equipment_handoff.select = equip_select;
    s->equipment_handoff.holster = equip_holster; s->equipment_handoff.is_holstered = equip_holstered;
    s->equipment_handoff.is_source_input = false; s->equipment_handoff.resume_immediate = equip_resume;
    add_binding(s, s->equipment_handoff, actor_current_fn, actor_current_ctx, NULL, NULL, false);
  }
  qa_app_sim_weapon_slot_restore_state initial;
  memset(&initial, 0, sizeof initial); initial.kind = "active"; initial.provider = primary.provider;
  s->current = restore_state(s, restored != NULL ? *restored : initial);
  s->restore_pending = restored != NULL;
  return s;
}
void qa_app_sim_weapon_slot_destroy(qa_app_sim_weapon_slot *slot) { free(slot); }
qa_app_sim_weapon_slot_state qa_app_sim_weapon_slot_snapshot(const qa_app_sim_weapon_slot *slot) { return slot->current; }
void qa_app_sim_weapon_slot_validate_restore(qa_app_sim_weapon_slot *slot) {
  if (!slot->restore_pending) return;
  qa_app_sim_weapon_slot_state state = slot->current;
  binding *outgoing = find_binding(slot, is(state.kind, "active") ? state.provider : state.from);
  if (!actor_current(slot) || outgoing == NULL) qa_range_error("Saved weapon source is not bound");
  if (outgoing->has_read) {
    qa_source_weapon_presentation p = outgoing->read(outgoing->read_ctx);
    bool declared = p.active == NULL;
    for (size_t i = 0; !declared && i < p.item_count; i++)
      if (p.items[i].is_weapon && is(p.items[i].item, p.active)) declared = true;
    if (!is(p.source.provider, outgoing->handoff.provider) || !declared || find_binding(slot, outgoing->handoff.provider) != outgoing)
      qa_range_error("Saved active weapon is not declared by its source owner");
  }
  if (!is(state.kind, "active")) {
    binding *incoming = find_binding(slot, state.next.provider);
    if (incoming == NULL || !incoming->handoff.accepts(incoming->handoff.ctx, state.next.item) || find_binding(slot, state.next.provider) != incoming)
      qa_range_error("Saved pending weapon is not admitted to its source owner");
    if (is(state.kind, "activating")) {
      if (!incoming->handoff.is_source_input) qa_range_error("Saved activation requires its original input owner");
      qa_source_weapon_request request = incoming->handoff.restore_request(incoming->handoff.ctx, state.request, state.next.item);
      if (request.id != state.request || find_binding(slot, state.next.provider) != incoming)
        qa_range_error("Saved weapon activation changed during restoration");
      slot->has_activation = true; slot->activation_state = state; slot->activation_request = request; slot->activation_cancelling = false;
    }
  }
  slot->restore_pending = false;
}
bool qa_app_sim_weapon_slot_selected(qa_app_sim_weapon_slot *slot, qa_provider_id provider) {
  return actor_current(slot) && is(slot->current.kind, "active") && is(slot->current.provider, provider) && find_binding(slot, provider) != NULL;
}
bool qa_app_sim_weapon_slot_presented(qa_app_sim_weapon_slot *slot, qa_provider_id provider) {
  qa_provider_id shown = is(slot->current.kind, "active") ? slot->current.provider : slot->current.from;
  return actor_current(slot) && is(shown, provider) && find_binding(slot, provider) != NULL;
}
bool qa_app_sim_weapon_slot_primary_selected(qa_app_sim_weapon_slot *slot) { return qa_app_sim_weapon_slot_selected(slot, slot->primary.provider); }
bool qa_app_sim_weapon_slot_equipment_selected(qa_app_sim_weapon_slot *slot) {
  return slot->has_equipment && qa_app_sim_weapon_slot_selected(slot, slot->equipment.weapon.provider);
}
static void cancel_activation(qa_app_sim_weapon_slot *s, qa_app_sim_weapon_slot_state state) {
  if (!is(state.kind, "activating") || !s->has_activation || s->activation_cancelling) return;
  if (memcmp(&s->activation_state, &state, sizeof state) != 0) return;
  s->activation_cancelling = true;
  if (s->activation_request.cancel) s->activation_request.cancel(s->activation_request.ctx);
  s->has_activation = false;
}
static void resume_slot(qa_app_sim_weapon_slot *s, qa_provider_id provider, qa_item_id item, qa_provider_id out_provider, binding *out_binding);
static void fallback_slot(qa_app_sim_weapon_slot *s) { resume_slot(s, s->primary.provider, NULL, NULL, NULL); }
static void resume_slot(qa_app_sim_weapon_slot *s, qa_provider_id provider, qa_item_id item, qa_provider_id out_provider, binding *out_binding) {
  binding *b = find_binding(s, provider);
  if (b == NULL) qa_range_error("Weapon selection has no live source owner");
  qa_app_sim_weapon_slot_state activated; memset(&activated, 0, sizeof activated);
  activated.kind = "active"; activated.provider = provider; s->current = activated;
  bool accepted;
  if (!b->handoff.is_source_input) {
    accepted = b->handoff.resume_immediate(b->handoff.ctx, item);
  } else {
    qa_source_weapon_request request = b->handoff.resume_request(b->handoff.ctx, item);
    if (!actor_current(s) || memcmp(&s->current, &activated, sizeof activated) != 0 || find_binding(s, provider) != b) { if (request.cancel) request.cancel(request.ctx); return; }
    const char *status = request.status(request.ctx);
    if (!actor_current(s) || memcmp(&s->current, &activated, sizeof activated) != 0 || find_binding(s, provider) != b) { if (request.cancel) request.cancel(request.ctx); return; }
    if (is(status, "pending")) {
      if (item == NULL || out_binding == NULL || find_binding(s, out_provider) != out_binding) { if (request.cancel) request.cancel(request.ctx); qa_range_error("Deferred source resumption has no unchanged outgoing owner"); }
      qa_app_sim_weapon_slot_state st; memset(&st, 0, sizeof st);
      st.kind = "activating"; st.from = out_provider; st.next = (qa_weapon_reference){provider, item}; st.request = request.id;
      s->current = st; s->has_activation = true; s->activation_state = st; s->activation_request = request; s->activation_cancelling = false;
      return;
    }
    accepted = is(status, "accepted");
  }
  if (accepted || !actor_current(s) || memcmp(&s->current, &activated, sizeof activated) != 0 || find_binding(s, provider) != b) return;
  if (out_binding != NULL && find_binding(s, out_provider) == out_binding) { resume_slot(s, out_provider, NULL, NULL, NULL); return; }
  qa_range_error("Original source refused weapon resumption");
}
int qa_app_sim_weapon_slot_bind(qa_app_sim_weapon_slot *slot, qa_source_weapon_binding source) {
  bool exists = false;
  for (int i = 0; i < MAX_BINDINGS; i++) if (slot->bindings[i].used && is(slot->bindings[i].handoff.provider, source.handoff.provider)) exists = true;
  if (!actor_current(slot) || (source.current && !source.current(source.ctx)) || exists)
    qa_range_error("Source weapon owner is unavailable or already bound");
  int before = slot->next_handle;
  add_binding(slot, source.handoff, source.current, source.ctx, source.read, source.ctx, source.read != NULL);
  return before;
}
void qa_app_sim_weapon_slot_unbind(qa_app_sim_weapon_slot *slot, int handle) {
  binding *b = NULL;
  for (int i = 0; i < MAX_BINDINGS; i++) if (slot->bindings[i].used && slot->bindings[i].handle == handle) b = &slot->bindings[i];
  if (b == NULL) return;
  qa_provider_id provider = b->handoff.provider;
  b->used = false;
  if (!actor_current(slot) || slot->restore_pending) return;
  qa_app_sim_weapon_slot_state state = slot->current;
  if ((is(state.kind, "active") && is(state.provider, provider)) || (!is(state.kind, "active") && is(state.from, provider))) {
    cancel_activation(slot, state); fallback_slot(slot);
  } else if (!is(state.kind, "active") && is(state.next.provider, provider)) {
    cancel_activation(slot, state);
    if (memcmp(&slot->current, &state, sizeof state) == 0) resume_slot(slot, state.from, NULL, NULL, NULL);
  }
}
const qa_source_weapon_presentation *qa_app_sim_weapon_slot_presentations(qa_app_sim_weapon_slot *slot, size_t *out_count) {
  slot->presentation_count = 0;
  if (actor_current(slot)) {
    for (int i = 0; i < MAX_BINDINGS; i++) {
      binding *e = &slot->bindings[i];
      if (!e->used || !e->has_read || find_binding(slot, e->handoff.provider) != e) continue;
      qa_source_weapon_presentation p = e->read(e->read_ctx);
      if (!is(p.source.provider, e->handoff.provider)) qa_range_error("Source weapon presentation belongs to another owner");
      if (find_binding(slot, e->handoff.provider) == e) slot->presentations[slot->presentation_count++] = p;
    }
  }
  if (out_count) *out_count = slot->presentation_count;
  return slot->presentations;
}
static void reconcile_activation(qa_app_sim_weapon_slot *s, qa_app_sim_weapon_slot_state state) {
  if (!s->has_activation || memcmp(&s->activation_state, &state, sizeof state) != 0)
    qa_range_error("Weapon activation lost its source request");
  if (s->activation_cancelling) return;
  binding *outgoing = find_binding(s, state.from), *incoming = find_binding(s, state.next.provider);
  if (outgoing == NULL) { cancel_activation(s, state); if (memcmp(&s->current, &state, sizeof state) == 0) fallback_slot(s); return; }
  if (incoming == NULL) { cancel_activation(s, state); if (memcmp(&s->current, &state, sizeof state) == 0) resume_slot(s, state.from, NULL, NULL, NULL); return; }
  const char *status = s->activation_request.status(s->activation_request.ctx);
  if (!actor_current(s) || memcmp(&s->current, &state, sizeof state) != 0 || !s->has_activation
      || find_binding(s, state.from) != outgoing || find_binding(s, state.next.provider) != incoming) return;
  if (is(status, "pending")) return;
  s->has_activation = false;
  if (is(status, "accepted")) { qa_app_sim_weapon_slot_state o; memset(&o, 0, sizeof o); o.kind = "active"; o.provider = state.next.provider; s->current = o; }
  else resume_slot(s, state.from, NULL, NULL, NULL);
}
bool qa_app_sim_weapon_slot_request(qa_app_sim_weapon_slot *slot, qa_weapon_reference weapon) {
  qa_app_sim_weapon_slot_validate_restore(slot);
  if (!actor_current(slot)) return false;
  binding *destination = find_binding(slot, weapon.provider);
  if (destination == NULL || !destination->handoff.accepts(destination->handoff.ctx, weapon.item) || find_binding(slot, weapon.provider) != destination) return false;
  if (is(slot->current.kind, "activating")) reconcile_activation(slot, slot->current);
  qa_app_sim_weapon_slot_state state = slot->current;
  if (is(state.kind, "activating")) {
    if (is(state.next.provider, weapon.provider) && is(state.next.item, weapon.item)) return true;
    cancel_activation(slot, state);
    if (!actor_current(slot) || memcmp(&slot->current, &state, sizeof state) != 0 || find_binding(slot, weapon.provider) != destination) return false;
    if (is(weapon.provider, state.from)) resume_slot(slot, state.from, weapon.item, NULL, NULL);
    else { qa_app_sim_weapon_slot_state o; memset(&o, 0, sizeof o); o.kind = "switching"; o.from = state.from; o.next = weapon; slot->current = o; }
    return true;
  }
  if (is(state.kind, "switching")) { slot->current.next = weapon; return true; }
  if (is(state.provider, weapon.provider)) {
    bool accepted = destination->handoff.select(destination->handoff.ctx, weapon.item);
    return accepted && actor_current(slot) && find_binding(slot, weapon.provider) == destination;
  }
  binding *outgoing = find_binding(slot, state.provider);
  if (outgoing == NULL) { fallback_slot(slot); return false; }
  qa_app_sim_weapon_slot_state o; memset(&o, 0, sizeof o); o.kind = "switching"; o.from = state.provider; o.next = weapon; slot->current = o;
  outgoing->handoff.holster(outgoing->handoff.ctx);
  return true;
}
void qa_app_sim_weapon_slot_reconcile(qa_app_sim_weapon_slot *slot) {
  qa_app_sim_weapon_slot_validate_restore(slot);
  if (!actor_current(slot)) return;
  qa_app_sim_weapon_slot_state state = slot->current;
  if (is(state.kind, "activating")) { reconcile_activation(slot, state); return; }
  if (is(state.kind, "active")) { if (find_binding(slot, state.provider) == NULL) fallback_slot(slot); return; }
  binding *outgoing = find_binding(slot, state.from), *incoming = find_binding(slot, state.next.provider);
  if (outgoing == NULL) { fallback_slot(slot); return; }
  bool accepted = incoming != NULL && incoming->handoff.accepts(incoming->handoff.ctx, state.next.item);
  if (!actor_current(slot) || memcmp(&slot->current, &state, sizeof state) != 0) return;
  if (!accepted || incoming == NULL || find_binding(slot, state.next.provider) != incoming) { resume_slot(slot, state.from, NULL, NULL, NULL); return; }
  if (outgoing->handoff.is_holstered(outgoing->handoff.ctx) && memcmp(&slot->current, &state, sizeof state) == 0
      && find_binding(slot, state.from) == outgoing && find_binding(slot, state.next.provider) == incoming)
    resume_slot(slot, state.next.provider, state.next.item, state.from, outgoing);
}
