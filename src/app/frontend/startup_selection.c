#include "startup_selection.h"
#include "startup_menus.h"
#include "internal.h"
#include "config_weapon_defaults.h"
#include "qa/application_character_selection.h"
#include "qa/monster_catalog.h"
#include "qa/bsp.h"
#include "../application/guest_q3_grapple_profile.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct model_preference { char *product, *model; } model_preference;
typedef struct q3_hook_metadata { qa_product_id product; application_q3_grapple_profile *profile; } q3_hook_metadata;
struct frontend_startup_selection {
    qa_ui_library_choice *choices;
    size_t count;
    char **strings;
    size_t string_count;
    model_preference *models;
    size_t model_count;
    qa_ui_library_roster_row *roster;
    unsigned *roster_counts;
    size_t roster_count;
    qa_catalog *hook_catalog;
    q3_hook_metadata *hooks;
    size_t hook_count;
    frontend_config_weapon_binding_catalog weapon_bindings;
};

static bool fail(qa_error *e, const char *message)
{
    qa_error_set(e, QA_ERROR_ARGUMENT, 0, "%s", message); return false;
}

static char *copy_text(const char *text, qa_error *e)
{
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (!copy) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining startup selection metadata"); return NULL; }
    memcpy(copy, text, size); return copy;
}

static void clear_rows(frontend_startup_selection *s)
{
    for (size_t i = 0; i < s->string_count; ++i) free(s->strings[i]);
    free(s->strings); free(s->choices); free(s->roster); free(s->roster_counts);
    s->strings = NULL; s->choices = NULL; s->string_count = s->count = 0;
    s->roster = NULL; s->roster_count = 0;
    s->roster_counts = NULL;
}

