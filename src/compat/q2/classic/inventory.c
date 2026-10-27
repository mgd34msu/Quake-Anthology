/* Port of src/compat/q2/classic/inventory.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/classic/inventory.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *item; double index; bool is_ammo; double offset; } native_item_t;
struct qa_compat_q2_classic_source_inventory {
  qa_compat_q2_classic_guest_host *host; qa_compat_q2_classic_world_profile profile; bool own_profile;
  native_item_t *items; size_t item_count;
  qa_inventory_entry *scratch;
  qa_raw_entity_view bound_record; qa_owned_actor bound_actor; bool has_bound;
};
static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static bool valid_classname(const char *s) {
  if (!s[0] || s[0] < 'a' || s[0] > 'z') return false;
  for (const char *p = s + 1; *p; p++) if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return false;
  return true;
}
qa_compat_q2_classic_source_inventory *qa_compat_q2_classic_source_inventory_create(
    qa_compat_q2_classic_guest_host *host, qa_guest_address image,
    const qa_compat_q2_classic_world_profile *declared, bool has_declared) {
  qa_compat_q2_memory *memory = qa_compat_q2_classic_guest_host_get_memory(host);
  qa_compat_q2_classic_world_profile profile; bool own = false;
  if (!has_declared && !declared) {
    if (!qa_compat_q2_classic_get_primary_world_profile(memory->module.digest, &profile)) return NULL;
    own = false;
  } else {
    if (!declared) return NULL;
    profile = *declared; own = false;
  }
  if (!profile.base.digest || !memory->module.digest || strcmp(profile.base.digest, memory->module.digest) != 0)
    qa_range_error("Classic inventory profile belongs to another artifact");
  qa_compat_q2_classic_source_inventory *inv = calloc(1, sizeof *inv);
  if (!inv) return NULL;
  inv->host = host; inv->profile = profile; inv->own_profile = own;
  const qa_compat_q2_classic_world_profile *pr = &inv->profile;
  size_t count = (size_t)pr->inventory_table.count;
  inv->items = calloc(count ? count : 1, sizeof *inv->items); inv->item_count = 0;
  for (size_t index = 0; index < count; index++) {
    qa_guest_address address = qa_compat_q2_mem_offset(memory, image, (int64_t)(pr->base.globals.item_list + (double)index * pr->base.globals.item_bytes));
    qa_guest_address cp, np;
    bool hc = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, address, (int64_t)pr->inventory_table.class_name), &cp);
    bool hn = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, address, (int64_t)pr->inventory_table.label), &np);
    char *classname = qa_compat_q2_read_classic_string(memory, hc, cp, 65536);
    char *name = qa_compat_q2_read_classic_string(memory, hn, np, 65536);
    char *item = NULL;
    const qa_compat_q2_classic_unnamed_item *unnamed = NULL;
    for (size_t u = 0; u < pr->inventory_table.unnamed_count; u++)
      if (pr->inventory_table.unnamed[u].index == (double)index && strcmp(pr->inventory_table.unnamed[u].label, name) == 0) unnamed = &pr->inventory_table.unnamed[u];
    if (classname[0] == 0 && unnamed) item = xstrdup(unnamed->item);
    else {
      if (!valid_classname(classname)) qa_range_error("Native item has no qualified classname");
      size_t need = strlen(classname) + 4; item = malloc(need); snprintf(item, need, "q2:%s", classname);
    }
    for (size_t k = 0; k < inv->item_count; k++) if (strcmp(inv->items[k].item, item) == 0) qa_range_error("Duplicate native inventory item");
    int32_t flags = qa_compat_q2_mem_read_i32(memory, qa_compat_q2_mem_offset(memory, address, (int64_t)pr->inventory_table.flags));
    native_item_t *it = &inv->items[inv->item_count++];
    it->item = item; it->index = (double)index;
    if ((flags & (int32_t)pr->inventory_table.ammo_flag) == 0) { it->is_ammo = false; }
    else {
      int32_t tag = qa_compat_q2_mem_read_i32(memory, qa_compat_q2_mem_offset(memory, address, (int64_t)pr->inventory_table.tag));
      if (tag < 0 || (size_t)tag >= pr->inventory_table.capacity_count) qa_range_error("Native ammo tag has no qualified capacity field");
      it->is_ammo = true; it->offset = pr->inventory_table.capacities[tag];
    }
    free(classname); free(name);
  }
  if (pr->inventory_table.sentinel) {
    qa_guest_address sentinel = qa_compat_q2_mem_offset(memory, image, (int64_t)(pr->base.globals.item_list + pr->inventory_table.count * pr->base.globals.item_bytes));
    size_t bytes = (size_t)pr->base.globals.item_bytes; uint8_t *buf = malloc(bytes ? bytes : 1);
    memory->copy(memory->ctx, sentinel, buf, (double)bytes);
    for (size_t i = 0; i < bytes; i++) if (buf[i] != 0) qa_range_error("Native item table exceeds its qualified roster");
    free(buf);
  }
  inv->scratch = calloc(inv->item_count ? inv->item_count : 1, sizeof *inv->scratch);
  return inv;
}
void qa_compat_q2_classic_source_inventory_destroy(qa_compat_q2_classic_source_inventory *inv) {
  if (!inv) return;
  for (size_t i = 0; i < inv->item_count; i++) free(inv->items[i].item);
  free(inv->items); free(inv->scratch); free(inv);
}
typedef struct bind_ctx { qa_compat_q2_classic_source_inventory *inv; qa_raw_entity_view record; qa_owned_actor actor; } bind_ctx_t;
static qa_guest_address client_of(bind_ctx_t *b) {
  qa_compat_q2_classic_guest_host *host = b->inv->host;
  qa_compat_q2_memory *memory = qa_compat_q2_classic_guest_host_get_memory(host);
  qa_owned_actor current;
  if (!qa_compat_q2_classic_edicts_current(qa_compat_q2_classic_guest_host_get_edicts(host), b->record, &current) || !qa_same_owned_actor(current, b->actor))
    qa_range_error("Native inventory owner was released");
  qa_guest_address address;
  if (!qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, b->record.address, 84), &address))
    qa_range_error("Native inventory requires a source client");
  return address;
}
static bool bind_mutable(void *ctx, qa_item_id item) {
  bind_ctx_t *b = ctx; (void)client_of(b);
  for (size_t i = 0; i < b->inv->item_count; i++) if (strcmp(b->inv->items[i].item, item) == 0 && b->inv->items[i].is_ammo) return true;
  return false;
}
static const qa_inventory_entry *bind_read(void *ctx, size_t *out_count) {
  bind_ctx_t *b = ctx;
  qa_compat_q2_memory *memory = qa_compat_q2_classic_guest_host_get_memory(b->inv->host);
  qa_guest_address address = client_of(b);
  for (size_t i = 0; i < b->inv->item_count; i++) {
    native_item_t *it = &b->inv->items[i];
    qa_guest_address count_a = qa_compat_q2_mem_offset(memory, address, (int64_t)(b->inv->profile.base.client.inventory + it->index * 4));
    b->inv->scratch[i].item = it->item;
    b->inv->scratch[i].count = qa_compat_q2_mem_read_i32(memory, count_a);
    b->inv->scratch[i].capacity = it->is_ammo ? qa_compat_q2_mem_read_i32(memory, qa_compat_q2_mem_offset(memory, address, (int64_t)it->offset))
                                              : (it->index == b->inv->profile.inventory_table.empty_index ? 0 : 2147483647.0);
    b->inv->scratch[i].has_count_policy = true;
    b->inv->scratch[i].count_policy = (qa_inventory_count_policy){"source-counter", "int32"};
  }
  if (out_count) *out_count = b->inv->item_count;
  return b->inv->scratch;
}
static void bind_write(void *ctx, qa_inventory_entry entry) {
  bind_ctx_t *b = ctx;
  qa_compat_q2_memory *memory = qa_compat_q2_classic_guest_host_get_memory(b->inv->host);
  native_item_t *item = NULL;
  for (size_t i = 0; i < b->inv->item_count; i++) if (strcmp(b->inv->items[i].item, entry.item) == 0) item = &b->inv->items[i];
  if (!item) qa_range_error("Item has no native inventory binding");
  if (entry.count != trunc(entry.count) || entry.count < -2147483648.0 || entry.count > 2147483647.0 ||
      entry.capacity != trunc(entry.capacity) || entry.capacity < 0 || entry.capacity > 2147483647.0)
    qa_range_error("Native inventory exceeds int32");
  if (!item->is_ammo && entry.capacity != (item->index == b->inv->profile.inventory_table.empty_index ? 0 : 2147483647.0))
    qa_range_error("Native item has a fixed source counter capacity");
  qa_guest_address address = client_of(b);
  if (item->is_ammo) qa_compat_q2_mem_write_i32(memory, qa_compat_q2_mem_offset(memory, address, (int64_t)item->offset), (int32_t)entry.capacity);
  qa_compat_q2_mem_write_i32(memory, qa_compat_q2_mem_offset(memory, address, (int64_t)(b->inv->profile.base.client.inventory + item->index * 4)), (int32_t)entry.count);
}
qa_inventory_state_binding qa_compat_q2_classic_source_inventory_bind(qa_compat_q2_classic_source_inventory *inv, qa_raw_entity_view record) {
  qa_owned_actor actor;
  if (!qa_compat_q2_classic_edicts_current(qa_compat_q2_classic_guest_host_get_edicts(inv->host), record, &actor))
    qa_range_error("Native inventory requires a live source actor");
  bind_ctx_t *b = malloc(sizeof *b); b->inv = inv; b->record = record; b->actor = actor;
  qa_inventory_state_binding binding; memset(&binding, 0, sizeof binding);
  binding.ctx = b; binding.read = bind_read; binding.write = bind_write; binding.mutable_capacity = bind_mutable;
  return binding;
}
