#include "qa/persistence_fields.h"
#include "portals.h"
#include "guest_q3_private.h"
#include "control_frame.h"
#include "qa/source_save.h"
#include "qa/id_index.h"
#include "qa/pool.h"

typedef struct application_portal_claim {
    qa_actor_owner provider;
    uint64_t source_serial;
    uint64_t map_generation;
    qa_game_family family;
    uint32_t first, second, portal, contributions;
} application_portal_claim;

struct application_portals {
    qa_unified_frame_lease *storage;
    qa_id_index index;
    qa_pool claims;
};

static uint64_t claim_key(const application_portal_claim *claim)
{
    return claim->family == QA_GAME_Q2
        ? ((uint64_t)claim->portal << 32) | UINT32_MAX
        : ((uint64_t)claim->first << 32) | claim->second;
}

static application_portal_claim *claim_at(const struct application_portals *owner, size_t ordinal)
{
    return qa_pool_at(&owner->claims, owner->index.entries[ordinal].value);
}

static application_portal_claim *claim_find(const struct application_portals *owner,
    const application_portal_claim *key)
{
    if (!owner) return NULL;
    qa_id_index_cursor cursor = qa_id_index_begin(&owner->index, key->provider, claim_key(key));
    size_t slot;
    return qa_id_index_next(&owner->index, &cursor, &slot) ? qa_pool_at(&owner->claims, slot) : NULL;
}

static void owner_destroy(struct application_portals *owner)
{
    if (!owner) return;
    qa_unified_frame_lease *storage = owner->storage;
    qa_id_index_destroy(&owner->index);
    qa_unified_frame_lease_release(storage);
}

static struct application_portals *owner_create(qa_application *app, size_t capacity,
    qa_error *error)
{
    qa_unified_frame_lease *storage = application_control_storage_acquire(app, error);
    if (!storage) return NULL;
    struct application_portals *owner = qa_unified_frame_lease_alloc(storage, 1, sizeof(*owner),
        _Alignof(struct application_portals), error);
    if (!owner) { qa_unified_frame_lease_release(storage); return NULL; }
    qa_arena *arena = qa_unified_frame_lease_arena(storage);
    *owner = (struct application_portals){.storage = storage, .index = {.storage = arena}};
    if (!qa_pool_prepare(&owner->claims, arena, capacity, sizeof(application_portal_claim),
        _Alignof(application_portal_claim), error) || !qa_id_index_reserve(&owner->index, capacity, error)) {
        owner_destroy(owner);
        return NULL;
    }
    return owner;
}

static application_portal_claim *claim_take(struct application_portals *owner,
    size_t *slot, qa_error *error)
{
    if (!qa_id_index_reserve(&owner->index, 1, error)) return NULL;
    if (owner->claims.head == SIZE_MAX && !qa_pool_extend(&owner->claims,
        qa_unified_frame_lease_arena(owner->storage), owner->claims.capacity ? owner->claims.capacity : 8,
        error)) return NULL;
    return qa_pool_take(&owner->claims, slot);
}

static application_provider *claim_provider(qa_application *app,
                                             qa_actor_owner owner)
{
    return application_provider_lookup(app, owner, APPLICATION_PROVIDER_LIVE);
}

