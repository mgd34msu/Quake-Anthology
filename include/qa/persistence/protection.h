/*
 * Port of src/persistence/protection.ts: the hidden primary-armor
 * provider checkpoint (schema "world:primary-protection").
 * TS SessionActorRegistry (owner: src/world/actors/registry.ts) and
 * GameplayAuthority (owner: src/world/gameplay/authority.ts) have
 * no C port yet; this header reuses the shared persistence mirrors
 * (qa_session_actor_registry in source_items.h,
 * qa_gameplay_authority in world_state.h) instead of defining its
 * own. (world_state.c also keeps a static readPrimaryProtection
 * from before this port; qa_read_primary_protection below
 * supersedes it.)
 * Capture returns false only on allocation failure; the returned
 * checkpoint owns its bytes (caller frees bytes.bytes). Read
 * results borrow strings from `root` (owned when a checkpoint was
 * recorded; release with qa_primary_protection_free).
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_PROTECTION_H
#define QA_PERSISTENCE_PROTECTION_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/session.h"
#include "qa/contracts/world.h"
#include "qa/persistence/source_items.h"
#include "qa/persistence/value.h"
#include "qa/persistence/world_state.h"

/* TS HiddenArmor, keyed by the resolved OwnedActor of the TS Map. */
typedef struct qa_primary_protection_entry {
  qa_owned_actor actor; /* Map key */
  qa_saved_actor_id saved;
  qa_provider_id owner;
  bool has_regular;
  qa_regular_armor_state regular;
  bool has_powered;
  qa_powered_protection_state powered;
} qa_primary_protection_entry;

typedef struct qa_primary_protection {
  bool recorded;
  qa_primary_protection_entry *entries; /* owned array */
  size_t entry_count;
  qa_save_value *root; /* owned when recorded; NULL otherwise */
} qa_primary_protection;

bool qa_capture_primary_protection(const qa_session_actor_registry *actors,
                                   const qa_gameplay_authority *combat,
                                   qa_provider_checkpoint *out);
bool qa_read_primary_protection(const qa_save_image *save,
                                const qa_session_actor_registry *actors,
                                qa_primary_protection *out);
void qa_primary_protection_free(qa_primary_protection *protection);

#endif
