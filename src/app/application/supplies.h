#ifndef QA_APPLICATION_SUPPLIES_H
#define QA_APPLICATION_SUPPLIES_H
#include "internal.h"
#include "qa/game_q3_supply.h"

typedef struct application_supplies application_supplies;

/* A publication owns these admissions; counts remain in its actual shared
 * inventory and selected source arsenals. */
bool application_supplies_create(qa_application *, qa_inventory *, application_supplies **,
                                  qa_error *);
bool application_supplies_idle(const application_supplies *);
bool application_supplies_destroy(application_supplies *, qa_error *);
bool application_supplies_prepare(application_supplies *, application_provider *source,
                                   application_provider *arsenal, qa_error *);
bool application_supplies_admit(application_supplies *, application_provider *source,
                                 qa_actor_id, qa_error *);
bool application_supplies_spawn(application_supplies *, application_provider *source,
                                 qa_actor_id, qa_error *);
void application_supplies_actor_released(application_supplies *, qa_actor_record);
bool application_supplies_for(application_supplies *, application_provider *source,
                               qa_actor_id, qa_supply **, qa_error *);
bool application_supplies_weapon_sources(application_supplies *, application_provider *source,
    qa_actor_id, const qa_supply_weapon *selected, size_t selected_count,
    const qa_supply_weapon *original, size_t original_count, qa_item_id *, qa_error *);
bool application_supplies_source_for(void *actual_provider, qa_actor_id, qa_supply **, qa_error *);
bool application_supplies_source_spawned(void *actual_provider, qa_actor_id, qa_error *);
/* Resolve the actual retained selected rule during isolated gameplay import.
 * The pickup codec owns its lease serial and full declarations. */
bool application_supplies_pickup_rule(application_supplies *,qa_actor_id,qa_actor_owner,
    uint64_t serial,uint32_t id,qa_pickup_rule *,qa_error *);

/* Both paths resolve the exact same authored offer against the selected
 * destination inventory. Source completion owns LOG/events/targets/respawn. */
bool application_supplies_q3_preview(void *, const qa_q3_supply_descriptor *,
    qa_q3_supply_kind *, qa_supply_preview_result *, qa_error *);
bool application_supplies_q3_take(void *, const qa_q3_supply_descriptor *,
    qa_q3_supply_kind *, bool *accepted, float *respawn_seconds, qa_error *);
bool application_supplies_q3_ammo_regeneration(void *, qa_actor_id, int32_t elapsed_ms,
    bool *handled, qa_error *);
bool application_supplies_q3_ammo_stored(void *, qa_actor_id, qa_q3_weapon, int32_t, qa_error *);
#endif