static const char *text(frontend_startup_selection *s, const char *value, qa_error *e)
{
    char *copy = copy_text(value, e);
    if (!copy) return NULL;
    char **grown = realloc(s->strings, (s->string_count + 1) * sizeof(*grown));
    if (!grown) { free(copy); qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining startup labels"); return NULL; }
    s->strings = grown; s->strings[s->string_count++] = copy; return copy;
}

static bool append(frontend_startup_selection *s, const char *id, const char *label,
    const char *unavailable, qa_error *e)
{
    const char *key = text(s, id, e), *title = text(s, label, e);
    if (!key || !title) return false;
    const char *reason = unavailable ? text(s, unavailable, e) : NULL;
    if (unavailable && !reason) return false;
    qa_ui_library_choice *grown = realloc(s->choices, (s->count + 1) * sizeof(*grown));
    if (!grown) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining startup choices"); return false; }
    s->choices = grown; s->choices[s->count++] = (qa_ui_library_choice){key, title, reason}; return true;
}

static const qa_launch_provider *provider(const qa_launch_choices *c, const char *instance)
{
    for (size_t i = 0; instance && i < c->provider_count; ++i)
        if (!strcmp(c->providers[i].instance, instance)) return &c->providers[i];
    return NULL;
}

static const qa_product *role_product(qa_ui_library *library, qa_launch_role role)
{
    const qa_launch_choices *c = qa_ui_library_choices(library);
    const qa_launch_binding *b = qa_launch_binding_for(c,
        (qa_launch_scope){.kind = QA_SCOPE_DEFAULT_PLAYER}, role, "");
    const qa_launch_provider *p = b ? provider(c, b->instance) : NULL;
    return p ? qa_catalog_product(qa_ui_library_catalog(library), p->product) : NULL;
}

static const char *unavailable(const qa_product *p)
{
    return !p ? "Source content unavailable" : p->availability == QA_CONTENT_INSTALLED ? NULL :
        p->availability == QA_CONTENT_INVALID ? "Source content is invalid" : "Game files are not installed";
}

static bool product_choice(frontend_startup_selection *s, const qa_product *p, qa_error *e)
{
    char label[512];
    const char *edition = p->edition == QA_EDITION_RERELEASE ? "rerelease" :
        p->edition == QA_EDITION_QUAKEWORLD ? "quakeworld" : p->edition == QA_EDITION_DEMO ? "demo" : "classic";
    snprintf(label, sizeof(label), "%s (%s)", p->title, edition);
    return append(s, p->key, label, unavailable(p), e);
}

static bool probe(qa_vfs *vfs, const char *path, bool *found, qa_error *e)
{
    uint64_t size; return qa_vfs_probe(vfs, path, found, &size, e);
}

static bool model_candidate(frontend_startup_selection *s, qa_vfs *vfs,
    qa_game_family family, const char *name, qa_error *e)
{
    if (!*name || strchr(name, '/') || strchr(name, '\\')) return true;
    for (size_t i = 0; i < s->count; ++i) if (!strcmp(s->choices[i].id, name)) return true;
    char path[sizeof("models/players/heads/") + 2 * 255 + sizeof("/.md3")]; bool lower, upper, head;
    snprintf(path, sizeof(path), family == QA_GAME_Q2 ? "players/%s/tris.md2" : "models/players/%s/lower.md3", name);
    if (!probe(vfs, path, &lower, e)) return false;
    if (family == QA_GAME_Q2) return !lower || append(s, name, name, NULL, e);
    if (!lower) {
        snprintf(path, sizeof(path), "models/players/characters/%s/lower.md3", name);
        if (!probe(vfs, path, &lower, e)) return false;
    }
    snprintf(path, sizeof(path), "models/players/%s/upper.md3", name);
    if (!probe(vfs, path, &upper, e)) return false;
    if (!upper) {
        snprintf(path, sizeof(path), "models/players/characters/%s/upper.md3", name);
        if (!probe(vfs, path, &upper, e)) return false;
    }
    snprintf(path, sizeof(path), "models/players/%s/head.md3", name);
    if (!probe(vfs, path, &head, e)) return false;
    if (!head) {
        snprintf(path, sizeof(path), "models/players/heads/%s/%s.md3", name, name);
        if (!probe(vfs, path, &head, e)) return false;
    }
    return !(lower && upper && head) || append(s, name, name, NULL, e);
}

static int choice_compare(const void *a, const void *b)
{
    return strcmp(((const qa_ui_library_choice *)a)->id, ((const qa_ui_library_choice *)b)->id);
}

static bool model_choices(frontend_startup_selection *s, qa_ui_library *library, qa_error *e)
{
    const qa_product *p = role_product(library, QA_ROLE_CHARACTER);
    if (!p) return fail(e, "Character source has no selected product");
    qa_vfs *vfs = NULL;
    if (!qa_catalog_open(qa_ui_library_catalog(library), p->id, &vfs, e)) return false;
    bool ok = true;
    if (p->family == QA_GAME_Q1) {
        bool found; ok = probe(vfs, "progs/player.mdl", &found, e);
        if (ok) ok = append(s, "player", "Quake player", found ? NULL : "Player model not installed", e);
    } else {
        const char *directories[] = {p->family == QA_GAME_Q2 ? "players" : "models/players", "models/players/characters"};
        size_t count = p->family == QA_GAME_Q2 ? 1 : 2;
        for (size_t d = 0; ok && d < count; ++d) {
            for (unsigned mode = 0; ok && mode < 2; ++mode) {
                qa_vfs_listing listing = {0};
                ok = qa_vfs_list(vfs, directories[d], mode ? "/" : p->family == QA_GAME_Q2 ? ".md2" : ".md3", &listing, e);
                for (size_t i = 0; ok && i < listing.count; ++i) {
                    const char *entry = listing.names[i];
                    const char *slash = strchr(entry, '/');
                    if (!mode && (!slash || strcmp(slash, p->family == QA_GAME_Q2 ? "/tris.md2" : "/lower.md3"))) continue;
                    size_t length = slash ? (size_t)(slash - entry) : strlen(entry);
                    if (!length || length >= 256) continue;
                    char name[256];
                    for (size_t n = 0; n < length; ++n) name[n] = (char)tolower((unsigned char)entry[n]);
                    name[length] = 0;
                    ok = model_candidate(s, vfs, p->family, name, e);
                }
                qa_vfs_listing_free(&listing);
            }
        }
        if (ok && s->count > 1) qsort(s->choices, s->count, sizeof(*s->choices), choice_compare);
    }
    qa_vfs_destroy(vfs); return ok;
}

static bool supports_weapons(const qa_product *p)
{
    if (p->family == QA_GAME_Q3) return !strcmp(p->campaign, "baseq3") || !strcmp(p->campaign, "missionpack");
    if (p->edition != QA_EDITION_CLASSIC && p->edition != QA_EDITION_RERELEASE) return false;
    if (p->family == QA_GAME_Q1) return !strcmp(p->campaign, "id1") || !strcmp(p->campaign, "hipnotic") ||
        !strcmp(p->campaign, "rogue") || (p->edition == QA_EDITION_RERELEASE &&
            (!strcmp(p->campaign, "dopa") || !strcmp(p->campaign, "mg1") || !strcmp(p->campaign, "mg3")));
    return p->family == QA_GAME_Q2 && (!strcmp(p->campaign, "baseq2") || !strcmp(p->campaign, "xatrix") ||
        !strcmp(p->campaign, "rogue") || (p->edition == QA_EDITION_RERELEASE && !strcmp(p->campaign, "mg2")));
}

static const qa_launch_equipment *equipment(qa_ui_library *library)
{
    const qa_launch_choices *c = qa_ui_library_choices(library);
    for (size_t i = 0; i < c->equipment_count; ++i)
        if (c->equipment[i].scope.kind == QA_SCOPE_DEFAULT_PLAYER) return &c->equipment[i];
    return NULL;
}

static qa_grapple_mechanic product_hook(const qa_product *p)
{
    if (p->family == QA_GAME_Q1 && !strcmp(p->campaign, "ctf") && p->edition == QA_EDITION_CLASSIC) return QA_GRAPPLE_THREEWAVE;
    if (p->family == QA_GAME_Q2 && !strcmp(p->campaign, "ctf") &&
        (p->edition == QA_EDITION_CLASSIC || p->edition == QA_EDITION_RERELEASE)) return QA_GRAPPLE_Q2_CTF;
    if (p->family == QA_GAME_Q2 && !strcmp(p->campaign, "lmctf") && p->edition == QA_EDITION_CLASSIC) return QA_GRAPPLE_LMCTF;
    return QA_GRAPPLE_DISABLED;
}

static const char *hook_id(qa_grapple_mechanic m)
{
    return m == QA_GRAPPLE_THREEWAVE ? "q1-threewave" : m == QA_GRAPPLE_Q2_CTF ? "q2-ctf" :
        m == QA_GRAPPLE_LMCTF ? "q2-lmctf" : "native";
}

static const qa_product *hook_source(qa_ui_library *library, qa_grapple_mechanic mechanic)
{
    const qa_catalog *catalog = qa_ui_library_catalog(library);
    const qa_product *preferred = qa_catalog_product(catalog, qa_ui_library_choices(library)->world.preset), *best = NULL;
    for (size_t i = 0; i < qa_catalog_count(catalog); ++i) {
        const qa_product *p = qa_catalog_at(catalog, i);
        if (product_hook(p) != mechanic) continue;
        if (!best || (p->availability == QA_CONTENT_INSTALLED && best->availability != QA_CONTENT_INSTALLED) ||
            (p->availability == best->availability && preferred && p->edition == preferred->edition && best->edition != preferred->edition)) best = p;
    }
    return best;
}

static const qa_product *grenade_source(qa_ui_library *library)
{
    const qa_catalog *catalog = qa_ui_library_catalog(library);
    const qa_product *preferred = qa_catalog_product(catalog, qa_ui_library_choices(library)->world.preset), *best = NULL;
    for (size_t i = 0; i < qa_catalog_count(catalog); ++i) {
        const qa_product *p = qa_catalog_at(catalog, i);
        if (p->family != QA_GAME_Q2 || strcmp(p->campaign, "baseq2") || p->availability != QA_CONTENT_INSTALLED ||
            (p->edition != QA_EDITION_CLASSIC && p->edition != QA_EDITION_RERELEASE)) continue;
        if (!best || (preferred && p->edition == preferred->edition && best->edition != preferred->edition)) best = p;
    }
    return best;
}

static void clear_hooks(frontend_startup_selection *s)
{
    for (size_t i = 0; i < s->hook_count; ++i) application_q3_grapple_profile_destroy(s->hooks[i].profile);
    free(s->hooks); s->hooks = NULL; s->hook_count = 0;
    qa_catalog_release(s->hook_catalog); s->hook_catalog = NULL;
}

static bool prepare_q3_hooks(frontend_startup_selection *s, qa_ui_library *library, qa_error *e)
{
    qa_catalog *catalog = qa_launch_draft_catalog(qa_ui_library_draft(library));
    if (s->hook_catalog == catalog) return true;
    clear_hooks(s);
    for (size_t i = 0; i < qa_catalog_count(catalog); ++i) {
        const qa_product *p = qa_catalog_at(catalog, i);
        if (p->family != QA_GAME_Q3 || p->availability != QA_CONTENT_INSTALLED) continue;
        qa_vfs *vfs = NULL; qa_qvm_image *image = NULL;
        qa_qvm_compatibility compatibility = {0}; application_q3_grapple_profile *profile = NULL;
        bool found = false;
        bool ok = qa_catalog_open(catalog, p->id, &vfs, e) && probe(vfs, "vm/qagame.qvm", &found, e);
        if (ok && found) ok = qa_qvm_image_open(vfs, "vm/qagame.qvm", QA_QVM_GAME, &image, &compatibility, e) &&
            application_q3_grapple_profile_create(image, QA_QVM_GAME, compatibility.abi, "vm/qagame.qvm", &profile, e);
        qa_qvm_compatibility_free(&compatibility); qa_qvm_image_release(image); qa_vfs_destroy(vfs);
        if (!ok) { application_q3_grapple_profile_destroy(profile); clear_hooks(s); return false; }
        if (!profile) continue;
        const application_q3_grapple_definition *definition = application_q3_grapple_profile_definition(profile);
        bool duplicate = false;
        for (size_t n = 0; n < s->hook_count; ++n)
            duplicate |= !strcmp(definition->id, application_q3_grapple_profile_definition(s->hooks[n].profile)->id);
        if (duplicate) { application_q3_grapple_profile_destroy(profile); continue; }
        q3_hook_metadata *grown = realloc(s->hooks, (s->hook_count + 1) * sizeof(*grown));
        if (!grown) { application_q3_grapple_profile_destroy(profile); clear_hooks(s); qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining installed Q3 hook metadata"); return false; }
        s->hooks = grown; s->hooks[s->hook_count++] = (q3_hook_metadata){p->id, profile};
    }
    s->hook_catalog = catalog; qa_catalog_retain(catalog); return true;
}

bool frontend_startup_selection_create(frontend_startup_selection **out, qa_error *e)
{
    if (!out) return fail(e, "Startup metadata needs an owner");
    *out = calloc(1, sizeof(**out));
    if (!*out) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Creating startup metadata"); return false; }
    return true;
}

void frontend_startup_selection_destroy(frontend_startup_selection *s)
{
    if (!s) return;
    clear_rows(s);
    clear_hooks(s);
    for (size_t i = 0; i < s->model_count; ++i) { free(s->models[i].product); free(s->models[i].model); }
    free(s->models); free(s);
}

bool frontend_startup_selection_weapon_bindings(void *context, qa_ui_library *library,
    const qa_input_weapon_binding **out, size_t *count, qa_error *e)
{
    frontend_seat *seat = context;
    if (!seat || !seat->startup_selection || !library || !out || !count ||
        seat->library != library || !qa_ui_library_draft(library))
        return fail(e, "Weapon bindings need their startup seat and draft");
    *out = NULL; *count = 0;
    const qa_launch_choices *choices = qa_ui_library_choices(library);
    qa_launch_scope scope = {.kind = QA_SCOPE_DEFAULT_PLAYER};
    if (seat->id < choices->seat_count && choices->seats[seat->id].local &&
        !choices->seats[seat->id].bot)
        scope = (qa_launch_scope){.kind = QA_SCOPE_SEAT, .seat = choices->seats[seat->id].id};
    frontend_config_weapon_binding_catalog *catalog = &seat->startup_selection->weapon_bindings;
    if (!frontend_config_weapon_bindings(qa_ui_library_draft(library), scope, catalog, e)) return false;
    *out = catalog->items; *count = catalog->count; return true;
}

static const char *model_selected(qa_ui_library *library)
{
    const qa_launch_choices *c = qa_ui_library_choices(library);
    for (size_t i = 0; i < c->seat_count; ++i)
        if (c->seats[i].local && !c->seats[i].bot && c->seats[i].character_model) return c->seats[i].character_model;
    const qa_launch_binding *b = qa_launch_binding_for(c,
        (qa_launch_scope){.kind = QA_SCOPE_DEFAULT_PLAYER}, QA_ROLE_BODY, "");
    return b ? b->definition : "";
}

static bool remember_model(frontend_startup_selection *s, const qa_product *p,
    const char *model, qa_error *e)
{
    size_t i = 0;
    while (i < s->model_count && strcmp(s->models[i].product, p->identity)) ++i;
    char *copy = copy_text(model, e);
    if (!copy) return false;
    if (i == s->model_count) {
        char *key = copy_text(p->identity, e);
        if (!key) { free(copy); return false; }
        model_preference *grown = realloc(s->models, (i + 1) * sizeof(*grown));
        if (!grown) { free(copy); free(key); qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining character model preference"); return false; }
        s->models = grown; s->models[s->model_count++] = (model_preference){key, copy};
    } else { free(s->models[i].model); s->models[i].model = copy; }
    return true;
}

static bool apply_model(qa_ui_library *library, const char *model, qa_error *e)
{
    const qa_product *p = role_product(library, QA_ROLE_CHARACTER);
    qa_native_q3_character_declaration declaration;
    if (!p || !qa_native_q3_character_default_declaration(p->family, &declaration, e)) return false;
    declaration.model = declaration.head_model = model;
    qa_launch_draft *draft = qa_ui_library_draft(library);
    const qa_launch_choices *c = qa_ui_library_choices(library);
    const qa_launch_binding *body = qa_launch_binding_for(c,
        (qa_launch_scope){.kind = QA_SCOPE_DEFAULT_PLAYER}, QA_ROLE_BODY, "");
    if (body) {
        qa_launch_binding value = *body; value.definition = model;
        if (!qa_launch_bind(draft, &value, e)) return false;
    }
    size_t count = qa_ui_library_choices(library)->seat_count;
    for (size_t i = 0; i < count; ++i) {
        qa_launch_seat seat = qa_ui_library_choices(library)->seats[i];
        if (!seat.local || seat.bot) continue;
        seat.character_model = declaration.model; seat.character_skin = declaration.skin;
        seat.character_head_model = declaration.head_model; seat.character_head_skin = declaration.head_skin;
        if (!qa_launch_set_seat(draft, &seat, e)) return false;
    }
    return true;
}

static const qa_launch_mode *mode(qa_ui_library *library)
{
    const qa_launch_choices *c = qa_ui_library_choices(library);
    for (size_t i = 0; i < c->mode_count; ++i) if (c->modes[i].primary_score) return c->modes + i;
    return c->mode_count ? c->modes : NULL;
}

static const char *rule_selected(qa_ui_library *library)
{
    const qa_launch_mode *m = mode(library);
    if (!m) return "standard";
    return m->rules.kind == QA_MODE_TAG ? "tag" : m->rules.kind == QA_MODE_DEATHBALL ? "deathball" :
        m->rules.kind == QA_MODE_HORDE ? "horde" : m->rules.kind == QA_MODE_CTF ?
            (m->rules.source == QA_MODE_LMCTF ? "lmctf" : "ctf") : "standard";
}

static const char *rule_unavailable_for(qa_ui_library *library, const char *rule, qa_mode_kind base_mode)
{
    const qa_catalog *catalog = qa_ui_library_catalog(library);
    const qa_product *p = qa_catalog_product(catalog, qa_ui_library_choices(library)->world.preset);
    bool deathmatch = base_mode == QA_MODE_FFA;
    if (!strcmp(rule, "standard")) return NULL;
    if (!strcmp(rule, "horde")) return !p || p->family != QA_GAME_Q1 || p->edition != QA_EDITION_RERELEASE ||
        (strcmp(p->campaign, "mg1") && strcmp(p->campaign, "dopa")) ?
        "Horde requires Quake rerelease Dimension of the Machine or Dimension of the Past" :
        deathmatch ? "Horde requires single player or cooperative mode" : NULL;
    if (!p || p->family != QA_GAME_Q2) return "These match rules require a Quake II source game";
    if (!deathmatch) return "These match rules require deathmatch mode";
    if (!strcmp(rule, "ctf") || !strcmp(rule, "lmctf")) {
        if (p->edition != QA_EDITION_CLASSIC) return "This CTF ruleset requires classic Quake II";
        return unavailable(qa_catalog_find(catalog, !strcmp(rule, "ctf") ? "q2-classic-ctf" : "q2-classic-lmctf"));
    }
    return p->edition != QA_EDITION_RERELEASE && strcmp(p->campaign, "rogue") ?
        "Tag and DeathBall require Ground Zero or Quake II rerelease" : NULL;
}

static const char *rule_unavailable(qa_ui_library *library, const char *rule)
{
    return rule_unavailable_for(library, rule, qa_ui_library_mode_preference(library));
}

static bool install_source(qa_ui_library *library, const qa_product *p,
    const char *instance, qa_error *e)
{
    qa_launch_draft *defaults = NULL;
    if (!p || p->availability != QA_CONTENT_INSTALLED) return fail(e, "Source content unavailable");
    if (!qa_launch_draft_create(qa_launch_draft_catalog(qa_ui_library_draft(library)), p->id, "", &defaults, e)) return false;
    const qa_launch_choices *c = qa_launch_draft_choices(defaults);
    bool ok = c->provider_count != 0;
    if (ok) {
        qa_launch_provider selection = c->providers[0]; selection.instance = instance;
        ok = qa_launch_set_provider(qa_ui_library_draft(library), &selection, e);
    } else fail(e, "Source preset has no provider");
    qa_launch_draft_destroy(defaults); return ok;
}

static const qa_product *monster_product(qa_ui_library *library, const qa_monster_catalog_source *source)
{
    const qa_catalog *catalog = qa_ui_library_catalog(library);
    for (size_t i = 0; source && i < qa_catalog_count(catalog); ++i) {
        const qa_product *p = qa_catalog_at(catalog, i);
        if (p->family == source->family && p->edition == source->edition && !strcmp(p->campaign, source->campaign)) return p;
    }
    return NULL;
}

static const qa_monster_catalog_source *monster_source_for(qa_ui_library *library, const char *instance)
{
    const qa_launch_provider *p = provider(qa_ui_library_choices(library), instance);
    const qa_product *product = p ? qa_catalog_product(qa_ui_library_catalog(library), p->product) : NULL;
    size_t count; const qa_monster_catalog_source *sources = qa_monster_catalog_sources(&count);
    for (size_t i = 0; product && i < count; ++i)
        if (sources[i].family == product->family && sources[i].edition == product->edition && !strcmp(sources[i].campaign, product->campaign)) return sources + i;
    return NULL;
}

static bool creature_available(qa_vfs *vfs, const qa_monster_catalog_creature *creature, bool *found, qa_error *e)
{
    *found = true;
    for (size_t i = 0; i < creature->resource_count; ++i) {
        bool exists;
        if (!probe(vfs, creature->resources[i], &exists, e)) return false;
        if (!exists) { *found = false; break; }
    }
    return true;
}

static void monster_name(const char *classname, char *out, size_t size)
{
    const char *known = !strcmp(classname, "monster_army") ? "Grunt" : !strcmp(classname, "monster_demon1") ? "Fiend" :
        !strcmp(classname, "monster_wizard") ? "Scrag" : !strcmp(classname, "monster_shalrath") ? "Vore" : !strcmp(classname, "monster_tarbaby") ? "Spawn" : NULL;
    if (known) { snprintf(out, size, "%s", known); return; }
    const char *source = !strncmp(classname, "monster_", 8) ? classname + 8 : classname;
    bool capital = true; size_t n = 0;
    for (; *source && n + 1 < size; ++source) {
        out[n++] = *source == '_' ? ' ' : capital ? (char)toupper((unsigned char)*source) : *source;
        capital = *source == '_';
    }
    out[n] = 0;
}

static qa_game_family map_family(qa_ui_library *library)
{
    const qa_product *p = qa_catalog_product(qa_ui_library_catalog(library), qa_ui_library_choices(library)->world.geometry);
    return p ? p->family : QA_GAME_Q3;
}

static const char *monster_adapter_unavailable(qa_ui_library *library)
{
    return map_family(library) == QA_GAME_Q3 ?
        "This map has no supported authored monster roster" : NULL;
}

static bool monster_choices(frontend_startup_selection *s, qa_ui_library *library,
    bool sources_only, const char *classname, qa_error *e)
{
    if (!sources_only && classname) {
        const qa_monster_catalog_source *source = monster_source_for(library, "startup:monster-source");
        const qa_monster_catalog_creature *creature = source ? qa_monster_catalog_default(map_family(library), source, classname) : NULL;
        char label[512], name[128];
        if (creature) {
            monster_name(creature->classname, name, sizeof(name));
            const char *suffix = strcmp(source->campaign, "id1") && strcmp(source->campaign, "baseq2") ? source->campaign : "";
            snprintf(label, sizeof(label), "Use default: %s (%s%s%s %s)", name, source->family == QA_GAME_Q1 ? "Q1" : "Q2",
                *suffix ? " " : "", suffix, source->edition == QA_EDITION_CLASSIC ? "classic" : "rerelease");
        } else snprintf(label, sizeof(label), "Use default: Keep native");
        if (!append(s, "default", label, NULL, e)) return false;
    }
    if (!append(s, "native", sources_only ? "Authored campaign monsters" : "Keep native", NULL, e)) return false;
    size_t count; const qa_monster_catalog_source *sources = qa_monster_catalog_sources(&count);
    for (size_t i = 0; i < count; ++i) {
        const qa_monster_catalog_source *source = sources + i;
        const qa_product *p = monster_product(library, source);
        const char *reason = unavailable(p), *adapter = monster_adapter_unavailable(library);
        if (!reason) reason = adapter;
        const char *edition = source->edition == QA_EDITION_CLASSIC ? "classic" : "rerelease";
        const char *family = source->family == QA_GAME_Q1 ? "Q1" : "Q2";
        char label[512];
        if (sources_only) {
            const char *title = !strcmp(source->campaign, "id1") ? "Quake" : !strcmp(source->campaign, "baseq2") ? "Quake II" : p ? p->title : source->campaign;
            snprintf(label, sizeof(label), "%s (%s)", title, edition);
            if (!append(s, source->provider, label, reason, e)) return false;
            continue;
        }
        qa_vfs *vfs = NULL;
        if (p && p->availability == QA_CONTENT_INSTALLED && !qa_catalog_open(qa_ui_library_catalog(library), p->id, &vfs, e)) return false;
        bool ok = true;
        for (size_t c = 0; ok && c < source->creature_count; ++c) {
            const qa_monster_catalog_creature *creature = source->creatures + c;
            bool found = false;
            if (vfs && !creature_available(vfs, creature, &found, e)) { ok = false; break; }
            char id[256], name[128]; monster_name(creature->classname, name, sizeof(name));
            snprintf(id, sizeof(id), "%s/%s", source->provider, creature->classname);
            const char *suffix = !strcmp(source->campaign, "id1") || !strcmp(source->campaign, "baseq2") ? "" : source->campaign;
            snprintf(label, sizeof(label), "%s (%s%s%s, %s)", name, family, *suffix ? " " : "", suffix, edition);
            ok = append(s, id, label, reason ? reason : found ? NULL : "Creature resources are not installed", e);
        }
        qa_vfs_destroy(vfs);
        if (!ok) return false;
    }
    return true;
}

bool frontend_startup_selection_choices(void *context, qa_ui_library *library, qa_ui_library_field field,
    const char *classname, const qa_ui_library_choice **out, size_t *count, const char **selected, qa_error *e)
{
    frontend_startup_selection *s = context;
    if (!s || !library || !out || !count || !selected || !qa_ui_library_draft(library)) return fail(e, "Startup choices need their draft");
    clear_rows(s); *out = NULL; *count = 0; *selected = "";
    const qa_catalog *catalog = qa_ui_library_catalog(library);
    const qa_launch_choices *c = qa_ui_library_choices(library);
    const qa_launch_equipment *gear = equipment(library);
    bool ok = true;
    switch (field) {
    case QA_UI_LIBRARY_MODEL:
        ok = model_choices(s, library, e); *selected = model_selected(library);
        if (ok && **selected) ok = remember_model(s, role_product(library, QA_ROLE_CHARACTER), *selected, e);
        break;
    case QA_UI_LIBRARY_SEATS: {
        unsigned locals = 0;
        for (size_t i = 0; i < c->seat_count; ++i) locals += c->seats[i].local && !c->seats[i].bot;
        for (unsigned i = 1; ok && i <= 4; ++i) { char id[2] = {(char)('0' + i), 0}; ok = append(s, id, id, NULL, e); }
        char id[16]; snprintf(id, sizeof(id), "%u", locals); *selected = text(s, id, e); break;
    }
    case QA_UI_LIBRARY_WEAPONS: {
        const qa_product *p = role_product(library, QA_ROLE_ARSENAL);
        *selected = p && p->id != c->world.preset ? p->key : "native";
        const qa_product *native = qa_catalog_product(catalog, c->world.preset);
        char label[512]; snprintf(label, sizeof(label), "%s (%s) weapons", native->title,
            native->edition == QA_EDITION_RERELEASE ? "rerelease" : native->edition == QA_EDITION_QUAKEWORLD ? "quakeworld" : native->edition == QA_EDITION_DEMO ? "demo" : "classic");
        ok = append(s, "native", label, NULL, e);
        for (size_t i = 0; ok && i < qa_catalog_count(catalog); ++i) {
            const qa_product *candidate = qa_catalog_at(catalog, i);
            if (supports_weapons(candidate)) ok = product_choice(s, candidate, e);
        }
        break;
    }
    case QA_UI_LIBRARY_GRAPPLE: {
        bool found = false;
        const qa_grapple_mechanic mechanics[] = {QA_GRAPPLE_THREEWAVE, QA_GRAPPLE_Q2_CTF, QA_GRAPPLE_LMCTF};
        for (size_t i = 0; i < 3; ++i) { const qa_product *p = hook_source(library, mechanics[i]); found |= p && !unavailable(p); }
        if (!prepare_q3_hooks(s, library, e)) return false;
        found |= s->hook_count != 0;
        *selected = !gear || gear->selection.grapple == QA_GRAPPLE_DISABLED ? "disabled" : gear->selection.binding == QA_EQUIPMENT_OFFHAND ? "offhand" : "slot";
        ok = append(s, "disabled", "Off", NULL, e) && append(s, "slot", "Weapon slot", found ? NULL : "Install a game or mod that provides a hook", e) &&
            append(s, "offhand", "Offhand", found ? NULL : "Install a game or mod that provides a hook", e); break;
    }
    case QA_UI_LIBRARY_GRAPPLE_STYLE: {
        const qa_grapple_mechanic mechanics[] = {QA_GRAPPLE_THREEWAVE, QA_GRAPPLE_Q2_CTF, QA_GRAPPLE_LMCTF};
        const char *labels[] = {"Threewave CTF (Quake 1)", "Threewave CTF (Quake 2)", "LMCTF (Quake 2)"};
        *selected = gear ? hook_id(gear->selection.grapple) : "native";
        for (size_t i = 0; ok && i < 3; ++i) {
            const qa_product *p = hook_source(library, mechanics[i]);
            if (p) ok = append(s, hook_id(mechanics[i]), labels[i], unavailable(p), e);
        }
        if (ok) ok = prepare_q3_hooks(s, library, e);
        for (size_t i = 0; ok && i < s->hook_count; ++i) {
            const application_q3_grapple_definition *definition = application_q3_grapple_profile_definition(s->hooks[i].profile);
            ok = append(s, definition->id, definition->title, NULL, e);
            const qa_launch_provider *actual = gear ? provider(c, gear->grapple_source) : NULL;
            if (gear && gear->selection.grapple == QA_GRAPPLE_Q3 && actual && actual->product == s->hooks[i].product) *selected = definition->id;
        }
        break;
    }
    case QA_UI_LIBRARY_GRENADES:
        *selected = gear && gear->selection.grenades.enabled ? "enabled" : "disabled";
        ok = append(s, "disabled", "Off", NULL, e) && append(s, "enabled", "On", grenade_source(library) ? NULL : "Requires Quake II grenade assets", e); break;
    case QA_UI_LIBRARY_ENVIRONMENT: {
        const qa_product *p = qa_catalog_find(catalog, "q2-rerelease-baseq2");
        *selected = c->world.environment == QA_ENVIRONMENT_DISABLED ? "disabled" : c->world.environment == QA_ENVIRONMENT_SELECTED ? "q2-rerelease-baseq2" : "audio-content";
        ok = append(s, "disabled", "Off", NULL, e) && append(s, "audio-content", "Game default", NULL, e) &&
            append(s, "q2-rerelease-baseq2", "Quake II environments", p && p->availability == QA_CONTENT_INSTALLED ? NULL : "Requires Quake II rerelease data", e);
        break;
    }
    case QA_UI_LIBRARY_RULES: {
        const char *ids[] = {"standard", "tag", "deathball", "horde", "ctf", "lmctf"};
        const char *labels[] = {"Standard", "Tag", "DeathBall", "Horde", "Q2 Capture the Flag", "Loki's Minions CTF"};
        *selected = rule_selected(library);
        for (size_t i = 0; ok && i < 6; ++i) ok = append(s, ids[i], labels[i], rule_unavailable(library, ids[i]), e);
        break;
    }
    case QA_UI_LIBRARY_MONSTER_SOURCE:
    case QA_UI_LIBRARY_ENEMIES: {
        const qa_monster_catalog_source *source = monster_source_for(library, "startup:monster-source");
        *selected = source ? source->provider : "native";
        ok = monster_choices(s, library, true, NULL, e);
        if (ok && field == QA_UI_LIBRARY_ENEMIES) {
            const qa_product *native = qa_catalog_product(catalog, c->world.preset);
            char label[512]; snprintf(label, sizeof(label), "%s (%s) authored monsters", native->title,
                native->edition == QA_EDITION_RERELEASE ? "rerelease" : native->edition == QA_EDITION_QUAKEWORLD ? "quakeworld" : native->edition == QA_EDITION_DEMO ? "demo" : "classic");
            s->choices[0].label = text(s, label, e);
            bool custom = false;
            for (size_t i = 0; i < c->monster_count; ++i)
                custom |= !c->monsters[i].map_defined && strcmp(c->monsters[i].instance, "startup:monster-source");
            if (custom) *selected = "custom";
            ok = s->choices[0].label && append(s, "custom", "Custom roster", map_family(library) == QA_GAME_Q3 ? "This map has no supported authored monster roster" : NULL, e);
            if (ok) {
                qa_ui_library_choice custom_choice = s->choices[s->count - 1];
                memmove(s->choices + 2, s->choices + 1, (s->count - 2) * sizeof(*s->choices));
                s->choices[1] = custom_choice;
            }
        }
        break;
    }
    case QA_UI_LIBRARY_MONSTER_CLASS: {
        *selected = classname ? "default" : "native";
        for (size_t i = 0; i < c->monster_count; ++i) if (!strcmp(c->monsters[i].authored_classname, classname ? classname : "")) {
            const qa_launch_monster *m = c->monsters + i;
            if (!strcmp(m->instance, "startup:monster-source")) break;
            const qa_monster_catalog_source *source = monster_source_for(library, m->instance);
            if (m->map_defined) *selected = "native";
            else if (source) { char id[256]; snprintf(id, sizeof(id), "%s/%s", source->provider, m->classname); *selected = text(s, id, e); }
            break;
        }
        /* Build after copying the current ID; append never invalidates text allocations. */
        ok = monster_choices(s, library, false, classname, e); break;
    }
    default: return fail(e, "Startup metadata field has no adapter");
    }
    if (!ok || !*selected) return false;
    *out = s->choices; *count = s->count; return true;
}

static bool select_seats(qa_ui_library *library, unsigned requested, qa_error *e)
{
    qa_launch_draft *draft = qa_ui_library_draft(library);
    size_t i = 0; unsigned kept = 0;
    while (i < qa_ui_library_choices(library)->seat_count) {
        qa_launch_seat seat = qa_ui_library_choices(library)->seats[i];
        if (seat.local && !seat.bot && ++kept > requested) {
            if (!qa_launch_remove_seat(draft, seat.id, e)) return false;
        } else ++i;
    }
    const qa_product *p = role_product(library, QA_ROLE_CHARACTER);
    qa_native_q3_character_declaration declaration;
    if (!p || !qa_native_q3_character_default_declaration(p->family, &declaration, e)) return false;
    char *model = copy_text(model_selected(library), e);
    if (!model) return false;
    if (*model) declaration.model = declaration.head_model = model;
    bool ok = true;
    for (; ok && kept < requested; ++kept) {
        const qa_launch_choices *c = qa_ui_library_choices(library);
        uint32_t id = 0; bool used;
        do {
            used = false;
            for (size_t n = 0; n < c->seat_count; ++n) used |= c->seats[n].id == id;
            if (used) ++id;
        } while (used);
        char name[32]; snprintf(name, sizeof(name), "Player %u", kept + 1);
        qa_launch_seat seat = {.id = id, .name = name, .team = "", .local = true,
            .input_device = kept, .character_model = declaration.model, .character_skin = declaration.skin,
            .character_head_model = declaration.head_model, .character_head_skin = declaration.head_skin};
        ok = qa_launch_set_seat(draft, &seat, e);
    }
    free(model); return ok && frontend_startup_selection_complete(library, e);
}

static qa_mode_kind local_mode(qa_ui_library *library, qa_mode_kind requested)
{
    const qa_launch_choices *choices = qa_ui_library_choices(library);
    const qa_product *product = qa_catalog_product(qa_ui_library_catalog(library), choices->world.preset);
    return product ? frontend_local_mode(product->family, frontend_local_seat_count(choices), requested) : requested;
}

bool frontend_startup_selection_complete(qa_ui_library *library, qa_error *error)
{
    qa_mode_kind current = qa_ui_library_mode_preference(library);
    qa_mode_kind selected = local_mode(library, current);
    return selected == current || qa_ui_library_select_mode(library, selected, error);
}

static bool select_hook(frontend_startup_selection *s, qa_ui_library *library, const char *id, bool placement, qa_error *e)
{
    const qa_launch_equipment *current = equipment(library);
    if (!current) return fail(e, "Hook selection requires the existing equipment owner");
    qa_launch_equipment gear = *current;
    if (placement && !strcmp(id, "disabled")) { gear.selection.grapple = QA_GRAPPLE_DISABLED; gear.grapple_source = ""; }
    else {
        qa_grapple_mechanic mechanic = gear.selection.grapple;
        const q3_hook_metadata *q3 = NULL;
        for (size_t i = 0; i < s->hook_count; ++i) {
            const application_q3_grapple_definition *definition = application_q3_grapple_profile_definition(s->hooks[i].profile);
            const qa_launch_provider *actual = provider(qa_ui_library_choices(library), gear.grapple_source);
            if ((!placement && !strcmp(id, definition->id)) || (placement && mechanic == QA_GRAPPLE_Q3 && actual && actual->product == s->hooks[i].product)) q3 = s->hooks + i;
        }
        if (!placement) mechanic = !strcmp(id, "q1-threewave") ? QA_GRAPPLE_THREEWAVE :
            !strcmp(id, "q2-ctf") ? QA_GRAPPLE_Q2_CTF : q3 ? QA_GRAPPLE_Q3 : QA_GRAPPLE_LMCTF;
        if (mechanic == QA_GRAPPLE_DISABLED) {
            const qa_grapple_mechanic candidates[] = {QA_GRAPPLE_THREEWAVE, QA_GRAPPLE_Q2_CTF, QA_GRAPPLE_LMCTF};
            for (size_t i = 0; i < 3; ++i) { const qa_product *p = hook_source(library, candidates[i]); if (p && !unavailable(p)) { mechanic = candidates[i]; break; } }
            if (mechanic == QA_GRAPPLE_DISABLED && s->hook_count) { q3 = s->hooks; mechanic = QA_GRAPPLE_Q3; }
        }
        const qa_product *p = q3 ? qa_catalog_product(qa_ui_library_catalog(library), q3->product) : hook_source(library, mechanic);
        if (!p || unavailable(p)) return fail(e, "Select an installed hook style");
        /* Copy the existing equipment owner fields before provider insertion
         * expires its borrowed view; launch setters retain all text. */
        char *instance = copy_text(gear.instance, e), *grenade = copy_text(gear.grenade_source, e);
        if (!instance || !grenade) { free(instance); free(grenade); return false; }
        gear.instance = instance; gear.grenade_source = grenade; gear.grapple_source = "startup:grapple";
        gear.selection.grapple = mechanic;
        if (placement) gear.selection.binding = !strcmp(id, "offhand") ? QA_EQUIPMENT_OFFHAND : QA_EQUIPMENT_WEAPON_SLOT;
        gear.selection.release_on_teleport = true;
        bool ok = install_source(library, p, gear.grapple_source, e);
        if (ok && q3) {
            qa_launch_provider selected = *provider(qa_ui_library_choices(library), gear.grapple_source);
            selected.runtime = QA_PROGRAM_QVM;
            selected.artifact = application_q3_grapple_profile_path(q3->profile);
            ok = qa_launch_set_provider(qa_ui_library_draft(library), &selected, e);
        }
        if (ok) ok = qa_launch_set_equipment(qa_ui_library_draft(library), &gear, e);
        free(instance); free(grenade); return ok;
    }
    return qa_launch_set_equipment(qa_ui_library_draft(library), &gear, e);
}

static bool select_grenades(qa_ui_library *library, const char *id, qa_error *e)
{
    const qa_launch_equipment *current = equipment(library);
    if (!current) return fail(e, "Grenade selection requires the existing equipment owner");
    qa_launch_equipment gear = *current;
    if (!strcmp(id, "disabled")) {
        gear.selection.grenades.enabled = false;
        return qa_launch_set_equipment(qa_ui_library_draft(library), &gear, e);
    }
    const qa_product *p = grenade_source(library);
    if (!p) return fail(e, "Offhand grenades require Quake II grenade assets");
    char *instance = copy_text(gear.instance, e), *grapple = copy_text(gear.grapple_source, e);
    if (!instance || !grapple) { free(instance); free(grapple); return false; }
    gear.instance = instance; gear.grapple_source = grapple; gear.grenade_source = "startup:grenades";
    gear.selection.grenades = (qa_q2_hand_grenade_options){.enabled = true, .initial_ammo = 5, .capacity = 50};
    bool ok = install_source(library, p, gear.grenade_source, e) && qa_launch_set_equipment(qa_ui_library_draft(library), &gear, e);
    free(instance); free(grapple); return ok;
}

static bool select_rule(qa_ui_library *library, const char *id, qa_mode_kind base_mode, qa_error *e)
{
    const qa_launch_mode *current = mode(library);
    if (!current) return fail(e, "Match rule selection requires its canonical mode");
    qa_launch_mode selected = *current;
    const qa_product *p = qa_catalog_product(qa_ui_library_catalog(library), qa_ui_library_choices(library)->world.preset);
    qa_mode_kind kind = !strcmp(id, "tag") ? QA_MODE_TAG : !strcmp(id, "deathball") ? QA_MODE_DEATHBALL :
        !strcmp(id, "horde") ? QA_MODE_HORDE : (!strcmp(id, "ctf") || !strcmp(id, "lmctf")) ? QA_MODE_CTF : base_mode;
    qa_mode_source source = !strcmp(id, "tag") ? QA_MODE_Q2_TAG : !strcmp(id, "deathball") ? QA_MODE_Q2_DEATHBALL :
        !strcmp(id, "horde") ? QA_MODE_Q1_HORDE : !strcmp(id, "ctf") ? QA_MODE_Q2_CTF : !strcmp(id, "lmctf") ? QA_MODE_LMCTF :
        p->family == QA_GAME_Q3 ? (!strcmp(p->campaign, "missionpack") ? QA_MODE_TEAM_ARENA : QA_MODE_Q3) :
        p->family == QA_GAME_Q2 ? QA_MODE_Q2 : !strcmp(p->campaign, "rogue") ? QA_MODE_ROGUE : QA_MODE_Q1;
    char *instance = copy_text(kind == QA_MODE_CTF ? selected.instance : "native:primary", e);
    const char *teams[3] = {NULL, NULL, NULL}; bool ok = instance != NULL;
    for (size_t i = 0; ok && i < 3; ++i) { teams[i] = copy_text(selected.teams[i] ? selected.teams[i] : "", e); ok = teams[i] != NULL; }
    char *forced = ok ? copy_text(selected.forced_team ? selected.forced_team : "", e) : NULL;
    ok = ok && forced;
    if (ok && kind == QA_MODE_CTF) {
        const qa_product *rules = qa_catalog_find(qa_ui_library_catalog(library), !strcmp(id, "lmctf") ? "q2-classic-lmctf" : "q2-classic-ctf");
        ok = install_source(library, rules, "startup:rules", e);
        if (ok) { free(instance); instance = copy_text("startup:rules", e); ok = instance != NULL; }
    }
    if (ok) {
        selected.instance = instance; selected.rules = qa_mode_defaults(source, kind);
        selected.rules.q2_rerelease = p->family == QA_GAME_Q2 && p->edition == QA_EDITION_RERELEASE;
        for (size_t i = 0; i < 3; ++i) selected.teams[i] = teams[i];
        selected.forced_team = forced;
        /* Replacing the mode's source keeps one primary mode in the draft. */
        char *previous = copy_text(mode(library)->instance, e);
        ok = previous && qa_launch_set_mode(qa_ui_library_draft(library), &selected, e);
        if (ok && strcmp(previous, instance)) ok = qa_launch_remove_mode(qa_ui_library_draft(library), previous, e);
        free(previous);
    }
    free(instance); free(forced); for (size_t i = 0; i < 3; ++i) free((void *)teams[i]); return ok;
}

static bool refresh_native_equipment(qa_ui_library *library, qa_error *e)
{
    const qa_launch_equipment *current = equipment(library);
    if (!current || (strcmp(current->grapple_source, "native:primary") && strcmp(current->grapple_source, "startup:rules"))) return true;
    qa_launch_equipment selected = *current;
    const qa_launch_mode *rules = mode(library);
    const qa_launch_provider *source = rules && rules->rules.kind == QA_MODE_CTF ? provider(qa_ui_library_choices(library), rules->instance) :
        provider(qa_ui_library_choices(library), "native:primary");
    if (!source) return fail(e, "Native equipment lost its selected source");
    qa_launch_draft *defaults = NULL;
    if (!qa_launch_draft_create(qa_launch_draft_catalog(qa_ui_library_draft(library)), source->product, "", &defaults, e)) return false;
    const qa_launch_choices *c = qa_launch_draft_choices(defaults);
    if (!c->equipment_count) { qa_launch_draft_destroy(defaults); return fail(e, "Native source has no equipment defaults"); }
    const qa_equipment_selection *native = &c->equipment[0].selection;
    selected.selection.grapple = native->grapple; selected.selection.binding = native->binding;
    selected.selection.retain_on_weapon_change = native->retain_on_weapon_change;
    selected.selection.release_on_jump = native->release_on_jump;
    selected.selection.release_on_teleport = native->release_on_teleport;
    selected.grapple_source = source->instance;
    bool ok = qa_launch_set_equipment(qa_ui_library_draft(library), &selected, e);
    qa_launch_draft_destroy(defaults); return ok;
}

static const qa_launch_monster *monster_override(qa_ui_library *library, const char *classname)
{
    const qa_launch_choices *c = qa_ui_library_choices(library);
    for (size_t i = 0; i < c->monster_count; ++i)
        if (!strcmp(c->monsters[i].authored_classname, classname ? classname : "")) return c->monsters + i;
    return NULL;
}

static bool native_monster(qa_ui_library *library, const char *classname, qa_error *e)
{
    return qa_launch_set_monster(qa_ui_library_draft(library), &(qa_launch_monster){
        .authored_classname = classname ? classname : "", .instance = "", .classname = "", .map_defined = true}, e);
}

static bool select_monster_source(qa_ui_library *library, const char *id, qa_error *e)
{
    const qa_monster_catalog_source *source = qa_monster_catalog_source_find(id);
    bool native = !strcmp(id, "native");
    if (!native && !source) return fail(e, "Unknown monster source");
    if (source && !install_source(library, monster_product(library, source), "startup:monster-source", e)) return false;
    size_t count;
    const qa_monster_catalog_slot *slots = qa_monster_catalog_slots(map_family(library), &count);
    for (size_t i = 0; i < count; ++i) {
        const qa_launch_monster *current = monster_override(library, slots[i].classname);
        if (current && strcmp(current->instance, "startup:monster-source")) continue;
        const qa_monster_catalog_creature *creature = source ? qa_monster_catalog_default(map_family(library), source, slots[i].classname) : NULL;
        qa_launch_monster selected = {.authored_classname = slots[i].classname, .instance = "startup:monster-source",
            .classname = creature ? creature->classname : "", .map_defined = creature == NULL};
        if (!qa_launch_set_monster(qa_ui_library_draft(library), &selected, e)) return false;
    }
    return !native || qa_launch_remove_provider(qa_ui_library_draft(library), "startup:monster-source", e);
}

static bool select_monster(qa_ui_library *library, const char *classname, const char *id, qa_error *e)
{
    if (!strcmp(id, "native")) return native_monster(library, classname, e);
    if (!strcmp(id, "default")) {
        const qa_monster_catalog_source *source = monster_source_for(library, "startup:monster-source");
        const qa_monster_catalog_creature *creature = source ? qa_monster_catalog_default(map_family(library), source, classname) : NULL;
        return qa_launch_set_monster(qa_ui_library_draft(library), &(qa_launch_monster){.authored_classname = classname ? classname : "",
            .instance = "startup:monster-source", .classname = creature ? creature->classname : "", .map_defined = !creature}, e);
    }
    size_t count; const qa_monster_catalog_source *sources = qa_monster_catalog_sources(&count);
    const qa_monster_catalog_source *source = NULL; const char *target = NULL;
    for (size_t i = 0; i < count; ++i) {
        size_t length = strlen(sources[i].provider);
        if (!strncmp(id, sources[i].provider, length) && id[length] == '/') { source = sources + i; target = id + length + 1; break; }
    }
    if (!source || !qa_monster_catalog_creature_find(source, target)) return fail(e, "Unknown monster roster choice");
    char instance[256]; snprintf(instance, sizeof(instance), "startup:monster:%s", source->provider);
    return install_source(library, monster_product(library, source), instance, e) &&
        qa_launch_set_monster(qa_ui_library_draft(library), &(qa_launch_monster){
            .authored_classname = classname ? classname : "", .instance = instance, .classname = target}, e);
}

bool frontend_startup_selection_select(void *context, qa_ui_library *library, qa_ui_library_field field,
    const char *classname, const char *choice, qa_error *e)
{
    frontend_startup_selection *s = context;
    if (!s || !library || !choice || !qa_ui_library_draft(library)) return fail(e, "Startup selection needs its draft");
    char *id = copy_text(choice, e), *class_copy = classname ? copy_text(classname, e) : NULL;
    if (!id || (classname && !class_copy)) { free(id); free(class_copy); return false; }
    bool ok;
    if (field == QA_UI_LIBRARY_CHARACTER) {
        clear_rows(s);
        const qa_product *p = role_product(library, QA_ROLE_CHARACTER);
        qa_native_q3_character_declaration declaration;
        ok = p && qa_native_q3_character_default_declaration(p->family, &declaration, e) && model_choices(s, library, e);
        const char *preferred = ok ? declaration.model : "";
        for (size_t i = 0; p && i < s->model_count; ++i) if (!strcmp(s->models[i].product, p->identity)) preferred = s->models[i].model;
        const char *selected = s->count ? s->choices[0].id : "";
        for (size_t i = 0; i < s->count; ++i) if (!strcmp(s->choices[i].id, preferred)) selected = preferred;
        if (ok) ok = apply_model(library, selected, e) && remember_model(s, p, selected, e);
        free(id); free(class_copy); return ok;
    }
    if (field == QA_UI_LIBRARY_MODE) {
        qa_mode_kind base_mode = local_mode(library, !strcmp(id, "singleplayer") ? QA_MODE_SINGLE_PLAYER : !strcmp(id, "coop") ? QA_MODE_COOPERATIVE : QA_MODE_FFA);
        ok = !strcmp(id, "singleplayer") || !strcmp(id, "coop") || !strcmp(id, "deathmatch");
        if (!ok) fail(e, "Unknown game mode");
        const char *rule = rule_selected(library);
        if (ok && rule_unavailable_for(library, rule, base_mode)) rule = "standard";
        if (ok) ok = select_rule(library, rule, base_mode, e) && qa_ui_library_mode_preference_set(library, base_mode, e) && refresh_native_equipment(library, e);
        free(id); free(class_copy); return ok;
    }
    if (field == QA_UI_LIBRARY_PRODUCT) {
        const char *rule = rule_selected(library);
        if (rule_unavailable(library, rule)) rule = "standard";
        ok = select_rule(library, rule, qa_ui_library_mode_preference(library), e) && refresh_native_equipment(library, e);
        if (ok && map_family(library) == QA_GAME_Q3) {
            size_t count = qa_ui_library_choices(library)->monster_count;
            for (size_t i = 0; ok && i < count; ++i) {
                char *name = copy_text(qa_ui_library_choices(library)->monsters[i].authored_classname, e);
                ok = name && native_monster(library, name, e); free(name);
            }
            if (ok) ok = qa_launch_remove_provider(qa_ui_library_draft(library), "startup:monster-source", e);
        }
        free(id); free(class_copy); return ok;
    }
    const qa_ui_library_choice *choices; size_t count; const char *selected;
    ok = frontend_startup_selection_choices(s, library, field, class_copy, &choices, &count, &selected, e);
    bool found = false;
    for (size_t i = 0; ok && i < count; ++i) if (!strcmp(choices[i].id, id)) {
        found = true; if (choices[i].unavailable) ok = fail(e, choices[i].unavailable); break;
    }
    if (ok && !found) ok = fail(e, "Unknown startup selection");
    if (ok) switch (field) {
    case QA_UI_LIBRARY_MODEL:
        ok = apply_model(library, id, e) && remember_model(s, role_product(library, QA_ROLE_CHARACTER), id, e); break;
    case QA_UI_LIBRARY_SEATS: ok = select_seats(library, (unsigned)(id[0] - '0'), e); break;
    case QA_UI_LIBRARY_WEAPONS:
        if (!strcmp(id, "native")) ok = qa_launch_bind(qa_ui_library_draft(library), &(qa_launch_binding){
            .scope = {.kind = QA_SCOPE_DEFAULT_PLAYER}, .role = QA_ROLE_ARSENAL, .instance = "native:primary"}, e);
        else ok = frontend_launch_overlay(qa_ui_library_draft(library), id, QA_ROLE_BIT(QA_ROLE_ARSENAL),
            "startup:weapons", (qa_launch_scope){.kind = QA_SCOPE_DEFAULT_PLAYER}, e);
        break;
    case QA_UI_LIBRARY_GRAPPLE: ok = select_hook(s, library, id, true, e); break;
    case QA_UI_LIBRARY_GRAPPLE_STYLE: ok = select_hook(s, library, id, false, e); break;
    case QA_UI_LIBRARY_GRENADES: ok = select_grenades(library, id, e); break;
    case QA_UI_LIBRARY_ENVIRONMENT: {
        qa_launch_world world = qa_ui_library_choices(library)->world;
        world.environment = !strcmp(id, "disabled") ? QA_ENVIRONMENT_DISABLED : !strcmp(id, "audio-content") ? QA_ENVIRONMENT_AUDIO_SOURCE : QA_ENVIRONMENT_SELECTED;
        world.environment_product = QA_PRODUCT_NONE; world.environment_path = "";
        if (world.environment == QA_ENVIRONMENT_SELECTED) {
            const qa_product *p = qa_catalog_find(qa_ui_library_catalog(library), id);
            world.environment_product = p->id; world.environment_path = "sound/default.environments";
        }
        ok = qa_launch_set_world(qa_ui_library_draft(library), &world, e); break;
    }
    case QA_UI_LIBRARY_RULES:
        ok = select_rule(library, id, qa_ui_library_mode_preference(library), e) && refresh_native_equipment(library, e); break;
    case QA_UI_LIBRARY_ENEMIES:
        if (!strcmp(id, "custom")) break;
        /* Native resets both source mappings and explicit roster overrides. */
        if (!strcmp(id, "native")) {
            size_t n = qa_ui_library_choices(library)->monster_count;
            for (size_t i = 0; ok && i < n; ++i) {
                char *name = copy_text(qa_ui_library_choices(library)->monsters[i].authored_classname, e);
                ok = name && native_monster(library, name, e); free(name);
            }
        }
        if (ok) ok = select_monster_source(library, id, e);
        break;
    case QA_UI_LIBRARY_MONSTER_SOURCE: ok = select_monster_source(library, id, e); break;
    case QA_UI_LIBRARY_MONSTER_CLASS: ok = select_monster(library, class_copy, id, e); break;
    default: ok = fail(e, "Startup selection field has no adapter"); break;
    }
    free(id); free(class_copy); return ok;
}

static bool roster_append(frontend_startup_selection *s, const char *classname,
    unsigned authored_count, qa_ui_library *library, qa_error *e)
{
    for (size_t i = 0; i < s->roster_count; ++i)
        if (classname && s->roster[i].classname && !strcmp(s->roster[i].classname, classname)) return true;
    char name[128], label[180];
    if (classname) { monster_name(classname, name, sizeof(name)); snprintf(label, sizeof(label), "%s (%u)", name, authored_count); }
    else snprintf(label, sizeof(label), "Unmatched classes");
    const qa_launch_monster *selection = monster_override(library, classname);
    char effective[512]; snprintf(effective, sizeof(effective), "Keep native");
    if (selection && !selection->map_defined) {
        const qa_monster_catalog_source *source = monster_source_for(library, selection->instance);
        monster_name(selection->classname, name, sizeof(name));
        snprintf(effective, sizeof(effective), "%s (%s%s%s %s)%s", name,
            source && source->family == QA_GAME_Q1 ? "Q1" : "Q2", source && strcmp(source->campaign, "id1") && strcmp(source->campaign, "baseq2") ? " " : "",
            source && strcmp(source->campaign, "id1") && strcmp(source->campaign, "baseq2") ? source->campaign : "",
            source && source->edition == QA_EDITION_RERELEASE ? "rerelease" : "classic", strcmp(selection->instance, "startup:monster-source") ? " *" : "");
    }
    const char *class_copy = classname ? text(s, classname, e) : NULL, *title = text(s, label, e), *value = text(s, effective, e);
    if ((classname && !class_copy) || !title || !value) return false;
    qa_ui_library_roster_row *grown = realloc(s->roster, (s->roster_count + 1) * sizeof(*grown));
    if (!grown) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining authored monster rows"); return false; }
    s->roster = grown;
    unsigned *counts = realloc(s->roster_counts, (s->roster_count + 1) * sizeof(*counts));
    if (!counts) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining authored monster counts"); return false; }
    s->roster_counts = counts;
    size_t index = classname ? 1 : 0;
    while (index < s->roster_count) {
        bool present = counts[index] != 0, selected_present = authored_count != 0;
        if (selected_present != present ? selected_present : strcmp(classname, s->roster[index].classname) < 0) break;
        ++index;
    }
    memmove(s->roster + index + 1, s->roster + index, (s->roster_count - index) * sizeof(*s->roster));
    memmove(counts + index + 1, counts + index, (s->roster_count - index) * sizeof(*counts));
    s->roster[index] = (qa_ui_library_roster_row){class_copy, title, value}; counts[index] = authored_count;
    ++s->roster_count; return true;
}

bool frontend_startup_selection_roster(void *context, qa_ui_library *library,
    const qa_ui_library_roster_row **out, size_t *count, const char **source_label, qa_error *e)
{
    frontend_startup_selection *s = context;
    if (!s || !library || !out || !count || !source_label) return fail(e, "Monster roster needs its draft");
    clear_rows(s); *out = NULL; *count = 0;
    if (map_family(library) == QA_GAME_Q3) return fail(e, "This map has no supported authored monster roster");
    const qa_monster_catalog_source *source = monster_source_for(library, "startup:monster-source");
    const qa_product *p = source ? monster_product(library, source) : NULL;
    *source_label = p ? p->title : "Authored campaign monsters";
    qa_vfs *vfs = NULL; qa_resource *resource = NULL; qa_entities entities = {0}; qa_bsp_view bsp;
    const qa_launch_choices *c = qa_ui_library_choices(library);
    bool ok = qa_catalog_open(qa_ui_library_catalog(library), c->world.geometry, &vfs, e) &&
        qa_vfs_acquire(vfs, c->world.map, &resource, NULL, e) && qa_bsp_open(qa_resource_bytes(resource), &bsp, e);
    if (ok) ok = qa_entities_parse(bsp.lumps[QA_BSP_ENTITIES].bytes, QA_ENTITY_Q1, &entities, e);
    if (ok) ok = roster_append(s, NULL, 0, library, e);
    size_t slot_count; const qa_monster_catalog_slot *slots = qa_monster_catalog_slots(map_family(library), &slot_count);
    for (size_t i = 0; ok && i < slot_count; ++i) {
        unsigned found = 0; size_t length = strlen(slots[i].classname);
        for (size_t n = 0; n < entities.count; ++n) { qa_bytes value; if (qa_entity_value(&entities, n, "classname", &value) && value.size == length && !memcmp(value.data, slots[i].classname, length)) ++found; }
        ok = roster_append(s, slots[i].classname, found, library, e);
    }
    for (size_t n = 0; ok && n < entities.count; ++n) {
        qa_bytes value;
        if (!qa_entity_value(&entities, n, "classname", &value) || value.size < 8 || memcmp(value.data, "monster_", 8)) continue;
        char *name = malloc(value.size + 1);
        if (!name) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining authored monster class"); ok = false; break; }
        memcpy(name, value.data, value.size); name[value.size] = 0;
        unsigned found = 0;
        for (size_t i = 0; i < entities.count; ++i) { qa_bytes other; if (qa_entity_value(&entities, i, "classname", &other) && other.size == value.size && !memcmp(other.data, value.data, value.size)) ++found; }
        ok = roster_append(s, name, found, library, e); free(name);
    }
    c = qa_ui_library_choices(library);
    for (size_t i = 0; ok && i < c->monster_count; ++i)
        if (*c->monsters[i].authored_classname) ok = roster_append(s, c->monsters[i].authored_classname, 0, library, e);
    qa_entities_free(&entities); qa_resource_release(resource); qa_vfs_destroy(vfs);
    if (!ok) return false;
    *out = s->roster; *count = s->roster_count; return true;
}