static bool claim_valid(qa_application *app, const application_portal_claim *c,
                          qa_error *error)
{
    application_provider *p = claim_provider(app, c->provider);
    if (!p || !p->launch || !c->source_serial || p->launch->identity != c->source_serial ||
        c->map_generation != qa_collision_load_generation(app->geometry) ||
        c->family != qa_collision_geometry_family(app->geometry))
        return application_fail(error, QA_ERROR_FORMAT,
                                "Native portal claim has no admitted map/source identity");
    if (c->family == QA_GAME_Q2) {
        bool primary;
        uint32_t count;
        if (p->kind != APPLICATION_PROVIDER_Q2 || c->first || c->second)
            return application_fail(error, QA_ERROR_FORMAT, "Invalid native Q2 portal owner");
        return qa_collision_portal_state(app->geometry, c->portal, &primary, &count, error);
    }
    if (c->family != QA_GAME_Q3 || p->kind != APPLICATION_PROVIDER_Q3 ||
        c->portal || c->first >= c->second ||
        c->second >= qa_collision_area_count(app->geometry) ||
        c->second > INT32_MAX || c->contributions > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Invalid native Q3 portal owner");
    return true;
}

static bool change(application_provider *provider, application_portal_claim key,
                     bool open, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    if (!app || !app->geometry || !provider->launch || !provider->constructed)
        return application_fail(error, QA_ERROR_ARGUMENT, "Portal mutation has no live native owner");
    key.provider = provider->owner;
    key.source_serial = provider->launch->identity;
    key.map_generation = qa_collision_load_generation(app->geometry);
    if (!claim_valid(app, &key, error))
        return false;
    struct application_portals *owner = app->portals;
    application_portal_claim *claim = claim_find(owner, &key);
    uint32_t before = claim ? claim->contributions : 0;
    uint32_t after;
    if (key.family == QA_GAME_Q2) {
        /* Q2 SetAreaPortalState assigns a boolean; repeated sets are legal. */
        after = open ? 1u : 0u;
        if (before == after) return true;
    } else {
        if (open ? before == UINT32_MAX : before == 0)
            return application_fail(error, QA_ERROR_ARGUMENT, "Native portal owner count overflow or underflow");
        after = open ? before + 1 : before - 1;
    }
    if (!owner) {
        owner = owner_create(app, 8, error);
        if (!owner) return false;
        app->portals = owner;
    }
    size_t slot = SIZE_MAX;
    bool admitted = claim != NULL;
    if (!admitted && !(claim = claim_take(owner, &slot, error))) return false;
    bool ok = key.family == QA_GAME_Q2
        ? qa_collision_adjust_portal(app->geometry, key.portal, (int)after - (int)before, error)
        : qa_collision_adjust_area_pair(app->geometry, (int32_t)key.first,
                                         (int32_t)key.second, open, error);
    if (!ok) {
        if (!admitted) qa_pool_release(&owner->claims, slot);
        return false;
    }
    if (!admitted) {
        *claim = key;
        qa_id_index_insert(&owner->index, key.provider, claim_key(&key), slot);
    }
    claim->contributions = after;
    return true;
}

bool application_portal_q2(application_provider *provider, uint32_t portal,
                            bool open, qa_error *error)
{
    return change(provider, (application_portal_claim){.family = QA_GAME_Q2,
                                                      .portal = portal}, open, error);
}

bool application_portal_q3(application_provider *provider, uint32_t first,
                            uint32_t second, bool open, qa_error *error)
{
    return change(provider, (application_portal_claim){.family = QA_GAME_Q3,
        .first = first < second ? first : second,
        .second = first < second ? second : first}, open, error);
}

static bool target(const qa_collision_portal_checkpoint *shared,
                     const qa_q3_host_portal_claim *claim, size_t *index,
                     qa_error *error)
{
    if (claim->family != shared->family || claim->map_generation != shared->map_generation ||
        claim->contributions > UINT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Portal claim differs from shared map identity or range");
    if (shared->family == QA_GAME_Q2) {
        size_t low = 0, high = shared->portal_count;
        while (low < high) {
            size_t middle = low + (high - low) / 2;
            if (shared->portals[middle].portal < claim->portal)
                low = middle + 1;
            else
                high = middle;
        }
        if (low == shared->portal_count || shared->portals[low].portal != claim->portal)
            return application_fail(error, QA_ERROR_FORMAT, "Portal claim names an absent Q2 portal");
        *index = low;
        return true;
    }
    if (shared->family != QA_GAME_Q3 || claim->first >= claim->second ||
        claim->second >= shared->area_count)
        return application_fail(error, QA_ERROR_FORMAT, "Portal claim names an invalid Q3 area pair");
    *index = (size_t)claim->first * shared->area_count + claim->second;
    return *index < shared->area_pair_count ||
        application_fail(error, QA_ERROR_FORMAT, "Portal claim exceeds shared Q3 area table");
}

static qa_q3_host_portal_claim shared_claim(const application_portal_claim *c)
{
    return (qa_q3_host_portal_claim){.family = c->family, .map_generation = c->map_generation,
        .first = c->first, .second = c->second, .portal = c->portal,
        .contributions = c->contributions};
}

typedef struct portal_retirement {
    struct application_portals *owner;
    qa_actor_owner provider;
} portal_retirement;

static bool keep_claim(void *context, const qa_id_index_entry *entry)
{
    portal_retirement *retirement = context;
    if (entry->key != retirement->provider) return true;
    qa_pool_release(&retirement->owner->claims, entry->value);
    return false;
}

bool application_portals_close(qa_application *app, qa_actor_owner provider,
                                qa_error *error)
{
    if (!app || !app->portals)
        return true;
    struct application_portals *owner = app->portals;
    bool found = false;
    for (size_t i = 0; i < owner->index.count; ++i)
        found |= !provider || owner->index.entries[i].key == provider;
    if (!found)
        return true;
    qa_collision_portal_checkpoint shared = {0};
    bool ok = qa_collision_capture_portals(app->geometry, &shared, error);
    for (size_t i = 0; ok && i < owner->index.count; ++i) {
        const application_portal_claim *c = claim_at(owner, i);
        if (provider && c->provider != provider)
            continue;
        qa_q3_host_portal_claim claim = shared_claim(c);
        size_t index;
        ok = claim_valid(app, c, error) && target(&shared, &claim, &index, error);
        if (!ok)
            break;
        uint32_t *count = shared.family == QA_GAME_Q2
            ? &shared.portals[index].contributions : shared.area_pairs + index;
        if (*count < c->contributions) {
            ok = application_fail(error, QA_ERROR_FORMAT, "Native portal close exceeds its shared count");
            break;
        }
        *count -= c->contributions;
        if (shared.family == QA_GAME_Q3)
            shared.area_pairs[(size_t)c->second * shared.area_count + c->first] = *count;
    }
    if (ok)
        ok = qa_collision_restore_portals(app->geometry, &shared, error);
    qa_collision_portal_checkpoint_free(&shared);
    if (!ok)
        return false;
    if (!provider) application_portals_destroy(app);
    else {
        portal_retirement retirement = {owner, provider};
        qa_id_index_retain(&owner->index, keep_claim, &retirement);
    }
    return true;
}

void application_portals_destroy(qa_application *app)
{
    if (app && app->portals) {
        struct application_portals *owner = app->portals;
        app->portals = NULL;
        owner_destroy(owner);
    }
}

static bool add_claim(const qa_collision_portal_checkpoint *shared, uint32_t *sums,
                        const qa_q3_host_portal_claim *claim, qa_error *error)
{
    size_t index;
    if (!target(shared, claim, &index, error))
        return false;
    if (claim->contributions > UINT32_MAX - sums[index])
        return application_fail(error, QA_ERROR_FORMAT, "Collective portal contributions overflow");
    sums[index] += (uint32_t)claim->contributions;
    if (shared->family == QA_GAME_Q3)
        sums[(size_t)claim->second * shared->area_count + claim->first] = sums[index];
    return true;
}

static bool add_host(const qa_collision_portal_checkpoint *shared, uint32_t *sums,
                       qa_q3_host *host, qa_error *error)
{
    size_t count = qa_q3_host_portal_claim_count(host);
    for (size_t i = 0; i < count; ++i) {
        qa_q3_host_portal_claim claim;
        if (!qa_q3_host_portal_claim_at(host, i, &claim) ||
            !add_claim(shared, sums, &claim, error))
            return application_fail(error, QA_ERROR_FORMAT, "Guest portal contribution is not admitted");
    }
    return true;
}

static bool collect(qa_application *app,
                    const qa_collision_portal_checkpoint *shared,
                    uint32_t **out, qa_error *error)
{
    size_t count = shared->family == QA_GAME_Q2 ? shared->portal_count : shared->area_pair_count;
    uint32_t *sums = count ? calloc(count, sizeof(*sums)) : NULL;
    bool ok = !count || sums != NULL;
    if (!ok)
        application_fail(error, QA_ERROR_MEMORY, "Allocating collective portal qualification");
    for (size_t i = 0; ok && app->portals && i < app->portals->index.count; ++i) {
        const application_portal_claim *c = claim_at(app->portals, i);
        qa_q3_host_portal_claim claim = shared_claim(c);
        ok = claim_valid(app, c, error) && add_claim(shared, sums, &claim, error);
    }
    for (application_provider *p = app->live_providers; ok && p; p = p->next_live) {
        struct application_q3_guest *engine = q3g_engine(p);
        if (engine) {
            for (q3g_role *role = engine->roles; ok && role; role = role->next)
                ok = add_host(shared, sums, role->host, error);
        } else if (p->kind == APPLICATION_PROVIDER_NATIVE && p->state.native.q3_host) {
            ok = add_host(shared, sums, p->state.native.q3_host, error);
        }
    }
    if (!ok) {
        free(sums);
        return false;
    }
    *out = sums;
    return true;
}

static bool shared_state(qa_application *app, bool reconnect, qa_error *error)
{
    if (!app || !app->geometry)
        return application_fail(error, QA_ERROR_ARGUMENT, "Portal qualification requires actual shared geometry");
    qa_collision_portal_checkpoint shared = {0};
    if (!qa_collision_capture_portals(app->geometry, &shared, error))
        return false;
    uint32_t *sums = NULL;
    bool ok = collect(app, &shared, &sums, error);
    size_t count = shared.family == QA_GAME_Q2 ? shared.portal_count : shared.area_pair_count;
    for (size_t i = 0; ok && i < count; ++i) {
        uint32_t *actual = shared.family == QA_GAME_Q2
            ? &shared.portals[i].contributions : shared.area_pairs + i;
        if (reconnect)
            *actual = sums[i];
        else if (sums[i] != *actual)
            ok = application_fail(error, QA_ERROR_FORMAT,
                                  "Shared portal count differs from collective actual source owners");
    }
    /* Original Q2 SV_ReadLevelFile/CM_ReadPortalState refloods after restore.
     * Counts derive from the restored native and guest owners; primary portal
     * booleans and no-areas policy remain their actual shared mutable state. */
    if (ok && reconnect)
        ok = qa_collision_restore_portals(app->geometry, &shared, error);
    free(sums);
    qa_collision_portal_checkpoint_free(&shared);
    return ok;
}

bool application_portals_validate(qa_application *app, qa_error *error)
{
    return shared_state(app, false, error);
}

bool application_portals_reconnect(qa_application *app, qa_error *error)
{
    return shared_state(app, true, error);
}

static bool row_fields(qa_source_save_io *io, qa_application *app,
                         application_portal_claim *c)
{
    uint32_t family = qa_persistence_family_tag(c->family);
    uint64_t reserved = 0;
    if (!qa_source_save_string(io, &c->provider) ||
        !qa_source_save_u64(io, &reserved) || !qa_source_save_u32(io, &family) ||
        !qa_source_save_u32(io, &c->first) || !qa_source_save_u32(io, &c->second) ||
        !qa_source_save_u32(io, &c->portal) || !qa_source_save_u32(io, &c->contributions))
        return false;
    c->family = qa_persistence_family_from_tag(family);
    if (io->direction == QA_SOURCE_SAVE_READ) {
        application_provider *provider = claim_provider(app, c->provider);
        if (!provider || !provider->launch)
            return application_fail(io->error, QA_ERROR_FORMAT, "Saved portal source owner is absent");
        c->source_serial = provider->launch->identity;
        c->map_generation = qa_collision_load_generation(app->geometry);
        if (c->family == QA_GAME_Q2)
            c->contributions = c->contributions != 0;
    }
    return claim_valid(app, c, io->error);
}

static bool header(qa_source_save_io *io, bool *present, size_t *count, size_t *capacity)
{
    uint8_t magic[4] = {'Q', 'A', 'P', 'O'};
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, "QAPO", 4) &&
        qa_source_save_bool(io, present) &&
        qa_source_save_count(io, count, SIZE_MAX / sizeof(application_portal_claim)) &&
        qa_source_save_count(io, capacity, SIZE_MAX / sizeof(application_portal_claim)) &&
        *count <= *capacity && (*present || (!*count && !*capacity));
}

static bool primary_fields(qa_source_save_io *io, qa_application *app)
{
    qa_collision_portal_checkpoint shared = {0};
    if (!qa_collision_capture_portals(app->geometry, &shared, io->error))
        return false;
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = 0;
    if (!reading)
        for (size_t i = 0; i < shared.portal_count; ++i)
            count += shared.portals[i].primary;
    bool ok = qa_source_save_count(io, &count, shared.portal_count);
    if (reading) {
        for (size_t i = 0; i < shared.portal_count; ++i)
            shared.portals[i].primary = false;
        uint32_t previous = 0;
        for (size_t i = 0; ok && i < count; ++i) {
            uint32_t portal = 0;
            size_t index;
            ok = qa_source_save_u32(io, &portal);
            if (ok && i && portal <= previous)
                ok = application_fail(io->error, QA_ERROR_FORMAT,
                                      "Saved primary portal IDs are not unique and ordered");
            qa_q3_host_portal_claim claim = {.family = shared.family,
                .map_generation = shared.map_generation, .portal = portal};
            if (ok) ok = target(&shared, &claim, &index, io->error);
            if (ok) shared.portals[index].primary = true;
            previous = portal;
        }
        if (ok) ok = qa_collision_restore_portals(app->geometry, &shared, io->error);
    } else {
        for (size_t i = 0; ok && i < shared.portal_count; ++i)
            if (shared.portals[i].primary)
                ok = qa_source_save_u32(io, &shared.portals[i].portal);
    }
    qa_collision_portal_checkpoint_free(&shared);
    return ok;
}

bool application_portals_capture(qa_application *app, qa_buffer *out, qa_error *error)
{
    if (!out || !application_portals_validate(app, error))
        return false;
    qa_source_save_io io = {0};
    bool present = app->portals != NULL;
    size_t count = present ? app->portals->index.count : 0;
    size_t capacity = present ? app->portals->claims.capacity : 0;
    bool ok = qa_source_save_writer(&io, app->session, error) && header(&io, &present, &count, &capacity);
    for (size_t i = 0; ok && i < count; ++i) {
        application_portal_claim copy = *claim_at(app->portals, i);
        ok = row_fields(&io, app, &copy);
    }
    if (ok) ok = primary_fields(&io, app);
    if (ok) ok = qa_source_save_text_assert(&io, qa_collision_map_name(app->geometry));
    if (ok)
        ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool application_portals_restore(qa_application *app, qa_bytes bytes, qa_error *error)
{
    if (!app || !app->session || !app->geometry || app->portals)
        return application_fail(error, QA_ERROR_ARGUMENT, "Portal import requires an empty actual candidate owner");
    qa_source_save_io io = {0};
    bool present = false;
    size_t count = 0, capacity = 0;
    struct application_portals *owner = NULL;
    bool ok = qa_source_save_reader(&io, app->session, bytes, error) &&
        header(&io, &present, &count, &capacity);
    if (ok && count > (bytes.size - io.offset) / 36)
        ok = application_fail(error, QA_ERROR_FORMAT, "Native portal journal byte extent disagrees");
    if (ok && present) { owner = owner_create(app, capacity, error); ok = owner != NULL; }
    for (size_t i = 0; ok && i < count; ++i) {
        application_portal_claim claim = {0};
        ok = row_fields(&io, app, &claim);
        if (ok && claim_find(owner, &claim))
            ok = application_fail(error, QA_ERROR_FORMAT, "Duplicate native portal owner/key");
        if (ok) {
            size_t slot;
            application_portal_claim *row = claim_take(owner, &slot, error);
            ok = row != NULL;
            if (ok) {
                *row = claim;
                qa_id_index_insert(&owner->index, claim.provider, claim_key(&claim), slot);
            }
        }
    }
    /* Earlier records cannot recover primary flags they never saved. */
    if (ok && io.offset < bytes.size) ok = primary_fields(&io, app);
    if (ok && io.offset < bytes.size)
        ok = qa_source_save_text_assert(&io, qa_collision_map_name(app->geometry));
    if (ok)
        ok = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK)
        application_fail(error, QA_ERROR_FORMAT, "Invalid native portal continuation");
    if (ok && present) {
        app->portals = owner;
    } else {
        owner_destroy(owner);
    }
    return ok;
}
