#include "recipe_private.h"
#include "qa/launch_identity_fields.h"
#include <float.h>
#include <math.h>

bool recipe_fail(qa_error *error, const char *text)
{ qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", text); return false; }
qa_json_id recipe_take(recipe_reader *r)
{
    qa_json_id id = qa_json_at(r->json, r->array, r->next++);
    if (id == QA_JSON_NONE) { r->failed = true; recipe_fail(r->error, "Truncated executable recipe record"); }
    return id;
}
bool recipe_record(recipe_reader *r, qa_json_id id, size_t count)
{
    if (qa_json_type(r->json, id) != QA_JSON_ARRAY || qa_json_size(r->json, id) != count) {
        r->failed = true; return recipe_fail(r->error, "Executable recipe record has unexpected fields");
    }
    r->array = id; r->next = 0; return true;
}
const char *recipe_text(recipe_reader *r)
{
    qa_buffer b = {0};
    if (r->failed || !qa_json_string(r->json, recipe_take(r), &b, r->error)) { r->failed = true; return ""; }
    if (b.size > RECIPE_MAX_BYTES || memchr(b.data, 0, b.size)) {
        qa_buffer_free(&b); r->failed = true; recipe_fail(r->error, "Invalid executable recipe text"); return "";
    }
    char *copy = qa_arena_alloc(&r->recipe->arena, b.size + 1, 1, r->error);
    if (!copy) { qa_buffer_free(&b); r->failed = true; return ""; }
    memcpy(copy, b.data, b.size); copy[b.size] = 0; qa_buffer_free(&b); return copy;
}
const char *recipe_optional_text(recipe_reader *r)
{
    if (qa_json_type(r->json, qa_json_at(r->json, r->array, r->next)) == QA_JSON_NULL) { recipe_take(r); return NULL; }
    return recipe_text(r);
}
uint64_t recipe_word(recipe_reader *r)
{
    const char *text = recipe_text(r); uint64_t value = 0;
    if (!*text || (text[0] == '0' && text[1])) { r->failed = true; recipe_fail(r->error, "Noncanonical recipe word"); }
    for (; !r->failed && *text; ++text) {
        unsigned digit = (unsigned char)*text - '0';
        if (digit > 9 || value > (UINT64_MAX - digit) / 10) { r->failed = true; recipe_fail(r->error, "Recipe word exceeds uint64"); break; }
        value = value * 10 + digit;
    }
    return value;
}
uint32_t recipe_unsigned(recipe_reader *r)
{
    uint64_t value = 0; double numeric = 0; qa_json_id id = recipe_take(r);
    if (r->failed || !qa_json_u64(r->json, id, &value, r->error) || value > UINT32_MAX ||
        !qa_json_number(r->json, id, &numeric, r->error) || signbit(numeric)) {
        r->failed = true; recipe_fail(r->error, "Recipe integer exceeds uint32");
    }
    return (uint32_t)value;
}
int32_t recipe_signed(recipe_reader *r)
{
    int64_t value = 0;
    if (r->failed || !qa_json_i64(r->json, recipe_take(r), &value, r->error) || value < INT32_MIN || value > INT32_MAX) {
        r->failed = true; recipe_fail(r->error, "Recipe integer exceeds int32");
    }
    return (int32_t)value;
}
float recipe_float(recipe_reader *r)
{
    double value = 0;
    if (r->failed || !qa_json_number(r->json, recipe_take(r), &value, r->error) || !isfinite(value) || fabs(value) > FLT_MAX || (double)(float)value != value) {
        r->failed = true; recipe_fail(r->error, "Recipe float is out of range");
    }
    return (float)value;
}
bool recipe_boolean(recipe_reader *r)
{
    bool value = false;
    if (r->failed || !qa_json_bool(r->json, recipe_take(r), &value, r->error)) r->failed = true;
    return value;
}
qa_product_id recipe_product(recipe_reader *r)
{
    const char *identity = recipe_text(r);
    if (!*identity) return QA_PRODUCT_NONE;
    const qa_product *p = qa_catalog_find(r->recipe->catalog, identity);
    if (!p || strcmp(identity, p->identity) || p->availability != QA_CONTENT_INSTALLED) {
        r->failed = true; recipe_fail(r->error, "Recipe content identity is not installed"); return QA_PRODUCT_NONE;
    }
    return p->id;
}
qa_bytes recipe_binary(recipe_reader *r)
{
    const char *text = recipe_text(r); size_t count = strlen(text);
    if (count & 1) { r->failed = true; recipe_fail(r->error, "Odd recipe binary length"); return (qa_bytes){0}; }
    uint8_t *bytes = count ? qa_arena_alloc(&r->recipe->arena, count / 2, 1, r->error) : NULL;
    if (count && !bytes) { r->failed = true; return (qa_bytes){0}; }
    for (size_t i = 0; i < count; ++i) {
        unsigned value = text[i] >= '0' && text[i] <= '9' ? (unsigned)(text[i] - '0') :
            text[i] >= 'a' && text[i] <= 'f' ? (unsigned)(text[i] - 'a' + 10) : 16;
        if (value == 16) { r->failed = true; recipe_fail(r->error, "Noncanonical recipe binary"); break; }
        if (!(i & 1)) bytes[i / 2] = (uint8_t)(value << 4); else bytes[i / 2] |= (uint8_t)value;
    }
    return (qa_bytes){bytes, count / 2};
}
bool recipe_path(const char *path, qa_error *error)
{
    char *normalized = qa_vfs_normalize_path(path, error);
    if (!normalized) return false;
    bool same = !strcmp(path, normalized); free(normalized);
    return same || recipe_fail(error, "Recipe paths must be canonical relative paths");
}
static qa_recipe_actor actor_read(recipe_reader *r)
{
    qa_json_id id = recipe_take(r); qa_recipe_actor result = {0};
    if (qa_json_type(r->json, id) == QA_JSON_NULL) return result;
    recipe_reader a = *r;
    if (!recipe_record(&a, id, 2)) { r->failed = true; return result; }
    result.slot = recipe_unsigned(&a); result.generation = recipe_word(&a); result.present = true;
    if (!result.generation) { a.failed = true; recipe_fail(r->error, "Recipe actor generation is empty"); }
    r->failed |= a.failed; return result;
}
static qa_recipe_scope scope_read(recipe_reader *r)
{
    recipe_reader s = *r; qa_recipe_scope result = {0};
    if (!recipe_record(&s, recipe_take(r), 3)) { r->failed = true; return result; }
    result.kind = recipe_unsigned(&s); result.actor = actor_read(&s); result.seat = recipe_unsigned(&s);
    if ((unsigned)result.kind > QA_SCOPE_SEAT || result.actor.present != (result.kind == QA_SCOPE_ACTOR) ||
        (result.kind != QA_SCOPE_SEAT && result.seat)) { s.failed = true; recipe_fail(r->error, "Invalid recipe scope"); }
    r->failed |= s.failed; return result;
}
static qa_clock_config clock_read(recipe_reader *r)
{
    recipe_reader c = *r; qa_clock_config result = {0};
    if (!recipe_record(&c, recipe_take(r), 7)) { r->failed = true; return result; }
    result.kind = recipe_unsigned(&c); result.initial_time_ns = recipe_word(&c); result.interval_ns = recipe_word(&c);
    result.minimum_frame_ns = recipe_word(&c); result.maximum_frame_ns = recipe_word(&c);
    result.initial_lead_ns = recipe_word(&c); result.maximum_steps = recipe_unsigned(&c);
    r->failed |= c.failed; return result;
}
static qa_mode_rules rules_read(recipe_reader *r)
{
    recipe_reader s = *r; qa_mode_rules v = {0};
    if (!recipe_record(&s, recipe_take(r), QA_LAUNCH_MODE_RULE_FIELD_COUNT)) { r->failed = true; return v; }
#define U(member) v.member = recipe_unsigned(&s);
#define I(member) v.member = recipe_signed(&s);
#define F(member) v.member = recipe_float(&s);
#define B(member) v.member = recipe_boolean(&s);
#define W(member) v.member = recipe_word(&s);
#define FIELD(kind, member) kind(member)
    QA_LAUNCH_MODE_RULE_FIELDS(FIELD)
#undef FIELD
#undef U
#undef I
#undef F
#undef B
#undef W
    r->failed |= s.failed; return v;
}
static void *array_allocate(recipe_reader *r, qa_json_id root, const char *field, size_t size, size_t *count)
{
    qa_json_id array = qa_json_get(r->json, root, field); *count = qa_json_size(r->json, array);
    if (qa_json_type(r->json, array) != QA_JSON_ARRAY || *count > RECIPE_MAX_RECORDS || *count > SIZE_MAX / size) {
        r->failed = true; recipe_fail(r->error, "Invalid recipe choice array"); return NULL;
    }
    void *data = *count ? qa_arena_alloc(&r->recipe->arena, *count * size, _Alignof(max_align_t), r->error) : NULL;
    if (*count && !data) r->failed = true;
    if (data) memset(data, 0, *count * size);
    return data;
}
static bool scope_equal(qa_recipe_scope a, qa_recipe_scope b)
{
    return a.kind == b.kind && a.seat == b.seat && a.actor.present == b.actor.present &&
        a.actor.slot == b.actor.slot && a.actor.generation == b.actor.generation;
}
static const qa_launch_provider *provider(const qa_recipe_choices *c, const char *name)
{
    for (size_t i = 0; i < c->provider_count; ++i) if (!strcmp(c->providers[i].instance, name)) return &c->providers[i];
    return NULL;
}
static bool choices_validate(qa_executable_recipe *recipe, qa_error *error)
{
    qa_recipe_choices *c = &recipe->choices; qa_launch_draft *draft = NULL;
    if (!qa_launch_draft_create_empty(recipe->catalog, &draft, error)) return false;
    bool ok = qa_launch_set_world(draft, &c->world, error);
    for (size_t i = 0; ok && i < c->provider_count; ++i) {
        for (size_t j = 0; j < i; ++j) if (!strcmp(c->providers[j].instance, c->providers[i].instance)) ok = recipe_fail(error, "Duplicate recipe provider");
        if (ok) ok = qa_launch_set_provider(draft, &c->providers[i], error);
    }
    for (size_t i = 0; ok && i < c->binding_count; ++i) {
        const qa_recipe_binding *b = &c->bindings[i];
        if (!provider(c, b->instance) || (unsigned)b->role >= QA_ROLE_COUNT) { ok = recipe_fail(error, "Recipe binding lacks a real provider or role"); break; }
        if (b->scope.kind == QA_SCOPE_SEAT) {
            bool found = false; for (size_t j = 0; j < c->seat_count; ++j) found |= c->seats[j].selection.id == b->scope.seat;
            if (!found) { ok = recipe_fail(error, "Recipe binding names an absent seat"); break; }
        }
        for (size_t j = 0; j < i; ++j) if (scope_equal(b->scope, c->bindings[j].scope) && b->role == c->bindings[j].role && !strcmp(b->selector, c->bindings[j].selector)) ok = recipe_fail(error, "Duplicate recipe binding");
        if (ok && b->scope.kind != QA_SCOPE_ACTOR) {
            qa_launch_binding native = {.scope = {.kind = b->scope.kind, .seat = b->scope.seat}, .role = b->role,
                .selector = b->selector, .instance = b->instance, .definition = b->definition};
            ok = qa_launch_bind(draft, &native, error);
        }
    }
    for (size_t i = 0; ok && i < c->mod_count; ++i) {
        for (size_t j = 0; j < i; ++j) if (!strcmp(c->mods[j].instance, c->mods[i].instance)) ok = recipe_fail(error, "Duplicate recipe addition instance");
        if (ok) ok = qa_launch_set_mod(draft, &c->mods[i], error);
    }
    for (size_t i = 0; ok && i < c->mode_count; ++i) {
        for (size_t j = 0; j < i; ++j) if (!strcmp(c->modes[j].instance, c->modes[i].instance)) ok = recipe_fail(error, "Duplicate recipe mode instance");
        if (ok) ok = qa_launch_set_mode(draft, &c->modes[i], error);
    }
    for (size_t i = 0; ok && i < c->seat_count; ++i) {
        for (size_t j = 0; j < i; ++j) if (c->seats[j].selection.id == c->seats[i].selection.id ||
            (c->seats[i].actor.present && c->seats[j].actor.present && c->seats[i].actor.slot == c->seats[j].actor.slot && c->seats[i].actor.generation == c->seats[j].actor.generation)) ok = recipe_fail(error, "Duplicate recipe seat or source actor");
        if (ok) ok = qa_launch_set_seat(draft, &c->seats[i].selection, error);
    }
    for (size_t i = 0; ok && i < c->loadout_count; ++i) {
        const qa_recipe_loadout *l = &c->loadout[i];
        for (size_t j = 0; j < i; ++j) if (scope_equal(l->scope, c->loadout[j].scope) && !strcmp(l->item, c->loadout[j].item)) ok = recipe_fail(error, "Duplicate portable loadout key");
        if (!ok) break;
        if (!strchr(l->item, ':') || l->quantity < 0 || (l->override_capacity && (l->capacity < 0 || l->quantity > l->capacity))) { ok = recipe_fail(error, "Invalid portable loadout"); break; }
        if (l->scope.kind != QA_SCOPE_ACTOR) {
            qa_launch_loadout native = {.scope = {.kind = l->scope.kind, .seat = l->scope.seat}, .item = l->item,
                .quantity = l->quantity, .capacity = l->capacity, .override_capacity = l->override_capacity, .drop_on_death = l->drop_on_death};
            ok = qa_launch_set_loadout(draft, &native, error);
        }
    }
    for (size_t i = 0; ok && i < c->monster_count; ++i) {
        for (size_t j = 0; j < i; ++j) if (!strcmp(c->monsters[j].authored_classname, c->monsters[i].authored_classname)) ok = recipe_fail(error, "Duplicate monster replacement key");
        if (ok) ok = qa_launch_set_monster(draft, &c->monsters[i], error);
    }
    if (ok) ok = qa_launch_validate(draft, error);
    for (size_t i = 0; ok && i < c->equipment_count; ++i) {
        const qa_recipe_equipment *e = &c->equipment[i];
        for (size_t j = 0; j < i; ++j) if (scope_equal(e->scope, c->equipment[j].scope) && !strcmp(e->instance, c->equipment[j].instance)) ok = recipe_fail(error, "Duplicate recipe equipment key");
        qa_launch_draft *validation = NULL;
        if (ok) ok = qa_launch_draft_copy(draft, &validation, error);
        qa_launch_equipment native = {.scope = {.kind = e->scope.kind == QA_SCOPE_ACTOR ? QA_SCOPE_DEFAULT_PLAYER : e->scope.kind, .seat = e->scope.seat},
            .instance = e->instance, .grapple_source = e->grapple_source, .grenade_source = e->grenade_source, .selection = e->selection};
        if (ok) ok = qa_launch_set_equipment(validation, &native, error) && qa_launch_validate(validation, error);
        qa_launch_draft_destroy(validation);
    }
    for (size_t i = 0; ok && i < c->behavior_count; ++i) {
        const qa_recipe_behavior *b = &c->behaviors[i];
        for (size_t j = 0; j < i; ++j) if (scope_equal(b->scope, c->behaviors[j].scope) && b->role == c->behaviors[j].role && !strcmp(b->weapon, c->behaviors[j].weapon)) ok = recipe_fail(error, "Duplicate trajectory selection key");
        qa_launch_draft *validation = NULL;
        if (ok) ok = qa_launch_draft_copy(draft, &validation, error);
        qa_launch_weapon_behavior native = {.scope = {.kind = b->scope.kind == QA_SCOPE_ACTOR ? QA_SCOPE_DEFAULT_PLAYER : b->scope.kind, .seat = b->scope.seat},
            .weapon = b->weapon, .instance = b->instance, .behavior = b->behavior, .role = b->role, .enabled = b->enabled};
        if (ok) ok = qa_launch_set_weapon_behavior(validation, &native, error) && qa_launch_validate(validation, error);
        qa_launch_draft_destroy(validation);
    }
    qa_launch_draft_destroy(draft); return ok;
}

bool recipe_choices_read(qa_executable_recipe *recipe, const qa_json_document *json, qa_json_id root, qa_error *error)
{
    if (qa_json_type(json, root) != QA_JSON_OBJECT || qa_json_size(json, root) != 10) return recipe_fail(error, "Invalid executable choices object");
    recipe_reader r = {.json = json, .recipe = recipe, .error = error}; qa_recipe_choices *c = &recipe->choices;
    if (!recipe_record(&r, qa_json_get(json, root, "world"), 14)) return false;
    c->world.preset = recipe_product(&r); c->world.geometry = recipe_product(&r); c->world.presentation = recipe_product(&r);
    c->world.map = recipe_text(&r); c->world.start_command = recipe_text(&r); c->world.explicit_presentation = recipe_boolean(&r);
    c->world.doppler = recipe_boolean(&r); c->world.campaign = recipe_boolean(&r); c->world.environment = recipe_unsigned(&r);
    c->world.environment_product = recipe_product(&r); c->world.environment_path = recipe_text(&r); c->world.skill = recipe_signed(&r);
    c->world.explicit_spawn_point = recipe_boolean(&r); c->world.spawn_point = recipe_text(&r);
#define EACH(field, member, count_member, type, fields) \
    type *member = array_allocate(&r, root, field, sizeof(type), &c->count_member); c->member = member; \
    for (size_t i = 0; !r.failed && i < c->count_member; ++i) { \
        if (!recipe_record(&r, qa_json_at(json, qa_json_get(json, root, field), i), fields)) { break; } type *v = &member[i]
#define END }
    EACH("providers", providers, provider_count, qa_launch_provider, 8);
        v->instance = recipe_text(&r); v->product = recipe_product(&r); v->runtime = recipe_unsigned(&r);
        v->implementation = recipe_text(&r); v->artifact = recipe_text(&r); v->component = recipe_text(&r);
        v->clock = clock_read(&r); v->options = recipe_binary(&r);
    END;
    EACH("bindings", bindings, binding_count, qa_recipe_binding, 5);
        v->scope = scope_read(&r); v->role = recipe_unsigned(&r); v->selector = recipe_text(&r);
        v->instance = recipe_text(&r); v->definition = recipe_text(&r);
    END;
    EACH("mods", mods, mod_count, qa_launch_mod_selection, 3);
        v->instance = recipe_text(&r); v->component = recipe_text(&r); v->enabled = recipe_boolean(&r);
    END;
    EACH("modes", modes, mode_count, qa_launch_mode, 7);
        v->instance = recipe_text(&r); v->rules = rules_read(&r); for (size_t j = 0; j < 3; ++j) v->teams[j] = recipe_text(&r);
        v->forced_team = recipe_text(&r); v->primary_score = recipe_boolean(&r);
    END;
    EACH("equipment", equipment, equipment_count, qa_recipe_equipment, 13);
        v->scope = scope_read(&r); v->instance = recipe_text(&r); v->grapple_source = recipe_text(&r); v->grenade_source = recipe_text(&r);
        v->selection.grapple = recipe_unsigned(&r); v->selection.binding = recipe_unsigned(&r);
        v->selection.retain_on_weapon_change = recipe_boolean(&r); v->selection.release_on_jump = recipe_boolean(&r);
        v->selection.release_on_teleport = recipe_boolean(&r); v->selection.grenades.enabled = recipe_boolean(&r);
        v->selection.grenades.infinite_ammo = recipe_boolean(&r); v->selection.grenades.initial_ammo = recipe_signed(&r); v->selection.grenades.capacity = recipe_signed(&r);
    END;
    EACH("seats", seats, seat_count, qa_recipe_seat, 15);
        v->selection.id = recipe_unsigned(&r); v->actor = actor_read(&r); v->selection.name = recipe_text(&r);
        v->selection.team = recipe_text(&r); v->selection.input_device = recipe_unsigned(&r); v->selection.local = recipe_boolean(&r);
        v->selection.spectator = recipe_boolean(&r); v->selection.bot = recipe_boolean(&r); v->selection.bot_skill = recipe_float(&r);
        v->selection.bot_definition = recipe_optional_text(&r); v->selection.bot_delay_ms = recipe_signed(&r);
        v->selection.character_model = recipe_optional_text(&r); v->selection.character_skin = recipe_optional_text(&r);
        v->selection.character_head_model = recipe_optional_text(&r); v->selection.character_head_skin = recipe_optional_text(&r);
    END;
    EACH("loadout", loadout, loadout_count, qa_recipe_loadout, 6);
        v->scope = scope_read(&r); v->item = recipe_text(&r); v->quantity = recipe_signed(&r); v->capacity = recipe_signed(&r);
        v->override_capacity = recipe_boolean(&r); v->drop_on_death = recipe_boolean(&r);
    END;
    EACH("monsters", monsters, monster_count, qa_launch_monster, 4);
        v->authored_classname = recipe_text(&r); v->instance = recipe_text(&r); v->classname = recipe_text(&r); v->map_defined = recipe_boolean(&r);
    END;
    EACH("behaviors", behaviors, behavior_count, qa_recipe_behavior, 6);
        v->scope = scope_read(&r); v->weapon = recipe_text(&r); v->instance = recipe_text(&r); v->behavior = recipe_text(&r);
        v->role = recipe_unsigned(&r); v->enabled = recipe_boolean(&r);
    END;
#undef EACH
#undef END
    return !r.failed && choices_validate(recipe, error);
}

bool recipe_copy_json(qa_json_writer *w, const qa_json_document *json, qa_json_id id, qa_error *error)
{
    switch (qa_json_type(json, id)) {
    case QA_JSON_OBJECT:
        qa_json_writer_object(w);
        for (size_t i = 0; i < qa_json_size(json, id); ++i) {
            qa_buffer key = {0};
            if (!qa_json_string(json, qa_json_key_at(json, id, i), &key, error)) return false;
            if (memchr(key.data, 0, key.size)) { qa_buffer_free(&key); return recipe_fail(error, "NUL recipe object key"); }
            qa_json_writer_key(w, (const char *)key.data); qa_buffer_free(&key);
            if (!recipe_copy_json(w, json, qa_json_at(json, id, i), error)) return false;
        }
        qa_json_writer_end(w); break;
    case QA_JSON_ARRAY:
        qa_json_writer_array(w);
        for (size_t i = 0; i < qa_json_size(json, id); ++i) if (!recipe_copy_json(w, json, qa_json_at(json, id, i), error)) return false;
        qa_json_writer_end(w); break;
    case QA_JSON_STRING: {
        qa_buffer text = {0}; if (!qa_json_string(json, id, &text, error)) return false;
        qa_json_writer_bytes(w, (qa_bytes){text.data, text.size}); qa_buffer_free(&text); break;
    }
    case QA_JSON_NUMBER: { double number; if (!qa_json_number(json, id, &number, error)) return false; qa_json_writer_number(w, number); break; }
    case QA_JSON_BOOL: { bool value; if (!qa_json_bool(json, id, &value, error)) return false; qa_json_writer_bool(w, value); break; }
    case QA_JSON_NULL: qa_json_writer_null(w); break;
    default: return recipe_fail(error, "Invalid recipe JSON value");
    }
    return !w->failed;
}
