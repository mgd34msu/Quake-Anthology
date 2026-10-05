#include "portals.h"
#include "guest_q3_private.h"
#include "qa/source_save.h"

typedef struct application_portal_claim {
    qa_actor_owner provider;
    qa_sha256_digest identity;
    uint64_t map_identity;
    qa_collision_family family;
    uint32_t first, second, portal, contributions;
} application_portal_claim;

struct application_portals {
    application_portal_claim *claims;
    size_t count, capacity;
};

static application_provider *claim_provider(qa_application *app,
                                             qa_actor_owner owner)
{
    for (application_provider *p = app->live_providers; p; p = p->next_live)
        if (p->owner == owner)
            return p;
    return NULL;
}

static bool claim_valid(qa_application *app, const application_portal_claim *c,
                          qa_error *error)
{
    application_provider *p = claim_provider(app, c->provider);
    if (!p || !p->launch || !qa_sha256_equal(&p->launch->identity, &c->identity) ||
        c->map_identity != qa_collision_map_identity(app->geometry) ||
        c->family != qa_collision_geometry_family(app->geometry))
        return application_fail(error, QA_ERROR_FORMAT,
                                "Native portal claim has no admitted map/source identity");
    if (c->family == QA_COLLISION_Q2) {
        bool primary;
        uint32_t count;
        if (p->kind != APPLICATION_PROVIDER_Q2 || c->first || c->second)
            return application_fail(error, QA_ERROR_FORMAT, "Invalid native Q2 portal owner");
        return qa_collision_portal_state(app->geometry, c->portal, &primary, &count, error);
    }
    if (c->family != QA_COLLISION_Q3 || p->kind != APPLICATION_PROVIDER_Q3 ||
        c->portal || c->first >= c->second ||
        c->second >= qa_collision_area_count(app->geometry) ||
        c->second > INT32_MAX || c->contributions > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Invalid native Q3 portal owner");
    return true;
}

static bool same_claim(const application_portal_claim *a,
                         const application_portal_claim *b)
{
    return a->provider == b->provider && a->family == b->family &&
           a->first == b->first && a->second == b->second && a->portal == b->portal;
}

static bool change(application_provider *provider, application_portal_claim key,
                     bool open, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    if (!app || !app->geometry || !provider->launch || !provider->constructed)
        return application_fail(error, QA_ERROR_ARGUMENT, "Portal mutation has no live native owner");
    key.provider = provider->owner;
    key.identity = provider->launch->identity;
    key.map_identity = qa_collision_map_identity(app->geometry);
    if (!claim_valid(app, &key, error))
        return false;
    struct application_portals *owner = app->portals;
    size_t index = 0;
    while (owner && index < owner->count && !same_claim(owner->claims + index, &key))
        ++index;
    uint32_t before = owner && index < owner->count ? owner->claims[index].contributions : 0;
    uint32_t after;
    if (key.family == QA_COLLISION_Q2) {
        /* Q2 SetAreaPortalState assigns a boolean; repeated sets are legal. */
        after = open ? 1u : 0u;
        if (before == after) return true;
    } else {
        if (open ? before == UINT32_MAX : before == 0)
            return application_fail(error, QA_ERROR_ARGUMENT, "Native portal owner count overflow or underflow");
        after = open ? before + 1 : before - 1;
    }
    if (!owner) {
        owner = calloc(1, sizeof(*owner));
        if (!owner)
            return application_fail(error, QA_ERROR_MEMORY, "Allocating native portal owner");
        app->portals = owner;
    }
    if (index == owner->count && owner->count == owner->capacity) {
        size_t capacity = owner->capacity ? owner->capacity * 2 : 8;
        if (capacity < owner->capacity || capacity > SIZE_MAX / sizeof(*owner->claims))
            return application_fail(error, QA_ERROR_MEMORY, "Native portal journal extent overflow");
        void *next = realloc(owner->claims, capacity * sizeof(*owner->claims));
        if (!next)
            return application_fail(error, QA_ERROR_MEMORY, "Retaining native portal source claim");
        owner->claims = next;
        owner->capacity = capacity;
    }
    bool ok = key.family == QA_COLLISION_Q2
        ? qa_collision_adjust_portal(app->geometry, key.portal, (int)after - (int)before, error)
        : qa_collision_adjust_area_pair(app->geometry, (int32_t)key.first,
                                         (int32_t)key.second, open, error);
    if (!ok)
        return false;
    if (index == owner->count)
        owner->claims[owner->count++] = key;
    owner->claims[index].contributions = after;
    return true;
}

bool application_portal_q2(application_provider *provider, uint32_t portal,
                            bool open, qa_error *error)
{
    return change(provider, (application_portal_claim){.family = QA_COLLISION_Q2,
                                                      .portal = portal}, open, error);
}

bool application_portal_q3(application_provider *provider, uint32_t first,
                            uint32_t second, bool open, qa_error *error)
{
    return change(provider, (application_portal_claim){.family = QA_COLLISION_Q3,
        .first = first < second ? first : second,
        .second = first < second ? second : first}, open, error);
}

static bool target(const qa_collision_portal_checkpoint *shared,
                     const qa_q3_host_portal_claim *claim, size_t *index,
                     qa_error *error)
{
    if (claim->family != shared->family || claim->map_identity != shared->map_identity ||
        claim->contributions > UINT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Portal claim differs from shared map identity or range");
    if (shared->family == QA_COLLISION_Q2) {
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
    if (shared->family != QA_COLLISION_Q3 || claim->first >= claim->second ||
        claim->second >= shared->area_count)
        return application_fail(error, QA_ERROR_FORMAT, "Portal claim names an invalid Q3 area pair");
    *index = (size_t)claim->first * shared->area_count + claim->second;
    return *index < shared->area_pair_count ||
        application_fail(error, QA_ERROR_FORMAT, "Portal claim exceeds shared Q3 area table");
}

static qa_q3_host_portal_claim shared_claim(const application_portal_claim *c)
{
    return (qa_q3_host_portal_claim){.family = c->family, .map_identity = c->map_identity,
        .first = c->first, .second = c->second, .portal = c->portal,
        .contributions = c->contributions};
}

bool application_portals_close(qa_application *app, qa_actor_owner provider,
                                qa_error *error)
{
    if (!app || !app->portals)
        return true;
    struct application_portals *owner = app->portals;
    bool found = false;
    for (size_t i = 0; i < owner->count; ++i)
        found |= !provider || owner->claims[i].provider == provider;
    if (!found)
        return true;
    qa_collision_portal_checkpoint shared = {0};
    bool ok = qa_collision_capture_portals(app->geometry, &shared, error);
    for (size_t i = 0; ok && i < owner->count; ++i) {
        const application_portal_claim *c = owner->claims + i;
        if (provider && c->provider != provider)
            continue;
        qa_q3_host_portal_claim claim = shared_claim(c);
        size_t index;
        ok = claim_valid(app, c, error) && target(&shared, &claim, &index, error);
        if (!ok)
            break;
        uint32_t *count = shared.family == QA_COLLISION_Q2
            ? &shared.portals[index].contributions : shared.area_pairs + index;
        if (*count < c->contributions) {
            ok = application_fail(error, QA_ERROR_FORMAT, "Native portal close exceeds its shared count");
            break;
        }
        *count -= c->contributions;
        if (shared.family == QA_COLLISION_Q3)
            shared.area_pairs[(size_t)c->second * shared.area_count + c->first] = *count;
    }
    if (ok)
        ok = qa_collision_restore_portals(app->geometry, &shared, error);
    qa_collision_portal_checkpoint_free(&shared);
    if (!ok)
        return false;
    size_t keep = 0;
    for (size_t i = 0; i < owner->count; ++i)
        if (provider && owner->claims[i].provider != provider)
            owner->claims[keep++] = owner->claims[i];
    owner->count = keep;
    if (!provider)
        application_portals_destroy(app);
    return true;
}

void application_portals_destroy(qa_application *app)
{
    if (app && app->portals) {
        free(app->portals->claims);
        free(app->portals);
        app->portals = NULL;
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
    if (shared->family == QA_COLLISION_Q3)
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
    size_t count = shared->family == QA_COLLISION_Q2 ? shared->portal_count : shared->area_pair_count;
    uint32_t *sums = count ? calloc(count, sizeof(*sums)) : NULL;
    bool ok = !count || sums != NULL;
    if (!ok)
        application_fail(error, QA_ERROR_MEMORY, "Allocating collective portal qualification");
    for (size_t i = 0; ok && app->portals && i < app->portals->count; ++i) {
        const application_portal_claim *c = app->portals->claims + i;
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
    size_t count = shared.family == QA_COLLISION_Q2 ? shared.portal_count : shared.area_pair_count;
    for (size_t i = 0; ok && i < count; ++i) {
        uint32_t *actual = shared.family == QA_COLLISION_Q2
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
    uint32_t family = (uint32_t)c->family;
    if (!qa_source_save_u32(io, &c->provider) ||
        !qa_source_save_bytes(io, c->identity.bytes, sizeof(c->identity.bytes)) ||
        !qa_source_save_u64(io, &c->map_identity) || !qa_source_save_u32(io, &family) ||
        !qa_source_save_u32(io, &c->first) || !qa_source_save_u32(io, &c->second) ||
        !qa_source_save_u32(io, &c->portal) || !qa_source_save_u32(io, &c->contributions))
        return false;
    c->family = (qa_collision_family)family;
    if (io->direction == QA_SOURCE_SAVE_READ && c->family == QA_COLLISION_Q2)
        c->contributions = c->contributions != 0;
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
                .map_identity = shared.map_identity, .portal = portal};
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
    size_t count = present ? app->portals->count : 0;
    size_t capacity = present ? app->portals->capacity : 0;
    bool ok = qa_source_save_writer(&io, NULL, error) && header(&io, &present, &count, &capacity);
    for (size_t i = 0; ok && i < count; ++i) {
        application_portal_claim copy = app->portals->claims[i];
        ok = row_fields(&io, app, &copy);
    }
    if (ok) ok = primary_fields(&io, app);
    if (ok)
        ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool application_portals_restore(qa_application *app, qa_bytes bytes, qa_error *error)
{
    if (!app || !app->geometry || app->portals)
        return application_fail(error, QA_ERROR_ARGUMENT, "Portal import requires an empty actual candidate owner");
    qa_source_save_io io = {0};
    bool present = false;
    struct application_portals *owner = calloc(1, sizeof(*owner));
    if (!owner)
        return application_fail(error, QA_ERROR_MEMORY, "Allocating restored native portal owner");
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) &&
        header(&io, &present, &owner->count, &owner->capacity);
    if (ok && owner->count > (bytes.size - io.offset) / 64)
        ok = application_fail(error, QA_ERROR_FORMAT, "Native portal journal byte extent disagrees");
    if (ok && owner->capacity) {
        owner->claims = calloc(owner->capacity, sizeof(*owner->claims));
        if (!owner->claims)
            ok = application_fail(error, QA_ERROR_MEMORY, "Allocating restored native portal claims");
    }
    for (size_t i = 0; ok && i < owner->count; ++i) {
        ok = row_fields(&io, app, owner->claims + i);
        for (size_t j = 0; ok && j < i; ++j)
            if (same_claim(owner->claims + i, owner->claims + j))
                ok = application_fail(error, QA_ERROR_FORMAT, "Duplicate native portal owner/key");
    }
    /* Earlier records cannot recover primary flags they never saved. */
    if (ok && io.offset < bytes.size) ok = primary_fields(&io, app);
    if (ok)
        ok = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK)
        application_fail(error, QA_ERROR_FORMAT, "Invalid native portal continuation");
    if (ok && present) {
        app->portals = owner;
    } else {
        free(owner->claims);
        free(owner);
    }
    return ok;
}
