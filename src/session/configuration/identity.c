#include "qa/launch_identity.h"
#include "qa/arena.h"
#include "qa/json.h"
#include "qa/json_writer.h"
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IDENTITY_MAX_BYTES (16u * 1024u * 1024u)
#define IDENTITY_MAX_RECORDS 65536u
static bool fail(qa_error *error, const char *text)
{ qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", text); return false; }
static void text(qa_json_writer *w, const char *v) { qa_json_writer_string(w, v ? v : ""); }
static void word(qa_json_writer *w, uint64_t v)
{ char n[21]; snprintf(n, sizeof(n), "%" PRIu64, v); text(w, n); }
static void number(qa_json_writer *w, double v) { qa_json_writer_number(w, v); }
static void boolean(qa_json_writer *w, bool v) { qa_json_writer_bool(w, v); }
static void writer_fail(qa_json_writer *w, const char *message)
{ if (!w->failed) { w->failed = true; fail(&w->failure, message); } }
static void product(qa_json_writer *w, qa_catalog *catalog, qa_product_id id)
{
    const qa_product *p = id ? qa_catalog_product(catalog, id) : NULL;
    if (id && (!p || !p->identity)) { writer_fail(w, "Launch product lacks a stable catalog identity"); return; }
    text(w, p ? p->identity : "");
}
static void binary(qa_json_writer *w, qa_bytes bytes)
{
    static const char hex[] = "0123456789abcdef";
    if (bytes.size > IDENTITY_MAX_BYTES / 2 || (bytes.size && !bytes.data)) {
        writer_fail(w, "Launch option bytes exceed portable identity bounds"); return;
    }
    char *encoded = malloc(bytes.size * 2 + 1);
    if (!encoded) { writer_fail(w, "Allocating portable option bytes"); return; }
    for (size_t i = 0; i < bytes.size; ++i) { encoded[i * 2] = hex[bytes.data[i] >> 4]; encoded[i * 2 + 1] = hex[bytes.data[i] & 15]; }
    encoded[bytes.size * 2] = 0; text(w, encoded); free(encoded);
}
static void digest(qa_json_writer *w, const qa_sha256_digest *v)
{ char hex[65]; if (!v) { writer_fail(w, "Launch resource lacks immutable content digest"); return; } qa_sha256_hex(v, hex); text(w, hex); }
static void actor(qa_json_writer *w, const qa_actor_registry *registry, qa_actor_id v)
{
    if (!v.registry) { qa_json_writer_null(w); return; }
    qa_saved_actor_id saved;
    if (!registry || !qa_actors_save_reference(registry, v, &saved, &w->failure)) { w->failed = true; return; }
    qa_json_writer_array(w); number(w, saved.slot); word(w, saved.generation); qa_json_writer_end(w);
}
static void scope(qa_json_writer *w, const qa_actor_registry *registry, qa_launch_scope v)
{
    qa_json_writer_array(w); number(w, v.kind);
    actor(w, registry, v.kind == QA_SCOPE_ACTOR ? v.actor : (qa_actor_id){0});
    number(w, v.kind == QA_SCOPE_SEAT ? v.seat : 0); qa_json_writer_end(w);
}
static void clock_write(qa_json_writer *w, qa_clock_config v)
{
    qa_json_writer_array(w); number(w, v.kind); word(w, v.initial_time_ns); word(w, v.interval_ns);
    word(w, v.minimum_frame_ns); word(w, v.maximum_frame_ns); word(w, v.initial_lead_ns);
    number(w, v.maximum_steps); qa_json_writer_end(w);
}
static void rules_write(qa_json_writer *w, const qa_mode_rules *v)
{
    qa_json_writer_array(w);
#define N(member) number(w, v->member)
#define B(member) boolean(w, v->member)
#define W(member) word(w, v->member)
    N(source); N(kind); N(teams[0]); N(teams[1]); N(teams[2]); N(forced_team);
    N(frag_limit); N(capture_limit); N(warmup_seconds); N(competition); N(setup_seconds);
    N(countdown_seconds); N(match_seconds); N(max_game_players); N(election_percent);
    N(teamplay); N(rune_mask); N(vote_limit); N(flags); N(referee_flags);
    N(time_limit_minutes); N(obelisk_health); N(obelisk_regen); W(obelisk_regen_ns); W(obelisk_respawn_ns);
    B(enabled); B(friendly_fire); B(force_join); B(match_lock); B(paused); B(auto_lock);
    B(relics); B(single_player_active); B(tournament_restart); B(q2_rerelease); B(start_map);
    B(force_balance); B(voting_disabled); B(rogue_deathmatch);
#undef N
#undef B
#undef W
    qa_json_writer_end(w);
}
static void mode_write(qa_json_writer *w, const qa_launch_mode *mode)
{
    text(w, mode->instance);
    rules_write(w, &mode->rules);
    for (size_t i = 0; i < 3; ++i)
        text(w, mode->teams[i]);
    text(w, mode->forced_team);
    boolean(w, mode->primary_score);
}

bool qa_launch_mode_identity_encode(const qa_launch_snapshot *snapshot,
                                    size_t index, qa_buffer *out,
                                    qa_error *error)
{
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    if (!snapshot || !choices || !out || index >= choices->mode_count)
        return fail(error, "Missing selected mode identity");
    qa_json_writer writer = {0};
    qa_json_writer_array(&writer);
    mode_write(&writer, &choices->modes[index]);
    qa_json_writer_end(&writer);
    bool ok = qa_json_writer_finish(&writer, out, error);
    qa_json_writer_destroy(&writer);
    return ok;
}
static void resource_write(qa_json_writer *w, qa_catalog *catalog, const qa_launch_resource *v)
{
    qa_json_writer_array(w); product(w, catalog, v->product); text(w, v->path);
    digest(w, qa_resource_digest(v->resource)); word(w, qa_resource_bytes(v->resource).size);
    qa_json_writer_end(w);
}
bool qa_launch_identity_encode(const qa_launch_snapshot *snapshot, const qa_actor_registry *registry,
    qa_buffer *out, qa_error *error)
{
    const qa_launch_choices *v = qa_launch_snapshot_choices(snapshot);
    qa_catalog *catalog = qa_launch_snapshot_catalog(snapshot);
    if (!snapshot || !v || !catalog || !out) return fail(error, "Missing immutable launch identity owner");
    qa_json_writer w = {0}; qa_json_writer_object(&w);
    qa_json_writer_key(&w, "schema"); text(&w, "qa-launch-identity");
    qa_json_writer_key(&w, "version"); number(&w, 5);
    qa_json_writer_key(&w, "world"); qa_json_writer_array(&w);
    product(&w, catalog, v->world.preset); product(&w, catalog, v->world.geometry);
    product(&w, catalog, v->world.presentation); text(&w, v->world.map); text(&w, v->world.start_command);
    boolean(&w, v->world.explicit_presentation); boolean(&w, v->world.doppler); boolean(&w, v->world.campaign);
    number(&w, v->world.environment); product(&w, catalog, v->world.environment_product);
    text(&w, v->world.environment_path); number(&w, v->world.skill);
    boolean(&w, v->world.explicit_spawn_point); text(&w, v->world.spawn_point);
    qa_json_writer_end(&w);
#define BEGIN(name, count) qa_json_writer_key(&w, name); qa_json_writer_array(&w); for (size_t i = 0; i < (count); ++i) { qa_json_writer_array(&w)
#define END qa_json_writer_end(&w); } qa_json_writer_end(&w)
    BEGIN("providers", v->provider_count);
        const qa_launch_provider *p = &v->providers[i]; text(&w, p->instance); product(&w, catalog, p->product);
        number(&w, p->runtime); text(&w, p->implementation); text(&w, p->artifact); text(&w, p->component);
        clock_write(&w, p->clock); binary(&w, p->options);
    END;
    BEGIN("bindings", v->binding_count);
        const qa_launch_binding *p = &v->bindings[i]; scope(&w, registry, p->scope); number(&w, p->role);
        text(&w, p->selector); text(&w, p->instance); text(&w, p->definition);
    END;
    BEGIN("mods", v->mod_count);
        const qa_launch_mod_selection *p = &v->mods[i]; text(&w, p->instance); text(&w, p->component); boolean(&w, p->enabled);
    END;
    BEGIN("modes", v->mode_count);
        mode_write(&w, &v->modes[i]);
    END;
    BEGIN("equipment", v->equipment_count);
        const qa_launch_equipment *p = &v->equipment[i]; scope(&w, registry, p->scope);
        text(&w, p->instance); text(&w, p->grapple_source); text(&w, p->grenade_source);
        number(&w, p->selection.grapple); number(&w, p->selection.binding);
        boolean(&w, p->selection.retain_on_weapon_change); boolean(&w, p->selection.release_on_jump);
        boolean(&w, p->selection.release_on_teleport); boolean(&w, p->selection.grenades.enabled);
        boolean(&w, p->selection.grenades.infinite_ammo); number(&w, p->selection.grenades.initial_ammo);
        number(&w, p->selection.grenades.capacity);
    END;
    BEGIN("seats", v->seat_count);
        const qa_launch_seat *p = &v->seats[i]; number(&w, p->id); actor(&w, registry, p->actor);
        text(&w, p->name); text(&w, p->team); number(&w, p->input_device);
        boolean(&w, p->local); boolean(&w, p->spectator); boolean(&w, p->bot); number(&w, p->bot_skill);
        if (p->bot_definition) text(&w, p->bot_definition); else qa_json_writer_null(&w);
        number(&w, p->bot_delay_ms);
        if (p->character_model) text(&w, p->character_model); else qa_json_writer_null(&w);
        if (p->character_skin) text(&w, p->character_skin); else qa_json_writer_null(&w);
        if (p->character_head_model) text(&w, p->character_head_model); else qa_json_writer_null(&w);
        if (p->character_head_skin) text(&w, p->character_head_skin); else qa_json_writer_null(&w);
    END;
    BEGIN("loadout", v->loadout_count);
        const qa_launch_loadout *p = &v->loadout[i]; scope(&w, registry, p->scope); text(&w, p->item);
        number(&w, p->quantity); number(&w, p->capacity); boolean(&w, p->override_capacity); boolean(&w, p->drop_on_death);
    END;
    BEGIN("monsters", v->monster_count);
        const qa_launch_monster *p = &v->monsters[i]; text(&w, p->authored_classname); text(&w, p->instance);
        text(&w, p->classname); boolean(&w, p->map_defined);
    END;
    BEGIN("behaviors", v->behavior_count);
        const qa_launch_weapon_behavior *p = &v->behaviors[i]; scope(&w, registry, p->scope);
        text(&w, p->weapon); text(&w, p->instance); text(&w, p->behavior); number(&w, p->role); boolean(&w, p->enabled);
    END;
    BEGIN("instances", qa_launch_snapshot_instance_count(snapshot));
        const qa_launch_instance *p = qa_launch_snapshot_instance(snapshot, i);
        text(&w, p->selection.instance); digest(&w, &p->identity); word(&w, p->roles);
        qa_json_writer_array(&w);
        for (size_t j = 0; j < p->interface_count; ++j) resource_write(&w, catalog, &p->interfaces[j]);
        qa_json_writer_end(&w);
        qa_json_writer_array(&w);
        for (size_t j = 0; j < p->behavior_count; ++j) {
            const qa_catalog_weapon_behavior *b = p->behaviors[j];
            qa_json_writer_array(&w); product(&w, catalog, b->product); text(&w, b->id); number(&w, b->runtime);
            number(&w, b->role); text(&w, b->artifact_path); digest(&w, &b->artifact_digest);
            text(&w, b->declaration_path); digest(&w, &b->declaration_digest); binary(&w, b->entry); qa_json_writer_end(&w);
        }
        qa_json_writer_end(&w);
    END;
    qa_json_writer_key(&w, "resources"); qa_json_writer_array(&w);
    for (size_t i = 0; i < qa_launch_snapshot_resource_count(snapshot); ++i)
        resource_write(&w, catalog, qa_launch_snapshot_resource(snapshot, i));
    qa_json_writer_end(&w); qa_json_writer_end(&w);
#undef BEGIN
#undef END
    qa_buffer encoded = {0}; bool ok = qa_json_writer_finish(&w, &encoded, error);
    qa_json_writer_destroy(&w);
    if (ok && encoded.size > IDENTITY_MAX_BYTES) { qa_buffer_free(&encoded); return fail(error, "Launch identity exceeds portable bound"); }
    if (ok) *out = encoded;
    return ok;
}

typedef struct identity_reader {
    const qa_json_document *document;
    qa_json_id array;
    size_t next;
    qa_catalog *catalog;
    const qa_actor_registry *registry;
    qa_arena *arena;
    qa_error *error;
    bool failed;
} identity_reader;
static qa_json_id take(identity_reader *r)
{
    qa_json_id id = qa_json_at(r->document, r->array, r->next++);
    if (id == QA_JSON_NONE && !r->failed) { fail(r->error, "Truncated explicit launch record"); r->failed = true; }
    return id;
}
static bool record(identity_reader *r, qa_json_id id, size_t count)
{
    if (qa_json_type(r->document, id) != QA_JSON_ARRAY || qa_json_size(r->document, id) != count) {
        r->failed = true; return fail(r->error, "Launch record has an unexpected field count");
    }
    r->array = id; r->next = 0; return true;
}
static const char *read_text(identity_reader *r)
{
    qa_buffer b = {0};
    if (r->failed || !qa_json_string(r->document, take(r), &b, r->error)) { r->failed = true; return ""; }
    if (memchr(b.data, 0, b.size)) { qa_buffer_free(&b); r->failed = true; fail(r->error, "Launch text contains embedded NUL"); return ""; }
    char *copy = qa_arena_alloc(r->arena, b.size + 1, 1, r->error);
    if (!copy) { qa_buffer_free(&b); r->failed = true; return ""; }
    memcpy(copy, b.data, b.size); copy[b.size] = 0; qa_buffer_free(&b); return copy;
}
static uint64_t read_word(identity_reader *r)
{
    const char *s = read_text(r); uint64_t n = 0;
    if (!*s || (s[0] == '0' && s[1])) { r->failed = true; fail(r->error, "Noncanonical launch unsigned word"); return 0; }
    for (; *s; ++s) {
        if (*s < '0' || *s > '9' || n > (UINT64_MAX - (unsigned)(*s - '0')) / 10) {
            r->failed = true; fail(r->error, "Launch unsigned word exceeds uint64 range"); return 0;
        }
        n = n * 10 + (unsigned)(*s - '0');
    }
    return n;
}
static const char *read_optional_text(identity_reader *r)
{
    if (qa_json_type(r->document, qa_json_at(r->document, r->array, r->next)) == QA_JSON_NULL) {
        (void)take(r); return NULL;
    }
    return read_text(r);
}
static int32_t read_signed(identity_reader *r)
{
    int64_t n = 0;
    if (r->failed || !qa_json_i64(r->document, take(r), &n, r->error) || n < INT32_MIN || n > INT32_MAX) {
        r->failed = true; fail(r->error, "Launch integer exceeds int32 range"); return 0;
    }
    return (int32_t)n;
}
static uint32_t read_unsigned(identity_reader *r)
{
    uint64_t n = 0;
    if (r->failed || !qa_json_u64(r->document, take(r), &n, r->error) || n > UINT32_MAX) {
        r->failed = true; fail(r->error, "Launch integer exceeds uint32 range"); return 0;
    }
    return (uint32_t)n;
}
static float read_float(identity_reader *r)
{
    double n = 0;
    if (r->failed || !qa_json_number(r->document, take(r), &n, r->error) || !isfinite(n) || n < -FLT_MAX || n > FLT_MAX) {
        r->failed = true; fail(r->error, "Invalid launch float"); return 0;
    }
    return (float)n;
}
static bool read_bool(identity_reader *r)
{
    bool b = false;
    if (r->failed || !qa_json_bool(r->document, take(r), &b, r->error)) r->failed = true;
    return b;
}
static qa_product_id read_product(identity_reader *r)
{
    const char *name = read_text(r); if (!*name) return 0;
    const qa_product *p = qa_catalog_find(r->catalog, name);
    if (!p || strcmp(name, p->identity)) { r->failed = true; fail(r->error, "Saved launch product is absent from candidate catalog"); return 0; }
    return p->id;
}
static qa_bytes read_binary(identity_reader *r)
{
    const char *s = read_text(r); size_t n = strlen(s);
    if (n & 1) { r->failed = true; fail(r->error, "Odd launch option hex length"); return (qa_bytes){0}; }
    uint8_t *data = n ? qa_arena_alloc(r->arena, n / 2, 1, r->error) : NULL;
    if (n && !data) { r->failed = true; return (qa_bytes){0}; }
    for (size_t i = 0; i < n; ++i) {
        unsigned x = s[i] >= '0' && s[i] <= '9' ? (unsigned)(s[i] - '0') :
            s[i] >= 'a' && s[i] <= 'f' ? (unsigned)(s[i] - 'a' + 10) : 16;
        if (x == 16) { r->failed = true; fail(r->error, "Noncanonical launch option hex"); return (qa_bytes){0}; }
        if (!(i & 1)) data[i / 2] = (uint8_t)(x << 4); else data[i / 2] |= (uint8_t)x;
    }
    return (qa_bytes){data, n / 2};
}
static qa_actor_id read_actor(identity_reader *r)
{
    qa_json_id id = take(r); if (qa_json_type(r->document, id) == QA_JSON_NULL) return (qa_actor_id){0};
    identity_reader a = *r; if (!record(&a, id, 2)) { r->failed = true; return (qa_actor_id){0}; }
    qa_saved_actor_id saved = {.slot = read_unsigned(&a)}; saved.generation = read_word(&a);
    qa_actor_id found = {0};
    if (a.failed || !a.registry || !qa_actors_reference_saved(a.registry, saved, true, &found, a.error)) {
        r->failed = true; fail(r->error, "Saved launch actor provenance is absent from restored candidate registry");
        return (qa_actor_id){0};
    }
    return found;
}
static qa_launch_scope read_scope(identity_reader *r)
{
    identity_reader s = *r; qa_launch_scope out = {0};
    if (!record(&s, take(r), 3)) { r->failed = true; return out; }
    out.kind = read_unsigned(&s); out.actor = read_actor(&s); out.seat = read_unsigned(&s);
    if ((unsigned)out.kind > QA_SCOPE_SEAT ||
        (out.kind == QA_SCOPE_ACTOR ? !out.actor.registry : out.actor.registry != 0) ||
        (out.kind != QA_SCOPE_SEAT && out.seat)) {
        s.failed = true; fail(r->error, "Invalid explicit launch scope");
    }
    r->failed |= s.failed; return out;
}
static qa_clock_config read_clock(identity_reader *r)
{
    identity_reader s = *r; qa_clock_config v = {0};
    if (!record(&s, take(r), 7)) { r->failed = true; return v; }
    v.kind = read_unsigned(&s); v.initial_time_ns = read_word(&s); v.interval_ns = read_word(&s);
    v.minimum_frame_ns = read_word(&s); v.maximum_frame_ns = read_word(&s); v.initial_lead_ns = read_word(&s);
    v.maximum_steps = read_unsigned(&s); r->failed |= s.failed; return v;
}
static qa_mode_rules read_rules(identity_reader *r)
{
    identity_reader s = *r; qa_mode_rules v = {0};
    if (!record(&s, take(r), 39)) { r->failed = true; return v; }
#define U(member) v.member = read_unsigned(&s)
#define I(member) v.member = read_signed(&s)
#define F(member) v.member = read_float(&s)
#define B(member) v.member = read_bool(&s)
#define W(member) v.member = read_word(&s)
    U(source); U(kind); U(teams[0]); U(teams[1]); U(teams[2]); U(forced_team);
    I(frag_limit); I(capture_limit); I(warmup_seconds); I(competition); I(setup_seconds);
    I(countdown_seconds); I(match_seconds); I(max_game_players); I(election_percent);
    I(teamplay); I(rune_mask); I(vote_limit); U(flags); U(referee_flags);
    F(time_limit_minutes); F(obelisk_health); F(obelisk_regen); W(obelisk_regen_ns); W(obelisk_respawn_ns);
    B(enabled); B(friendly_fire); B(force_join); B(match_lock); B(paused); B(auto_lock);
    B(relics); B(single_player_active); B(tournament_restart); B(q2_rerelease); B(start_map);
    B(force_balance); B(voting_disabled); B(rogue_deathmatch);
#undef U
#undef I
#undef F
#undef B
#undef W
    r->failed |= s.failed; return v;
}
static bool metadata_resource(identity_reader *r, qa_json_id id)
{
    identity_reader s = *r; qa_sha256_digest value;
    if (!record(&s, id, 4)) { r->failed = true; return false; }
    (void)read_product(&s); (void)read_text(&s); const char *hex = read_text(&s);
    if (strlen(hex) != 64 || !qa_sha256_parse(hex, &value, s.error)) s.failed = true;
    (void)read_word(&s); r->failed |= s.failed; return !r->failed;
}
static bool metadata(identity_reader *r, qa_json_id root)
{
    qa_json_id resources = qa_json_get(r->document, root, "resources"), instances = qa_json_get(r->document, root, "instances");
    if (qa_json_type(r->document, resources) != QA_JSON_ARRAY || qa_json_type(r->document, instances) != QA_JSON_ARRAY ||
        qa_json_size(r->document, resources) > IDENTITY_MAX_RECORDS || qa_json_size(r->document, instances) > IDENTITY_MAX_RECORDS)
        return fail(r->error, "Invalid launch content manifest bounds");
    for (size_t i = 0; i < qa_json_size(r->document, resources); ++i)
        if (!metadata_resource(r, qa_json_at(r->document, resources, i))) return false;
    for (size_t i = 0; i < qa_json_size(r->document, instances); ++i) {
        identity_reader s = *r; qa_sha256_digest value;
        if (!record(&s, qa_json_at(r->document, instances, i), 5)) return false;
        (void)read_text(&s); const char *hex = read_text(&s);
        if (strlen(hex) != 64 || !qa_sha256_parse(hex, &value, s.error)) return false;
        (void)read_word(&s); qa_json_id interfaces = take(&s), behaviors = take(&s);
        if (qa_json_type(s.document, interfaces) != QA_JSON_ARRAY || qa_json_type(s.document, behaviors) != QA_JSON_ARRAY ||
            qa_json_size(s.document, interfaces) > IDENTITY_MAX_RECORDS || qa_json_size(s.document, behaviors) > IDENTITY_MAX_RECORDS)
            return fail(r->error, "Invalid instance manifest bounds");
        for (size_t j = 0; j < qa_json_size(s.document, interfaces); ++j)
            if (!metadata_resource(&s, qa_json_at(s.document, interfaces, j))) return false;
        for (size_t j = 0; j < qa_json_size(s.document, behaviors); ++j) {
            identity_reader b = s;
            if (!record(&b, qa_json_at(s.document, behaviors, j), 9)) return false;
            (void)read_product(&b); (void)read_text(&b); (void)read_unsigned(&b); (void)read_unsigned(&b);
            (void)read_text(&b); hex = read_text(&b);
            if (strlen(hex) != 64 || !qa_sha256_parse(hex, &value, b.error)) return false;
            (void)read_text(&b); hex = read_text(&b);
            if (strlen(hex) != 64 || !qa_sha256_parse(hex, &value, b.error)) return false;
            (void)read_binary(&b); if (b.failed) return false;
        }
        if (s.failed) return false;
    }
    return !r->failed;
}
bool qa_launch_identity_decode(qa_catalog *catalog, const qa_actor_registry *registry, qa_bytes bytes,
    qa_launch_draft **out, qa_error *error)
{
    if (!catalog || !out || bytes.size > IDENTITY_MAX_BYTES) return fail(error, "Invalid portable launch identity input");
    qa_json_document *document = NULL; if (!qa_json_parse(bytes, &document, error)) return false;
    qa_json_id root = qa_json_root(document); uint64_t version = 0;
    if (qa_json_type(document, root) != QA_JSON_OBJECT || qa_json_size(document, root) != 14 ||
        !qa_json_string_equal(document, qa_json_get(document, root, "schema"), "qa-launch-identity") ||
        !qa_json_u64(document, qa_json_get(document, root, "version"), &version, error) || version != 5) {
        qa_json_destroy(document); return fail(error, "Unsupported explicit launch identity schema");
    }
    qa_arena arena = {0}; qa_launch_draft *draft = NULL;
    identity_reader r = {.document = document, .catalog = catalog, .registry = registry, .arena = &arena, .error = error};
    bool ok = qa_launch_draft_create_empty(catalog, &draft, error) && record(&r, qa_json_get(document, root, "world"), 14);
    if (ok) {
        qa_launch_world v = {0}; v.preset = read_product(&r); v.geometry = read_product(&r); v.presentation = read_product(&r);
        v.map = read_text(&r); v.start_command = read_text(&r); v.explicit_presentation = read_bool(&r);
        v.doppler = read_bool(&r); v.campaign = read_bool(&r); v.environment = read_unsigned(&r);
        v.environment_product = read_product(&r); v.environment_path = read_text(&r); v.skill = read_signed(&r);
        v.explicit_spawn_point = read_bool(&r); v.spawn_point = read_text(&r);
        ok = !r.failed && qa_launch_set_world(draft, &v, error);
    }
#define EACH(name, fields) do { qa_json_id array = qa_json_get(document, root, name); \
    size_t count = qa_json_size(document, array); \
    if (qa_json_type(document, array) != QA_JSON_ARRAY || count > IDENTITY_MAX_RECORDS) { ok = fail(error, "Invalid launch choice array"); break; } \
    for (size_t i = 0; ok && i < count; ++i) { if (!record(&r, qa_json_at(document, array, i), fields)) { ok = false; break; }
#define DONE(setter) ok = !r.failed && (setter); } } while (0)
    if (ok) EACH("providers", 8)
        qa_launch_provider v = {0}; v.instance = read_text(&r); v.product = read_product(&r); v.runtime = read_unsigned(&r);
        v.implementation = read_text(&r); v.artifact = read_text(&r); v.component = read_text(&r);
        v.clock = read_clock(&r); v.options = read_binary(&r);
        DONE(qa_launch_set_provider(draft, &v, error));
    if (ok) EACH("bindings", 5)
        qa_launch_binding v = {0}; v.scope = read_scope(&r); v.role = read_unsigned(&r);
        v.selector = read_text(&r); v.instance = read_text(&r); v.definition = read_text(&r);
        DONE(qa_launch_bind(draft, &v, error));
    if (ok) EACH("mods", 3)
        qa_launch_mod_selection v = {0}; v.instance = read_text(&r); v.component = read_text(&r); v.enabled = read_bool(&r);
        DONE(qa_launch_set_mod(draft, &v, error));
    if (ok) EACH("modes", 7)
        qa_launch_mode v = {0}; v.instance = read_text(&r); v.rules = read_rules(&r);
        for (size_t j = 0; j < 3; ++j) v.teams[j] = read_text(&r);
        v.forced_team = read_text(&r); v.primary_score = read_bool(&r);
        DONE(qa_launch_set_mode(draft, &v, error));
    if (ok) EACH("equipment", 13)
        qa_launch_equipment v = {0}; v.scope = read_scope(&r); v.instance = read_text(&r);
        v.grapple_source = read_text(&r); v.grenade_source = read_text(&r);
        v.selection.grapple = read_unsigned(&r); v.selection.binding = read_unsigned(&r);
        v.selection.retain_on_weapon_change = read_bool(&r); v.selection.release_on_jump = read_bool(&r);
        v.selection.release_on_teleport = read_bool(&r); v.selection.grenades.enabled = read_bool(&r);
        v.selection.grenades.infinite_ammo = read_bool(&r); v.selection.grenades.initial_ammo = read_signed(&r);
        v.selection.grenades.capacity = read_signed(&r);
        DONE(qa_launch_set_equipment(draft, &v, error));
    if (ok) EACH("seats", 15)
        qa_launch_seat v = {0}; v.id = read_unsigned(&r); v.actor = read_actor(&r); v.name = read_text(&r);
        v.team = read_text(&r); v.input_device = read_unsigned(&r); v.local = read_bool(&r);
        v.spectator = read_bool(&r); v.bot = read_bool(&r); v.bot_skill = read_float(&r);
        if (qa_json_type(document, qa_json_at(document, r.array, r.next)) == QA_JSON_NULL)
            (void)take(&r);
        else v.bot_definition = read_text(&r);
        v.bot_delay_ms = read_signed(&r);
        v.character_model = read_optional_text(&r); v.character_skin = read_optional_text(&r);
        v.character_head_model = read_optional_text(&r); v.character_head_skin = read_optional_text(&r);
        DONE(qa_launch_set_seat(draft, &v, error));
    if (ok) EACH("loadout", 6)
        qa_launch_loadout v = {0}; v.scope = read_scope(&r); v.item = read_text(&r); v.quantity = read_signed(&r);
        v.capacity = read_signed(&r); v.override_capacity = read_bool(&r); v.drop_on_death = read_bool(&r);
        DONE(qa_launch_set_loadout(draft, &v, error));
    if (ok) EACH("monsters", 4)
        qa_launch_monster v = {0}; v.authored_classname = read_text(&r); v.instance = read_text(&r);
        v.classname = read_text(&r); v.map_defined = read_bool(&r);
        DONE(qa_launch_set_monster(draft, &v, error));
    if (ok) EACH("behaviors", 6)
        qa_launch_weapon_behavior v = {0}; v.scope = read_scope(&r); v.weapon = read_text(&r);
        v.instance = read_text(&r); v.behavior = read_text(&r); v.role = read_unsigned(&r); v.enabled = read_bool(&r);
        DONE(qa_launch_set_weapon_behavior(draft, &v, error));
#undef EACH
#undef DONE
    if (ok) {
        const qa_launch_choices *choices = qa_launch_draft_choices(draft);
        const char *names[] = {"providers", "bindings", "mods", "modes", "equipment", "seats", "loadout", "monsters", "behaviors"};
        const size_t counts[] = {choices->provider_count, choices->binding_count, choices->mod_count,
            choices->mode_count, choices->equipment_count, choices->seat_count, choices->loadout_count,
            choices->monster_count, choices->behavior_count};
        for (size_t i = 0; ok && i < sizeof(counts) / sizeof(*counts); ++i)
            if (counts[i] != qa_json_size(document, qa_json_get(document, root, names[i])))
                ok = fail(error, "Portable launch identity contains duplicate selection keys");
    }
    if (ok) ok = metadata(&r, root) && qa_launch_validate(draft, error);
    qa_arena_destroy(&arena); qa_json_destroy(document);
    if (!ok) { qa_launch_draft_destroy(draft); return false; }
    *out = draft; return true;
}
bool qa_launch_identity_match(const qa_launch_snapshot *snapshot, const qa_actor_registry *registry,
    qa_bytes bytes, qa_error *error)
{
    qa_buffer current = {0}; if (!qa_launch_identity_encode(snapshot, registry, &current, error)) return false;
    bool equal = current.size == bytes.size && (!bytes.size || (bytes.data && !memcmp(current.data, bytes.data, bytes.size)));
    qa_buffer_free(&current); return equal || fail(error, "Prepared launch differs from exact canonical content/choice identity");
}
