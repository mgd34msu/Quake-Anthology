/* Port of src/content/q3/guest-items.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_GUEST_ITEMS_H
#define QA_CONTENT_Q3_GUEST_ITEMS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/q3/equipment/lrctf_grapple_profile.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/qvm_mod_items.h"

typedef struct qa_q3_guest_weapon {
  double weapon; qa_item_id item; qa_item_id ammo; /* NULL = none */ const char *label;
  char *owned_item, *owned_ammo, *owned_label;
} qa_q3_guest_weapon;
extern const qa_q3_guest_weapon qa_q3_standard_guest_weapons[];
extern const qa_q3_guest_weapon qa_q3_base_guest_weapons[];
enum { QA_Q3_STANDARD_GUEST_WEAPON_COUNT = 13, QA_Q3_BASE_GUEST_WEAPON_COUNT = 10 };
typedef struct qa_q3_guest_weapon_list { qa_q3_guest_weapon *values; size_t count; } qa_q3_guest_weapon_list;
void qa_q3_guest_weapon_list_destroy(qa_q3_guest_weapon_list *list);

/* Mirror of QvmItemLayout (TS owner: src/compat/qvm/item-catalog.ts). */
typedef struct qa_q3_guest_qvm_item_layout {
  bool address_is_global; double address;
  bool count_is_global; double count, count_maximum;
  bool source_live; double stride;
  double field_class_name, field_pickup_name, field_type, field_tag;
  double weapon_type, ammo_type;
} qa_q3_guest_qvm_item_layout;
/* Mirror of QvmCatalogRecord (TS owner: src/compat/qvm/item-catalog.ts). */
typedef struct qa_q3_guest_qvm_catalog_record {
  char *class_name, *pickup_name; double type, tag, index, address;
} qa_q3_guest_qvm_catalog_record;
/* Mirror of Pick<MountedContent,"open"> (TS owner: src/content/mounts/index.ts):
 * false is TS null; bytes are malloc'd and transferred to the caller. */
typedef struct qa_q3_guest_mounts {
  void *ctx;
  bool (*open)(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_length);
} qa_q3_guest_mounts;
/* TS q3GuestWeapons (async becomes synchronous). declared_layout NULL is
 * TS undefined. Result list is caller-owned. */
qa_q3_guest_weapon_list qa_q3_guest_weapons(const qa_q3_equipment_qvm_artifact *artifact,
    qa_q3_guest_mounts mounts, const qa_q3_guest_qvm_item_layout *declared_layout, bool private_inventory);

/* Mirror of QvmModule fields read here (TS owner: src/compat/qvm/module.ts)
 * and QvmMemory bytes (TS owner: src/compat/qvm/memory.ts). */
typedef struct qa_q3_guest_qvm_module {
  qa_module_identity profile_module;
  const uint8_t *memory_bytes; size_t memory_length;
} qa_q3_guest_qvm_module;
/* Mirror of QvmInventoryProfile storage variant test (TS owner:
 * src/compat/qvm/game-inventory.ts): has_storage is TS `"storage" in inventory`. */
typedef struct qa_q3_guest_qvm_inventory_profile {
  bool has_storage; const qa_qvm_item_storage *storage; size_t storage_count;
} qa_q3_guest_qvm_inventory_profile;
typedef struct qa_q3_guest_selection { double value; qa_item_id item; } qa_q3_guest_selection;

typedef struct qa_q3_guest_catalog qa_q3_guest_catalog;
/* initial/selection arrays are borrowed at construction and copied where
 * retained. layout/inventory NULL are TS undefined/null. NULL only on OOM.
 * artifact NULL is TS `source === null` (a native module: no readable item
 * table, carries the weapons it was prepared with); module is then ignored. */
qa_q3_guest_catalog *qa_q3_guest_catalog_create(const qa_q3_equipment_qvm_artifact *artifact,
    qa_q3_guest_qvm_module module, const qa_q3_guest_qvm_item_layout *layout,
    const qa_q3_guest_weapon *initial, size_t initial_count,
    bool has_selection, const qa_q3_guest_selection *selection, size_t selection_count,
    const qa_q3_guest_qvm_inventory_profile *inventory);
void qa_q3_guest_catalog_destroy(qa_q3_guest_catalog *catalog);
/* Borrowed catalog-owned records/weapons, valid until reset/close/destroy
 * or the next weapons() recomputation. */
const qa_q3_guest_qvm_catalog_record *qa_q3_guest_catalog_records(qa_q3_guest_catalog *catalog, size_t *out_count);
const qa_q3_guest_weapon *qa_q3_guest_catalog_weapons(qa_q3_guest_catalog *catalog, size_t *out_count);
void qa_q3_guest_catalog_reset(qa_q3_guest_catalog *catalog);
void qa_q3_guest_catalog_close(qa_q3_guest_catalog *catalog);
#endif
