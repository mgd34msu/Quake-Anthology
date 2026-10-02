#include "guest_native_q2_private.h"
#include "guest_native_q2_private_state.h"
#include "qa/source_save.h"
#include "qa/json.h"
#include <math.h>

enum { PRIVATE_LAYOUTS = 128, PRIVATE_FIELDS = 1024, PRIVATE_RECORDS = 4096,
    PRIVATE_BYTES = 8 * 1024 * 1024, PRIVATE_REFS = 65536 };
typedef enum private_domain { PRIVATE_CLIENT, PRIVATE_ENTITY, PRIVATE_IMAGE, PRIVATE_HEAP } private_domain;
typedef enum private_storage { PRIVATE_SCALAR, PRIVATE_BOOL, PRIVATE_F32, PRIVATE_F64,
    PRIVATE_POINTER, PRIVATE_RECONSTRUCTED } private_storage;
enum { REF_NULL, REF_ENTITY, REF_CLIENT, REF_IMAGE, REF_OBJECT };
typedef struct private_field {
    qa_json_id name, target, type;
    qa_buffer name_text;
    uint32_t offset, bytes, width;
    private_storage storage;
    bool image_alternative, image_record;
} private_field;
typedef struct private_layout {
    qa_json_id name;
    qa_buffer name_text;
    private_domain domain;
    uint64_t rva;
    uint32_t bytes, field_count, pointer_count, image_count;
    int32_t tag;
    private_field *fields;
} private_layout;
typedef struct private_profile {
    qa_json_document *document;
    private_layout layouts[PRIVATE_LAYOUTS];
    uint32_t count, client_layout, entity_layout, client_pointer, client_bytes, entity_bytes;
} private_profile;
typedef struct private_ref { uint32_t kind; uint64_t value, offset; } private_ref;
typedef struct private_record {
    uint32_t layout, source;
    uint64_t count;
    qa_buffer data;
    private_ref *refs;
    size_t ref_count;
    qa_native_address capture_base;
} private_record;
struct application_q2_private_state {
    private_profile profile;
    private_record *records;
    uint32_t count;
    size_t bytes, refs;
    qa_native_address clients[257];
    qa_native_entity_table table;
    bool applied;
};
static bool word(const qa_json_document *doc, qa_json_id object, const char *key,
    uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(doc, qa_json_get(doc, object, key), &value, error)) return false;
    if (value > UINT32_MAX) {
        application_fail(error, QA_ERROR_FORMAT, "Native private layout word overflows");
        return false;
    }
    *out = (uint32_t)value; return true;
}
static bool valid_name(const qa_json_document *doc, qa_json_id name, qa_buffer *text, qa_error *error)
{
    if (!qa_json_string(doc, name, text, error)) return false;
    bool ok = text->size && text->size <= 256 && !memchr(text->data, 0, text->size);
    return ok || application_fail(error, QA_ERROR_FORMAT, "Native private identity requires nonempty source text");
}
static bool named(const private_profile *p, qa_json_id name, const char *text)
{ return qa_json_string_equal(p->document, name, text); }
static uint32_t layout_named(const private_profile *p, qa_json_id name)
{
    for (uint32_t i = 0; i < p->count; ++i)
        if (p->layouts[i].name_text.data && qa_json_string_equal(p->document, name, (const char *)p->layouts[i].name_text.data)) return i;
    return UINT32_MAX;
}
static void profile_free(private_profile *p)
{
    for (uint32_t i = 0; i < p->count; ++i) {
        if (p->layouts[i].fields) for (uint32_t j = 0; j < p->layouts[i].field_count; ++j)
            qa_buffer_free(&p->layouts[i].fields[j].name_text);
        free(p->layouts[i].fields); qa_buffer_free(&p->layouts[i].name_text);
    }
    qa_json_destroy(p->document); memset(p, 0, sizeof(*p));
}
void application_q2_private_free(struct application_q2_private_state *state)
{
    if (!state) return;
    for (uint32_t i = 0; i < state->count; ++i) { qa_buffer_free(&state->records[i].data); free(state->records[i].refs); }
    free(state->records); profile_free(&state->profile); free(state);
}
static bool scalar_type(const private_profile *p, qa_json_id field, private_field *out, qa_error *error)
{
    const qa_json_document *doc = p->document;
    qa_json_id storage = qa_json_get(doc, field, "storage"); out->type = storage;
    out->storage = PRIVATE_SCALAR;
    out->width = named(p, storage, "uint8") || named(p, storage, "int8") ? 1 :
        named(p, storage, "uint16") || named(p, storage, "int16") ? 2 :
        named(p, storage, "uint32") || named(p, storage, "int32") ? 4 :
        named(p, storage, "uint64") || named(p, storage, "int64") ? 8 : 0;
    if (named(p, storage, "bool8")) { out->width = 1; out->storage = PRIVATE_BOOL; }
    if (named(p, storage, "float32")) { out->width = 4; out->storage = PRIVATE_F32; }
    if (named(p, storage, "float64")) { out->width = 8; out->storage = PRIVATE_F64; }
    if (named(p, storage, "bytes")) {
        bool pointer_free = false;
        if (!qa_json_bool(doc, qa_json_get(doc, field, "pointerFree"), &pointer_free, error) || !pointer_free)
            return application_fail(error, QA_ERROR_FORMAT, "Native byte span lacks original pointer-free layout qualification");
        out->width = 1;
    }
    if (named(p, storage, "pointer")) {
        out->width = 8; out->storage = PRIVATE_POINTER; out->target = qa_json_get(doc, field, "target");
        if (qa_json_type(doc, out->target) == QA_JSON_OBJECT) {
            out->image_record = qa_json_string_equal(doc, qa_json_get(doc, out->target, "kind"), "image-record");
            if (!out->image_record && !qa_json_string_equal(doc, qa_json_get(doc, out->target, "kind"), "image-or-heap"))
                return application_fail(error, QA_ERROR_FORMAT, "Native private pointer union has no original relocation kind");
            out->image_alternative = !out->image_record; out->target = qa_json_get(doc, out->target, "layout");
        }
    }
    if (named(p, storage, "reconstructed")) {
        if (!qa_json_string_equal(doc, qa_json_get(doc, field, "producer"), "original-init"))
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Native reconstructed span lacks its actual original Init producer");
        out->width = 1; out->storage = PRIVATE_RECONSTRUCTED;
    }
    uint32_t count;
    if (!out->width || !word(doc, field, "count", &count, error) || !count || count > PRIVATE_BYTES / out->width)
        return application_fail(error, QA_ERROR_FORMAT, "Native private field changes its declared original representation");
    out->bytes = count * out->width; return true;
}
static bool source_client_coverage(const private_profile *p, qa_error *error)
{
    /* These original live members are absent from the KEX JSON save roster.
     * Names qualify source semantics; offsets are supplied by the artifact. */
    static const char *const required[] = {
        "ping", "pers.connected", "pers.spawned", "pers.autoswitch", "pers.autoshield", "pers.selected_item_time", "pers.bob_skip", "pers.fog_transition_time",
        "resp.coop_respawn.autoswitch", "resp.coop_respawn.autoshield", "resp.coop_respawn.connected", "resp.coop_respawn.spawned",
        "resp.coop_respawn.selected_item_time", "resp.coop_respawn.bob_skip", "resp.coop_respawn.fog_transition_time",
        "resp.ctf_team", "resp.ctf_state", "resp.ctf_lasthurtcarrier", "resp.ctf_lastreturnedflag", "resp.ctf_flagsince",
        "resp.ctf_lastfraggedcarrier", "resp.id_state", "resp.lastidtime", "resp.voted", "resp.ready", "resp.admin", "resp.ghost",
        "old_pmove", "showscores", "showeou", "showinventory", "showhelp", "buttons", "oldbuttons", "latched_buttons", "cmd",
        "weapon_fire_finished", "weapon_think_time", "weapon_fire_buffered", "weapon_thunk",
        "damage_armor", "damage_parmor", "damage_blood", "damage_knockback", "damage_from", "damage_indicators", "num_damage_indicators",
        "kick_origin", "v_forward", "flash_time", "anim_time", "flood_locktill", "flood_when", "flood_whenhead",
        "chase_target", "update_chase", "owned_sphere", "inmenu", "menu", "menutime", "menudirty", "ctf_grapple",
        "ctf_grapplestate", "ctf_grapplereleasetime", "ctf_regentime", "ctf_techsndtime", "ctf_lasttechmsg", "no_weapon_chains",
        "chase_msg_time", "menu_sign", "coop_respawn_state", "last_damage_time", "num_lag_origins", "next_lag_origin",
        "is_lag_compensated", "lag_restore_origin", "slow_view_angles", "slow_view_angle_time", "help_draw_points", "help_draw_index",
        "help_draw_count", "help_draw_time", "step_frame", "help_poi_image", "help_poi_location", "awaiting_respawn", "respawn_timeout",
        "fog", "heightfog", "last_attacker_time"
    };
    const private_layout *layout = &p->layouts[p->client_layout];
    for (size_t i = 0; i < sizeof(required) / sizeof(*required); ++i) {
        bool found = false;
        for (uint32_t field = 0; field < layout->field_count; ++field)
            if (named(p, layout->fields[field].name, required[i]) && layout->fields[field].storage != PRIVATE_RECONSTRUCTED) found = true;
        if (!found) return application_fail(error, QA_ERROR_UNSUPPORTED, "Native private client layout omits an original unsaved live member");
    }
    static const char *const pointers[] = {"resp.ghost", "chase_target", "owned_sphere", "menu", "ctf_grapple",
        "pers.weapon", "pers.lastweapon", "resp.coop_respawn.weapon", "resp.coop_respawn.lastweapon", "newweapon", "oldgroundentity",
        "trail_head", "trail_tail", "landmark_name", "sight_entity", "sound_entity", "sound2_entity"};
    for (size_t i = 0; i < sizeof(pointers) / sizeof(*pointers); ++i) {
        bool found = false;
        for (uint32_t field = 0; field < layout->field_count; ++field)
            if (named(p, layout->fields[field].name, pointers[i])) {
                if (layout->fields[field].storage != PRIVATE_POINTER || layout->fields[field].bytes != 8)
                    return application_fail(error, QA_ERROR_FORMAT, "Native live client pointer lacks relocation qualification");
                const private_field *pointer = &layout->fields[field];
                bool entity = !strcmp(pointers[i], "chase_target") || !strcmp(pointers[i], "owned_sphere") || !strcmp(pointers[i], "ctf_grapple") ||
                    !strcmp(pointers[i], "oldgroundentity") || !strcmp(pointers[i], "trail_head") || !strcmp(pointers[i], "trail_tail") ||
                    !strcmp(pointers[i], "sight_entity") || !strcmp(pointers[i], "sound_entity") || !strcmp(pointers[i], "sound2_entity");
                bool ghost = !strcmp(pointers[i], "resp.ghost");
                bool image = strstr(pointers[i], "weapon") != NULL;
                if ((entity && (!named(p, pointer->target, "entity") || pointer->image_alternative)) ||
                    (image && (!named(p, pointer->target, "image") || pointer->image_alternative || pointer->image_record)) ||
                    (ghost && (!pointer->image_record || !named(p, pointer->target, "ctfgame.ghosts"))) ||
                    (!strcmp(pointers[i], "menu") && (pointer->image_record || pointer->image_alternative || !named(p, pointer->target, "menu.handle"))))
                    return application_fail(error, QA_ERROR_FORMAT, "Native original client pointer changes its source address domain");
                found = true;
            }
        if (!found) return application_fail(error, QA_ERROR_UNSUPPORTED, "Native full client layout omits an original pointer member");
    }
    const qa_json_document *doc = p->document;
    qa_json_id clients = qa_json_get(doc, qa_json_get(doc, qa_json_root(doc), "continuation"), "clients");
    const char *connection_names[] = {"pers.connected", "pers.spawned"}, *connection_keys[] = {"connected", "spawned"};
    for (size_t i = 0; i < 2; ++i) {
        uint32_t offset;
        if (!word(doc, qa_json_get(doc, clients, connection_keys[i]), "offset", &offset, error)) return false;
        bool found = false;
        for (uint32_t j = 0; j < layout->field_count; ++j) if (named(p, layout->fields[j].name, connection_names[i])) {
            const private_field *field = &layout->fields[j];
            if (field->offset != offset || field->storage != PRIVATE_BOOL || field->bytes != 1)
                return application_fail(error, QA_ERROR_FORMAT, "Native complete client layout changes its qualified connection fields");
            found = true;
        }
        if (!found) return false;
    }
    return true;
}
static const private_layout *source_layout(const private_profile *p, const char *name)
{
    for (uint32_t i = 0; i < p->count; ++i) if (named(p, p->layouts[i].name, name)) return &p->layouts[i];
    return NULL;
}
static const private_field *source_field(const private_profile *p, const private_layout *layout, const char *name)
{
    if (layout) for (uint32_t i = 0; i < layout->field_count; ++i)
        if (named(p, layout->fields[i].name, name)) return &layout->fields[i];
    return NULL;
}
static bool source_scalar(const private_profile *p, const private_layout *layout,
    const char *name, const char *type, uint32_t bytes)
{
    const private_field *field = source_field(p, layout, name);
    return field && named(p, field->type, type) && field->bytes == bytes;
}
static bool source_pointer(const private_profile *p, const private_layout *layout,
    const char *name, const char *target)
{
    const private_field *field = source_field(p, layout, name);
    return field && field->storage == PRIVATE_POINTER && field->bytes == 8 &&
        !field->image_record && !field->image_alternative && named(p, field->target, target);
}
static bool source_object_coverage(const private_profile *p, qa_error *error)
{
    const private_layout *game = source_layout(p, "game.runtime"), *level = source_layout(p, "level.runtime"), *ctf = source_layout(p, "ctfgame"),
        *ghosts = source_layout(p, "ctfgame.ghosts"), *lag = source_layout(p, "lag.origin"),
        *menu = source_layout(p, "menu.handle"), *entries = source_layout(p, "menu.entry"),
        *settings = source_layout(p, "menu.admin-settings");
    /* Counts and leaf representations come from the original members and
     * allocation expressions; their physical offsets still come from the
     * exact artifact declaration. */
    if (!game || game->domain != PRIVATE_IMAGE || game->image_count != 1 ||
        !source_scalar(p, game, "maxclients", "uint32", 4) || !source_scalar(p, game, "maxentities", "uint32", 4) ||
        !source_scalar(p, game, "airacceleration_modified", "int32", 4) || !source_scalar(p, game, "gravity_modified", "int32", 4) ||
        !source_scalar(p, game, "max_lag_origins", "int32", 4) || !source_pointer(p, game, "clients", "client") ||
        !source_pointer(p, game, "lag_origins", "lag.origin") || !lag || lag->domain != PRIVATE_HEAP ||
        lag->tag != 765 || lag->bytes != 12 || !source_scalar(p, lag, "origin", "float32", 12))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native full private state lacks the original game/lag allocation producer");
    if (!level || level->domain != PRIVATE_IMAGE || level->image_count != 1 ||
        !source_scalar(p, level, "in_frame", "bool8", 1) || !source_scalar(p, level, "time", "int64", 8))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native full private state lacks the original complete level root");
    const private_layout *entities = &p->layouts[p->entity_layout];
    const private_layout *clients = &p->layouts[p->client_layout];
    if (!source_scalar(p, clients, "num_lag_origins", "uint8", 1) || !source_scalar(p, clients, "next_lag_origin", "uint8", 1) ||
        !source_scalar(p, clients, "num_damage_indicators", "uint8", 1))
        return application_fail(error, QA_ERROR_FORMAT, "Native private client counters change their original byte representation");
    const private_field *client = source_field(p, entities, "shared.client");
    if (!source_pointer(p, entities, "shared.client", "client") || client->offset != p->client_pointer ||
        !source_scalar(p, entities, "spawn_count", "int32", 4))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native complete entity layout differs from its original source/client identity");
    const char *entity_pointers[] = {"shared.owner", "target_ent", "goalentity", "movetarget", "chain", "enemy", "oldenemy", "activator", "groundentity",
        "teamchain", "teammaster", "mynoise", "mynoise2", "bad_area", "hint_chain", "monster_hint_chain", "target_hint_chain", "beam", "beam2", "proboscus", "disintegrator",
        "monsterinfo.goal_hint", "monsterinfo.badMedic1", "monsterinfo.badMedic2", "monsterinfo.healer", "monsterinfo.last_player_enemy", "monsterinfo.commander",
        "monsterinfo.damage_attacker", "monsterinfo.damage_inflictor"};
    const char *entry_pointers[] = {"prethink.value", "prethink.list", "postthink.value", "postthink.list", "think.value", "think.list",
        "touch.value", "touch.list", "use.value", "use.list", "pain.value", "pain.list", "die.value", "die.list", "item"};
    for (size_t i = 0; i < sizeof(entity_pointers) / sizeof(*entity_pointers); ++i)
        if (!source_pointer(p, entities, entity_pointers[i], "entity"))
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Native complete entity record omits an original entity pointer");
    for (size_t i = 0; i < sizeof(entry_pointers) / sizeof(*entry_pointers); ++i)
        if (!source_pointer(p, entities, entry_pointers[i], "image"))
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Native complete entity record omits an original entry/item pointer");
    const char *wrappers[] = {"moveinfo.endfunc", "moveinfo.blocked", "monsterinfo.active_move", "monsterinfo.next_move", "monsterinfo.stand", "monsterinfo.idle",
        "monsterinfo.search", "monsterinfo.walk", "monsterinfo.run", "monsterinfo.dodge", "monsterinfo.attack", "monsterinfo.melee", "monsterinfo.sight",
        "monsterinfo.checkattack", "monsterinfo.setskin", "monsterinfo.physics_change", "monsterinfo.blocked", "monsterinfo.duck", "monsterinfo.unduck", "monsterinfo.sidestep"};
    for (size_t i = 0; i < sizeof(wrappers) / sizeof(*wrappers); ++i) {
        char name[96];
        snprintf(name, sizeof(name), "%s.value", wrappers[i]);
        if (!source_pointer(p, entities, name, "image")) return application_fail(error, QA_ERROR_UNSUPPORTED, "Native complete entity record omits an original movement/AI value");
        snprintf(name, sizeof(name), "%s.list", wrappers[i]);
        if (!source_pointer(p, entities, name, "image")) return application_fail(error, QA_ERROR_UNSUPPORTED, "Native complete entity record omits an original movement/AI registry");
    }
    const private_field *curve = source_field(p, entities, "moveinfo.curve_positions.ptr");
    if (!curve || curve->storage != PRIVATE_POINTER || curve->bytes != 8 || curve->image_record || curve->image_alternative ||
        !source_scalar(p, entities, "moveinfo.curve_positions.count", "uint64", 8))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native complete entity record lacks original allocated mover-curve state");
    uint32_t curve_layout = layout_named(p, curve->target);
    if (curve_layout == UINT32_MAX || p->layouts[curve_layout].domain != PRIVATE_HEAP || p->layouts[curve_layout].tag != 766 ||
        p->layouts[curve_layout].bytes != 4 || !source_scalar(p, &p->layouts[curve_layout], "position", "float32", 4))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native mover curve lacks original float allocation layout");
    if (!ctf || ctf->domain != PRIVATE_IMAGE || ctf->image_count != 1 || !source_pointer(p, ctf, "etarget", "entity") ||
        !ghosts || ghosts->domain != PRIVATE_IMAGE || ghosts->image_count != 256 || ctf->rva + ctf->bytes != ghosts->rva ||
        !source_pointer(p, ghosts, "ent", "entity"))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native full private state lacks original CTF election/ghost state");
    static const char *const ctf_ints[] = {"team1", "team2", "total1", "total2", "last_capture_team", "match", "lasttime", "election", "evotes", "needvotes", "warnactive"};
    static const char *const ctf_times[] = {"last_flag_capture", "matchtime", "electtime"};
    static const char *const ghost_ints[] = {"number", "deaths", "kills", "caps", "basedef", "carrierdef", "code", "team", "score"};
    for (size_t i = 0; i < sizeof(ctf_ints) / sizeof(*ctf_ints); ++i)
        if (!source_scalar(p, ctf, ctf_ints[i], "int32", 4)) return application_fail(error, QA_ERROR_FORMAT, "Native CTF scalar changes its original type");
    for (size_t i = 0; i < sizeof(ctf_times) / sizeof(*ctf_times); ++i)
        if (!source_scalar(p, ctf, ctf_times[i], "int64", 8)) return application_fail(error, QA_ERROR_FORMAT, "Native CTF timer changes its original type");
    for (size_t i = 0; i < sizeof(ghost_ints) / sizeof(*ghost_ints); ++i)
        if (!source_scalar(p, ghosts, ghost_ints[i], "int32", 4)) return application_fail(error, QA_ERROR_FORMAT, "Native ghost scalar changes its original type");
    if (!source_scalar(p, ctf, "countdown", "bool8", 1) || !source_scalar(p, ctf, "elevel", "bytes", 32) ||
        !source_scalar(p, ctf, "emsg", "bytes", 256) || !source_scalar(p, ghosts, "netname", "bytes", 32))
        return application_fail(error, QA_ERROR_FORMAT, "Native CTF text/countdown changes its original type");
    if (!menu || menu->domain != PRIVATE_HEAP || menu->tag != 766 ||
        !source_pointer(p, menu, "entries", "menu.entry") || !source_pointer(p, menu, "UpdateFunc", "image") ||
        !source_scalar(p, menu, "cur", "int32", 4) || !source_scalar(p, menu, "num", "int32", 4) ||
        !entries || entries->domain != PRIVATE_HEAP || entries->tag != 766 ||
        !source_pointer(p, entries, "SelectFunc", "image") || !source_scalar(p, entries, "align", "int32", 4) ||
        !source_scalar(p, entries, "text", "bytes", 64) || !source_scalar(p, entries, "text_arg1", "bytes", 64))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native full private state lacks original menu allocation/callback state");
    if (!source_pointer(p, menu, "arg", "menu.admin-settings") || !settings || settings->domain != PRIVATE_HEAP || settings->tag != 766)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native menu argument needs its actual tagged object layout");
    const char *setting_ints[] = {"matchlen", "matchsetuplen", "matchstartlen"};
    const char *setting_bools[] = {"weaponsstay", "instantitems", "quaddrop", "instantweap", "matchlock"};
    for (size_t i = 0; i < sizeof(setting_ints) / sizeof(*setting_ints); ++i)
        if (!source_scalar(p, settings, setting_ints[i], "int32", 4)) return application_fail(error, QA_ERROR_FORMAT, "Native menu setting changes its original type");
    for (size_t i = 0; i < sizeof(setting_bools) / sizeof(*setting_bools); ++i)
        if (!source_scalar(p, settings, setting_bools[i], "bool8", 1)) return application_fail(error, QA_ERROR_FORMAT, "Native menu setting changes its original bool type");
    return true;
}
static bool profile_read(struct application_native_q2 *engine, private_profile *p, qa_error *error)
{
    p->client_layout = p->entity_layout = UINT32_MAX;
    qa_native_target target = qa_native_module_describe(engine->provider->state.native.module).image.target;
    if (engine->profile != QA_NATIVE_Q2_GAME_API2023 || target.pointer_bytes != 8 || target.arch != QA_NATIVE_ARCH_X86_64 || !engine->declaration)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native live object supplement requires its qualified KEX64 original source artifact");
    if (!qa_json_parse(qa_native_declaration_primary(engine->declaration), &p->document, error)) return false;
    const qa_json_document *doc = p->document; qa_json_id root = qa_json_root(doc);
    qa_json_id section = qa_json_get(doc, qa_json_get(doc, root, "continuation"), "private"), layouts = qa_json_get(doc, section, "layouts");
    uint32_t schema;
    if (qa_json_type(doc, section) != QA_JSON_OBJECT || !word(doc, section, "schema", &schema, error) || schema != 1 ||
        !qa_json_string_equal(doc, qa_json_get(doc, section, "coverage"), "kex-live-client-and-declared-globals") ||
        !qa_json_string_equal(doc, qa_json_get(doc, section, "moduleCoverage"), "original-complete") ||
        qa_json_type(doc, layouts) != QA_JSON_ARRAY || !qa_json_size(doc, layouts) || qa_json_size(doc, layouts) > PRIVATE_LAYOUTS)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native private continuation lacks complete source-qualified live client/object layouts");
    qa_json_id world = qa_json_get(doc, root, "world"), edict = qa_json_get(doc, world, "edict"), client = qa_json_get(doc, qa_json_get(doc, world, "client"), "layout");
    if (!word(doc, edict, "byteLength", &p->entity_bytes, error) || !word(doc, client, "byteLength", &p->client_bytes, error)) return false;
    bool client_pointer = false;
    qa_json_id fields = qa_json_get(doc, edict, "fields");
    for (size_t i = 0; i < qa_json_size(doc, fields); ++i) {
        qa_json_id field = qa_json_at(doc, fields, i);
        if (!qa_json_string_equal(doc, qa_json_get(doc, field, "name"), "shared.client")) continue;
        uint32_t count;
        if (client_pointer || !qa_json_string_equal(doc, qa_json_get(doc, field, "storage"), "pointer") ||
            !word(doc, field, "count", &count, error) || count != 1 || !word(doc, field, "offset", &p->client_pointer, error) ||
            p->client_pointer > p->entity_bytes || 8 > p->entity_bytes - p->client_pointer) return false;
        client_pointer = true;
    }
    if (!client_pointer) return application_fail(error, QA_ERROR_FORMAT, "Native private client root lacks its real source pointer");
    p->count = (uint32_t)qa_json_size(doc, layouts);
    for (uint32_t i = 0; i < p->count; ++i) {
        qa_json_id object = qa_json_at(doc, layouts, i); private_layout *layout = &p->layouts[i];
        layout->name = qa_json_get(doc, object, "name");
        if (!valid_name(doc, layout->name, &layout->name_text, error) || !word(doc, object, "byteLength", &layout->bytes, error) || !layout->bytes || layout->bytes > PRIVATE_BYTES)
            return application_fail(error, QA_ERROR_FORMAT, "Native private layout has no bounded named source record");
        if (named(p, layout->name, "entity") || named(p, layout->name, "client") || named(p, layout->name, "image"))
            return application_fail(error, QA_ERROR_FORMAT, "Native private layout identity shadows a source pointer domain");
        for (uint32_t j = 0; j < i; ++j) if (!strcmp((const char *)layout->name_text.data, (const char *)p->layouts[j].name_text.data))
            return application_fail(error, QA_ERROR_FORMAT, "Native private layouts repeat an identity");
        qa_json_id domain = qa_json_get(doc, object, "domain");
        if (qa_json_string_equal(doc, domain, "client")) {
            if (p->client_layout != UINT32_MAX || layout->bytes != p->client_bytes)
                return application_fail(error, QA_ERROR_FORMAT, "Native private layout changes its actual client extent");
            layout->domain = PRIVATE_CLIENT; p->client_layout = i;
        } else if (qa_json_string_equal(doc, domain, "entity")) {
            if (p->entity_layout != UINT32_MAX || layout->bytes != p->entity_bytes)
                return application_fail(error, QA_ERROR_FORMAT, "Native private layout changes its complete original entity extent");
            layout->domain = PRIVATE_ENTITY; p->entity_layout = i;
        } else if (qa_json_string_equal(doc, domain, "image")) {
            layout->domain = PRIVATE_IMAGE;
            if (!word(doc, object, "count", &layout->image_count, error) || !layout->image_count ||
                layout->image_count > PRIVATE_BYTES / layout->bytes ||
                !qa_json_u64(doc, qa_json_get(doc, object, "rva"), &layout->rva, error) ||
                layout->rva > UINT64_MAX - (uint64_t)layout->bytes * layout->image_count) return false;
            if (!qa_native_module_mutable_range(engine->provider->state.native.module, layout->rva,
                (uint64_t)layout->bytes * layout->image_count, error)) return false;
            for (uint32_t j = 0; j < i; ++j) if (p->layouts[j].domain == PRIVATE_IMAGE &&
                layout->rva < p->layouts[j].rva + (uint64_t)p->layouts[j].bytes * p->layouts[j].image_count &&
                p->layouts[j].rva < layout->rva + (uint64_t)layout->bytes * layout->image_count)
                return application_fail(error, QA_ERROR_FORMAT, "Native private image roots overlap");
        } else if (qa_json_string_equal(doc, domain, "heap")) {
            uint32_t tag; layout->domain = PRIVATE_HEAP;
            if (!word(doc, object, "tag", &tag, error) || tag > INT32_MAX) return false;
            layout->tag = (int32_t)tag;
        } else return application_fail(error, QA_ERROR_FORMAT, "Native private layout has an unknown ownership domain");
        fields = qa_json_get(doc, object, "fields"); layout->field_count = (uint32_t)qa_json_size(doc, fields);
        if (qa_json_type(doc, fields) != QA_JSON_ARRAY || !layout->field_count || layout->field_count > PRIVATE_FIELDS)
            return application_fail(error, QA_ERROR_FORMAT, "Native private layout has no complete typed field roster");
        layout->fields = calloc(layout->field_count, sizeof(*layout->fields));
        if (!layout->fields) return application_fail(error, QA_ERROR_MEMORY, "Retaining native private fields");
        uint64_t covered = 0;
        for (uint32_t k = 0; k < layout->field_count; ++k) {
            qa_json_id declaration = qa_json_at(doc, fields, k); private_field *field = &layout->fields[k];
            field->name = qa_json_get(doc, declaration, "name");
            if (!valid_name(doc, field->name, &field->name_text, error) || !word(doc, declaration, "offset", &field->offset, error) ||
                !scalar_type(p, declaration, field, error) || field->offset > layout->bytes || field->bytes > layout->bytes - field->offset) return false;
            for (uint32_t j = 0; j < k; ++j) {
                private_field *previous = &layout->fields[j];
                if (!strcmp((const char *)field->name_text.data, (const char *)previous->name_text.data) ||
                    (field->offset < previous->offset + previous->bytes && previous->offset < field->offset + field->bytes))
                    return application_fail(error, QA_ERROR_FORMAT, "Native private fields alias a name or source bytes");
            }
            covered += field->bytes;
            if (field->storage == PRIVATE_RECONSTRUCTED && layout->domain != PRIVATE_IMAGE)
                return application_fail(error, QA_ERROR_UNSUPPORTED, "Native live client/heap bytes cannot be replaced by original Init defaults");
            if (field->storage == PRIVATE_POINTER) layout->pointer_count += field->bytes / 8;
        }
        if (covered != layout->bytes) return application_fail(error, QA_ERROR_UNSUPPORTED, "Native private record has undeclared bytes or pointers");
    }
    if (p->client_layout == UINT32_MAX || p->entity_layout == UINT32_MAX)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native full private continuation omits complete client or entity records");
    if (!source_client_coverage(p, error)) return false;
    if (!source_object_coverage(p, error)) return false;
    qa_json_id rng_fields = qa_json_get(doc, qa_json_get(doc, qa_json_get(doc, root, "continuation"), "rng"), "fields");
    for (size_t i = 0; i < qa_json_size(doc, rng_fields); ++i) {
        qa_json_id field = qa_json_at(doc, rng_fields, i); uint64_t rva; uint32_t count;
        uint32_t width = qa_json_string_equal(doc, qa_json_get(doc, field, "storage"), "uint32") ? 4 : 8;
        if (!qa_json_u64(doc, qa_json_get(doc, field, "rva"), &rva, error) || !word(doc, field, "count", &count, error) ||
            rva > UINT64_MAX - (uint64_t)count * width) return false;
        for (uint32_t j = 0; j < p->count; ++j) if (p->layouts[j].domain == PRIVATE_IMAGE &&
            rva < p->layouts[j].rva + (uint64_t)p->layouts[j].bytes * p->layouts[j].image_count &&
            p->layouts[j].rva < rva + (uint64_t)count * width)
            return application_fail(error, QA_ERROR_FORMAT, "Native complete private roots overlap separately owned original RNG state");
    }
    for (uint32_t i = 0; i < p->count; ++i) for (uint32_t j = 0; j < p->layouts[i].field_count; ++j) {
        private_field *field = &p->layouts[i].fields[j];
        if (field->storage != PRIVATE_POINTER) continue;
        if (!field->image_record && !field->image_alternative && (named(p, field->target, "entity") || named(p, field->target, "client") || named(p, field->target, "image"))) continue;
        uint32_t target_layout = layout_named(p, field->target);
        if (target_layout == UINT32_MAX || p->layouts[target_layout].domain != (field->image_record ? PRIVATE_IMAGE : PRIVATE_HEAP))
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Native private pointer has no original relocation producer");
    }
    return true;
}
bool application_q2_private_qualified(struct application_native_q2 *engine, qa_error *error)
{
    private_profile profile = {0};
    bool ok = profile_read(engine, &profile, error); profile_free(&profile); return ok;
}
static struct application_q2_private_state *state_create(struct application_native_q2 *engine, qa_error *error)
{
    struct application_q2_private_state *state = calloc(1, sizeof(*state));
    if (!state) { application_fail(error, QA_ERROR_MEMORY, "Preparing native private object owner"); return NULL; }
    state->records = calloc(PRIVATE_RECORDS, sizeof(*state->records));
    if (!state->records || !profile_read(engine, &state->profile, error)) { application_q2_private_free(state); return NULL; }
    return state;
}
static bool source_tables(struct application_native_q2 *engine, struct application_q2_private_state *state, qa_error *error)
{
    qa_native_instance *instance = qa_native_host_instance(engine->provider->state.native.host);
    if (!qa_native_entity_table_get(instance, &state->table, error) || state->table.stride != state->profile.entity_bytes) return false;
    qa_native_allocation_info entity_allocation;
    if (!state->table.base || !qa_native_allocation_query(instance, state->table.base, &entity_allocation, error)) return false;
    if (entity_allocation.base != state->table.base || entity_allocation.tag != 765 ||
        entity_allocation.bytes != (uint64_t)state->table.capacity * state->table.stride)
        return application_fail(error, QA_ERROR_FORMAT, "Native private entity table differs from original tagged allocation bounds");
    memset(state->clients, 0, sizeof(state->clients));
    for (uint32_t slot = 1; slot < 257 && slot < state->table.capacity; ++slot) {
        qa_native_address address; uint8_t bytes[8];
        if (!qa_native_entity_address(instance, slot, &address, error) || address > UINT64_MAX - state->profile.client_pointer ||
            !qa_native_read(instance, address + state->profile.client_pointer, bytes, 8, error)) return false;
        state->clients[slot] = qa_load_u64le(bytes);
        for (uint32_t previous = 1; state->clients[slot] && previous < slot; ++previous)
            if (state->clients[slot] == state->clients[previous]) return application_fail(error, QA_ERROR_FORMAT, "Native private client roots alias one another");
    }
    qa_native_allocation_info allocation;
    if (!state->clients[1] || !qa_native_allocation_query(instance, state->clients[1], &allocation, error)) return false;
    if (allocation.base != state->clients[1] || allocation.tag != 765 || !state->profile.client_bytes ||
        allocation.bytes % state->profile.client_bytes || allocation.bytes / state->profile.client_bytes > 256 ||
        allocation.bytes > UINT64_MAX - allocation.base)
        return application_fail(error, QA_ERROR_FORMAT, "Native private client array differs from its actual original tagged allocation");
    uint64_t count = allocation.bytes / state->profile.client_bytes;
    for (uint32_t slot = 1; slot < 257; ++slot) {
        qa_native_address expected = slot <= count ? allocation.base + (uint64_t)(slot - 1) * state->profile.client_bytes : 0;
        if (state->clients[slot] != expected)
            return application_fail(error, QA_ERROR_FORMAT, "Native private client slots differ from original contiguous allocation rows");
    }
    return true;
}
static bool record_add(struct application_q2_private_state *state, uint32_t layout_id, uint32_t source,
    uint64_t count, qa_native_address address, uint32_t *id, qa_error *error)
{
    private_layout *layout = &state->profile.layouts[layout_id];
    if (!count || count > PRIVATE_BYTES / layout->bytes || count * layout->bytes > PRIVATE_BYTES - state->bytes ||
        (layout->pointer_count && count > PRIVATE_REFS / layout->pointer_count) ||
        count * layout->pointer_count > PRIVATE_REFS - state->refs || state->count == PRIVATE_RECORDS)
        return application_fail(error, QA_ERROR_FORMAT, "Native private object graph exceeds its bounded owner");
    private_record *record = &state->records[state->count];
    record->layout = layout_id; record->source = source; record->count = count; record->capture_base = address;
    record->data.size = (size_t)count * layout->bytes; record->data.data = calloc(1, record->data.size);
    record->ref_count = (size_t)count * layout->pointer_count;
    record->refs = calloc(record->ref_count ? record->ref_count : 1, sizeof(*record->refs));
    ++state->count;
    if (!record->data.data || !record->refs) return application_fail(error, QA_ERROR_MEMORY, "Retaining native private object bytes and relocations");
    state->bytes += record->data.size; state->refs += record->ref_count; *id = state->count - 1; return true;
}
static bool reference_capture(struct application_native_q2 *engine, struct application_q2_private_state *state,
    const private_field *field, qa_native_address address, private_ref *out, qa_error *error)
{
    if (!address) { *out = (private_ref){0}; return true; }
    qa_native_instance *instance = qa_native_host_instance(engine->provider->state.native.host);
    const private_profile *p = &state->profile;
    if (named(p, field->target, "entity")) {
        if (address < state->table.base || !state->table.stride ||
            (address - state->table.base) % state->table.stride || (address - state->table.base) / state->table.stride >= state->table.capacity)
            return application_fail(error, QA_ERROR_FORMAT, "Native private entity pointer is outside its actual source table");
        *out = (private_ref){REF_ENTITY, (address - state->table.base) / state->table.stride, 0}; return true;
    }
    if (named(p, field->target, "client")) {
        for (uint32_t slot = 1; slot < 257; ++slot) if (state->clients[slot] == address) {
            *out = (private_ref){REF_CLIENT, slot, 0}; return true;
        }
        return application_fail(error, QA_ERROR_FORMAT, "Native private client pointer is outside its actual client roots");
    }
    if (named(p, field->target, "image") || field->image_alternative || field->image_record) {
        qa_native_address base;
        qa_native_module_info info = qa_native_module_describe(engine->provider->state.native.module);
        if (!qa_native_rva(instance, 0, 1, &base, error)) return false;
        if (address >= base && address - base < info.image.image_bytes) {
            if (field->image_record) {
                uint32_t id = layout_named(p, field->target); const private_layout *root = &p->layouts[id];
                uint64_t rva = address - base;
                if (rva < root->rva || rva - root->rva >= (uint64_t)root->bytes * root->image_count ||
                    (rva - root->rva) % root->bytes)
                    return application_fail(error, QA_ERROR_FORMAT, "Native private image record pointer is outside its actual source root");
            }
            *out = (private_ref){REF_IMAGE, address - base, 0}; return true;
        }
        if (!field->image_alternative)
            return application_fail(error, QA_ERROR_FORMAT, "Native private image pointer is outside its qualified artifact");
    }
    uint32_t layout_id = layout_named(p, field->target); qa_native_allocation_info allocation;
    if (layout_id == UINT32_MAX || !qa_native_allocation_query(instance, address, &allocation, error)) return false;
    const private_layout *layout = &p->layouts[layout_id];
    if (allocation.tag != layout->tag || allocation.bytes % layout->bytes ||
        (address - allocation.base) % layout->bytes)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native heap object differs from its original declared layout or allocation tag");
    uint32_t id = UINT32_MAX;
    for (uint32_t i = 0; i < state->count; ++i) if (state->records[i].capture_base == allocation.base) {
        if (state->records[i].layout != layout_id || state->records[i].count != allocation.bytes / layout->bytes)
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Native private allocation has conflicting object layouts");
        id = i; break;
    }
    if (id == UINT32_MAX && !record_add(state, layout_id, 0, allocation.bytes / layout->bytes, allocation.base, &id, error)) return false;
    *out = (private_ref){REF_OBJECT, id, address - allocation.base}; return true;
}
static bool scalar_valid(const private_field *field, const uint8_t *bytes)
{
    for (uint32_t i = 0; i < field->bytes; i += field->width) {
        if (field->storage == PRIVATE_BOOL && bytes[i] > 1) return false;
        if (field->storage == PRIVATE_F32) { uint32_t word = qa_load_u32le(bytes + i); float value; memcpy(&value, &word, 4); if (!isfinite(value)) return false; }
        if (field->storage == PRIVATE_F64) { uint64_t word = qa_load_u64le(bytes + i); double value; memcpy(&value, &word, 8); if (!isfinite(value)) return false; }
    }
    return true;
}
static bool capture_records(struct application_native_q2 *engine, struct application_q2_private_state *state, qa_error *error)
{
    qa_native_instance *instance = qa_native_host_instance(engine->provider->state.native.host);
    for (uint32_t index = 0; index < state->count; ++index) {
        private_record *record = &state->records[index]; const private_layout *layout = &state->profile.layouts[record->layout];
        if (record->capture_base > UINT64_MAX - record->data.size ||
            !qa_native_read(instance, record->capture_base, record->data.data, record->data.size, error)) return false;
        size_t reference = 0;
        for (uint64_t row = 0; row < record->count; ++row) for (uint32_t i = 0; i < layout->field_count; ++i) {
            const private_field *field = &layout->fields[i];
            uint64_t offset = row * layout->bytes + field->offset;
            if (field->storage == PRIVATE_RECONSTRUCTED) { memset(record->data.data + offset, 0, field->bytes); continue; }
            if (field->storage == PRIVATE_POINTER) for (uint32_t at = 0; at < field->bytes; at += 8) {
                qa_native_address address = qa_load_u64le(record->data.data + offset + at);
                memset(record->data.data + offset + at, 0, 8);
                if (!reference_capture(engine, state, field, address, &record->refs[reference++], error)) return false;
            } else if (!scalar_valid(field, record->data.data + offset))
                return application_fail(error, QA_ERROR_FORMAT, "Native private scalar has no finite original representation");
        }
    }
    return true;
}
static bool state_io(qa_source_save_io *io, struct application_q2_private_state *state)
{
    uint32_t version = 1; size_t count = state->count;
    if (!qa_source_save_u32(io, &version) || version != 1 || !qa_source_save_count(io, &count, PRIVATE_RECORDS)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        for (size_t i = 0; i < count; ++i) {
            uint32_t layout, source; uint64_t rows;
            if (!qa_source_save_u32(io, &layout) || layout >= state->profile.count || !qa_source_save_u32(io, &source) ||
                !qa_source_save_u64(io, &rows)) return false;
            uint32_t id;
            if (!record_add(state, layout, source, rows, 0, &id, io->error)) return false;
            private_record *record = &state->records[id];
            if (!qa_source_save_bytes(io, record->data.data, record->data.size)) return false;
            for (size_t j = 0; j < record->ref_count; ++j)
                if (!qa_source_save_u32(io, &record->refs[j].kind) || !qa_source_save_u64(io, &record->refs[j].value) ||
                    !qa_source_save_u64(io, &record->refs[j].offset)) return false;
        }
        return true;
    }
    for (uint32_t i = 0; i < state->count; ++i) {
        private_record *record = &state->records[i];
        if (!qa_source_save_u32(io, &record->layout) || !qa_source_save_u32(io, &record->source) ||
            !qa_source_save_u64(io, &record->count) || !qa_source_save_bytes(io, record->data.data, record->data.size)) return false;
        for (size_t j = 0; j < record->ref_count; ++j)
            if (!qa_source_save_u32(io, &record->refs[j].kind) || !qa_source_save_u64(io, &record->refs[j].value) ||
                !qa_source_save_u64(io, &record->refs[j].offset)) return false;
    }
    return true;
}
static const private_ref *record_reference(const private_profile *p, const private_record *record, const char *name)
{
    const private_layout *layout = &p->layouts[record->layout]; size_t index = 0;
    for (uint32_t i = 0; i < layout->field_count; ++i) {
        const private_field *field = &layout->fields[i];
        if (field->storage != PRIVATE_POINTER) continue;
        if (named(p, field->name, name)) return &record->refs[index];
        index += field->bytes / 8;
    }
    return NULL;
}
static bool reference_rows(const struct application_q2_private_state *state, const private_ref *ref, uint64_t count)
{
    return ref && ref->kind == REF_OBJECT && !ref->offset && ref->value < state->count && state->records[ref->value].count == count;
}
static bool source_record_counts(const struct application_q2_private_state *state, qa_error *error)
{
    const private_profile *p = &state->profile;
    const private_layout *game = source_layout(p, "game.runtime"); const private_record *runtime = NULL;
    for (uint32_t i = 0; i < state->count; ++i) if (&p->layouts[state->records[i].layout] == game) runtime = &state->records[i];
    if (!runtime) return application_fail(error, QA_ERROR_FORMAT, "Native private graph has no original game root");
    uint32_t clients = qa_load_u32le(runtime->data.data + source_field(p, game, "maxclients")->offset);
    uint32_t maxentities = qa_load_u32le(runtime->data.data + source_field(p, game, "maxentities")->offset);
    uint32_t lag = qa_load_u32le(runtime->data.data + source_field(p, game, "max_lag_origins")->offset);
    const private_ref *client_base = record_reference(p, runtime, "clients"), *lag_base = record_reference(p, runtime, "lag_origins");
    if (!clients || clients > 256 || !lag || lag > INT32_MAX || !client_base || client_base->kind != REF_CLIENT ||
        client_base->value != 1 || !reference_rows(state, lag_base, (uint64_t)clients * lag))
        return application_fail(error, QA_ERROR_FORMAT, "Native private lag/client allocation counts differ from original game state");
    uint32_t client_rows = 0; bool entities = false;
    for (uint32_t i = 0; i < state->count; ++i) {
        const private_record *record = &state->records[i]; const private_layout *layout = &p->layouts[record->layout];
        if (layout->domain == PRIVATE_CLIENT) {
            if (!record->source || record->source > clients) return false;
            if (record->data.data[source_field(p, layout, "num_lag_origins")->offset] > lag ||
                record->data.data[source_field(p, layout, "next_lag_origin")->offset] >= lag ||
                record->data.data[source_field(p, layout, "num_damage_indicators")->offset] > 4)
                return application_fail(error, QA_ERROR_FORMAT, "Native private client count exceeds original lag or damage indicator bounds");
            ++client_rows;
        }
        if (layout->domain == PRIVATE_ENTITY) {
            if (maxentities <= clients || record->count != maxentities) return false;
            entities = true;
            const private_field *count = source_field(p, layout, "moveinfo.curve_positions.count");
            const private_ref *base = record_reference(p, record, "moveinfo.curve_positions.ptr");
            const private_ref *client_pointer = record_reference(p, record, "shared.client");
            for (uint64_t row = 0; row < record->count; ++row) {
                const private_ref *client_ref = client_pointer + row * layout->pointer_count;
                if ((row && row <= clients && (client_ref->kind != REF_CLIENT || client_ref->value != row || client_ref->offset)) ||
                    ((!row || row > clients) && client_ref->kind != REF_NULL))
                    return application_fail(error, QA_ERROR_FORMAT, "Native private entity client pointer changes its original source slot");
                uint64_t rows = qa_load_u64le(record->data.data + row * layout->bytes + count->offset);
                const private_ref *ref = base + row * layout->pointer_count;
                if ((!rows && ref->kind != REF_NULL) || (rows && !reference_rows(state, ref, rows)))
                    return application_fail(error, QA_ERROR_FORMAT, "Native private mover curve differs from original allocated-memory count");
            }
        }
        if (named(p, layout->name, "menu.handle")) {
            uint32_t num = qa_load_u32le(record->data.data + source_field(p, layout, "num")->offset);
            uint32_t cur = qa_load_u32le(record->data.data + source_field(p, layout, "cur")->offset);
            const private_ref *arg = record_reference(p, record, "arg");
            if (record->count != 1 || !num || num > INT32_MAX || (cur != UINT32_MAX && cur >= num) ||
                !reference_rows(state, record_reference(p, record, "entries"), num) ||
                !arg || (arg->kind != REF_NULL && !reference_rows(state, arg, 1)))
                return application_fail(error, QA_ERROR_FORMAT, "Native private menu allocation differs from original handle counts");
        }
        if (named(p, layout->name, "menu.admin-settings") && record->count != 1) return false;
    }
    return (entities && client_rows == clients) || application_fail(error, QA_ERROR_FORMAT, "Native private entity/client roots differ from original game counts");
}
bool application_q2_private_capture(struct application_native_q2 *engine, qa_buffer *out, qa_error *error)
{
    struct application_q2_private_state *state = state_create(engine, error);
    if (!state) return false;
    bool ok = source_tables(engine, state, error); uint32_t id;
    qa_native_instance *instance = qa_native_host_instance(engine->provider->state.native.host);
    for (uint32_t slot = 1; ok && slot < 257; ++slot) if (state->clients[slot])
        ok = record_add(state, state->profile.client_layout, slot, 1, state->clients[slot], &id, error);
    if (ok) ok = record_add(state, state->profile.entity_layout, 0, state->table.capacity, state->table.base, &id, error);
    for (uint32_t layout = 0; ok && layout < state->profile.count; ++layout) if (state->profile.layouts[layout].domain == PRIVATE_IMAGE) {
        qa_native_address address;
        size_t bytes = (size_t)state->profile.layouts[layout].bytes * state->profile.layouts[layout].image_count;
        ok = qa_native_rva(instance, state->profile.layouts[layout].rva, bytes, &address, error) &&
            record_add(state, layout, 0, state->profile.layouts[layout].image_count, address, &id, error);
    }
    qa_source_save_io io = {0};
    if (ok) ok = capture_records(engine, state, error) && source_record_counts(state, error) && qa_source_save_writer(&io, engine->provider->application->session, error) &&
        state_io(&io, state) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); application_q2_private_free(state); return ok;
}
static bool validate_records(struct application_q2_private_state *state, qa_error *error)
{
    bool clients[257] = {0}, images[PRIVATE_LAYOUTS] = {0}, entities = false;
    for (uint32_t index = 0; index < state->count; ++index) {
        private_record *record = &state->records[index]; const private_layout *layout = &state->profile.layouts[record->layout];
        if (layout->domain == PRIVATE_CLIENT) {
            if (!record->source || record->source >= 257 || record->count != 1 || clients[record->source]) return false;
            clients[record->source] = true;
        } else if (layout->domain == PRIVATE_ENTITY) {
            if (record->source || entities) return false;
            entities = true;
        } else if (layout->domain == PRIVATE_IMAGE) {
            if (record->source || record->count != layout->image_count || images[record->layout]) return false;
            images[record->layout] = true;
        } else if (record->source) return false;
        size_t reference = 0;
        for (uint64_t row = 0; row < record->count; ++row) for (uint32_t i = 0; i < layout->field_count; ++i) {
            const private_field *field = &layout->fields[i]; const uint8_t *bytes = record->data.data + row * layout->bytes + field->offset;
            if (field->storage == PRIVATE_POINTER || field->storage == PRIVATE_RECONSTRUCTED) {
                for (uint32_t at = 0; at < field->bytes; ++at) if (bytes[at]) return false;
            } else if (!scalar_valid(field, bytes)) return false;
            if (field->storage != PRIVATE_POINTER) continue;
            for (uint32_t at = 0; at < field->bytes; at += 8) {
                private_ref *ref = &record->refs[reference++];
                if (ref->kind == REF_NULL) { if (ref->value || ref->offset) return false; continue; }
                if (field->image_record) {
                    uint32_t id = layout_named(&state->profile, field->target); const private_layout *root = &state->profile.layouts[id];
                    if (ref->kind != REF_IMAGE || ref->offset || ref->value < root->rva ||
                        ref->value - root->rva >= (uint64_t)root->bytes * root->image_count ||
                        (ref->value - root->rva) % root->bytes) return false;
                    continue;
                }
                if (field->image_alternative && ref->kind == REF_IMAGE) { if (ref->offset) return false; continue; }
                if (named(&state->profile, field->target, "entity")) { if (ref->kind != REF_ENTITY || ref->value > UINT32_MAX || ref->offset) return false; }
                else if (named(&state->profile, field->target, "client")) { if (ref->kind != REF_CLIENT || !ref->value || ref->value >= 257 || ref->offset) return false; }
                else if (named(&state->profile, field->target, "image")) { if (ref->kind != REF_IMAGE || ref->offset) return false; }
                else {
                    uint32_t target = layout_named(&state->profile, field->target);
                    if (ref->kind != REF_OBJECT || ref->value >= state->count || state->records[ref->value].layout != target ||
                        ref->offset >= state->records[ref->value].data.size ||
                        ref->offset % state->profile.layouts[target].bytes) return false;
                }
            }
        }
    }
    for (uint32_t layout = 0; layout < state->profile.count; ++layout)
        if (state->profile.layouts[layout].domain == PRIVATE_IMAGE && !images[layout]) return false;
    if (!entities) return false;
    bool reached[PRIVATE_RECORDS] = {0}; uint32_t queue[PRIVATE_RECORDS], queued = 0;
    for (uint32_t i = 0; i < state->count; ++i) if (state->profile.layouts[state->records[i].layout].domain != PRIVATE_HEAP) {
        reached[i] = true; queue[queued++] = i;
    }
    for (uint32_t cursor = 0; cursor < queued; ++cursor) {
        const private_record *record = &state->records[queue[cursor]];
        for (size_t i = 0; i < record->ref_count; ++i) if (record->refs[i].kind == REF_OBJECT && !reached[record->refs[i].value]) {
            reached[record->refs[i].value] = true; queue[queued++] = (uint32_t)record->refs[i].value;
        }
    }
    for (uint32_t i = 0; i < state->count; ++i) if (!reached[i]) return false;
    return source_record_counts(state, error);
}
bool application_q2_private_prepare(struct application_native_q2 *engine, qa_bytes bytes,
    struct application_q2_private_state **out, qa_error *error)
{
    *out = NULL; struct application_q2_private_state *state = state_create(engine, error);
    if (!state) return false;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, engine->provider->application->session, bytes, error) && state_io(&io, state) &&
        qa_source_save_finish(&io, NULL) && validate_records(state, error);
    qa_source_save_dispose(&io);
    if (!ok) { application_q2_private_free(state); if (error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "Invalid native private object graph"); return false; }
    *out = state; return true;
}
bool application_q2_private_client_matches(const struct application_q2_private_state *state,
    uint32_t slot, bool present, bool connected, bool spawned, qa_error *error)
{
    if (!state || !slot || slot >= 257) return application_fail(error, QA_ERROR_ARGUMENT, "Native private client qualification needs its prepared source row");
    const private_record *record = NULL;
    for (uint32_t i = 0; i < state->count; ++i)
        if (state->records[i].layout == state->profile.client_layout && state->records[i].source == slot) record = &state->records[i];
    if ((record != NULL) != present) return application_fail(error, QA_ERROR_FORMAT, "Native private client roots differ from original connection continuation");
    if (!record) return !connected && !spawned;
    const private_layout *layout = &state->profile.layouts[state->profile.client_layout];
    for (uint32_t i = 0; i < layout->field_count; ++i) {
        const private_field *field = &layout->fields[i];
        if ((named(&state->profile, field->name, "pers.connected") && (record->data.data[field->offset] != 0) != connected) ||
            (named(&state->profile, field->name, "pers.spawned") && (record->data.data[field->offset] != 0) != spawned))
            return application_fail(error, QA_ERROR_FORMAT, "Native private client flags disagree with original connection continuation");
    }
    return true;
}
bool application_q2_private_apply(struct application_native_q2 *engine, struct application_q2_private_state *state, qa_error *error)
{
    if (!state || state->applied || !engine->initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native private object publication requires its isolated initialized source owner");
    if (!source_tables(engine, state, error)) return false;
    qa_native_instance *instance = qa_native_host_instance(engine->provider->state.native.host);
    qa_native_address *addresses = calloc(state->count ? state->count : 1, sizeof(*addresses));
    if (!addresses) return application_fail(error, QA_ERROR_MEMORY, "Resolving native private object addresses");
    bool ok = true; bool clients[257] = {0};
    for (uint32_t i = 0; ok && i < state->count; ++i) {
        private_record *record = &state->records[i]; const private_layout *layout = &state->profile.layouts[record->layout];
        if (layout->domain == PRIVATE_CLIENT) { addresses[i] = state->clients[record->source]; clients[record->source] = true; ok = addresses[i] != 0; }
        else if (layout->domain == PRIVATE_ENTITY) { addresses[i] = state->table.base; ok = record->count == state->table.capacity; }
        else if (layout->domain == PRIVATE_IMAGE) ok = qa_native_rva(instance, layout->rva, record->data.size, &addresses[i], error);
        else ok = qa_native_allocate(instance, record->data.size, layout->tag, &addresses[i], error);
        if (ok && addresses[i] > UINT64_MAX - record->data.size) ok = false;
    }
    for (uint32_t slot = 1; ok && slot < 257; ++slot) if ((state->clients[slot] != 0) != clients[slot]) ok = false;
    /* Every owned object exists before pointer resolution; cycles and shared
     * allocations retain one candidate identity. No source callback runs. */
    for (uint32_t index = 0; ok && index < state->count; ++index) {
        private_record *record = &state->records[index]; const private_layout *layout = &state->profile.layouts[record->layout]; size_t reference = 0;
        for (uint64_t row = 0; ok && row < record->count; ++row) for (uint32_t i = 0; ok && i < layout->field_count; ++i) {
            const private_field *field = &layout->fields[i]; uint8_t *bytes = record->data.data + row * layout->bytes + field->offset;
            if (field->storage == PRIVATE_RECONSTRUCTED)
                ok = qa_native_read(instance, addresses[index] + row * layout->bytes + field->offset, bytes, field->bytes, error);
            if (field->storage != PRIVATE_POINTER) continue;
            for (uint32_t at = 0; ok && at < field->bytes; at += 8) {
                const private_ref *ref = &record->refs[reference++]; qa_native_address address = 0;
                if (ref->kind == REF_ENTITY) ok = ref->value < state->table.capacity && qa_native_entity_address(instance, (uint32_t)ref->value, &address, error);
                else if (ref->kind == REF_CLIENT) { address = state->clients[ref->value]; ok = address != 0; }
                else if (ref->kind == REF_IMAGE) ok = qa_native_rva(instance, ref->value, 1, &address, error);
                else if (ref->kind == REF_OBJECT) address = addresses[ref->value] + ref->offset;
                qa_store_u64le(bytes + at, address);
            }
        }
    }
    /* Whole typed records include their explicitly declared padding. Source
     * pointers were replaced in owned buffers, never serialized as addresses. */
    state->applied = true;
    for (unsigned pass = 0; ok && pass < 2; ++pass) for (uint32_t i = 0; ok && i < state->count; ++i) {
        bool heap = state->profile.layouts[state->records[i].layout].domain == PRIVATE_HEAP;
        if (heap == (pass == 0))
            ok = qa_native_write(instance, addresses[i], (qa_bytes){state->records[i].data.data, state->records[i].data.size}, error);
    }
    free(addresses);
    if (!ok && error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "Native private state differs from the reconstructed source owner");
    return ok;
}
