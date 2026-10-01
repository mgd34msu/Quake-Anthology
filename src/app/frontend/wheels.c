#include "internal.h"
#include "wheels_save.h"

static bool items(void *context, uint32_t id, qa_hud_wheel_mode mode,
                  const qa_hud_wheel_item **out, size_t *count, qa_error *error)
{
    frontend_seat *seat = context; (void)id;
    *out = NULL; *count = 0;
    qa_actor_id actor;
    if (!qa_application_player_actor(seat->frontend->application, seat->id, &actor)) return true;
    qa_inventory *inventory = qa_application_inventory(seat->frontend->application);
    size_t total;
    if (!qa_inventory_item_definitions(inventory, actor, NULL, 0, &total, error)) return false;
    if (total > seat->wheel_capacity) {
        if (total > SIZE_MAX / sizeof(*seat->wheel_definitions) || total > SIZE_MAX / sizeof(*seat->wheel_items))
            return frontend_fail(error, QA_ERROR_MEMORY, "wheel definition storage overflow");
        qa_item_definition *definitions = realloc(seat->wheel_definitions, total * sizeof(*definitions));
        if (!definitions) return frontend_fail(error, QA_ERROR_MEMORY, "allocating wheel definitions");
        seat->wheel_definitions = definitions;
        qa_hud_wheel_item *rows = realloc(seat->wheel_items, total * sizeof(*rows));
        if (!rows) return frontend_fail(error, QA_ERROR_MEMORY, "allocating wheel rows");
        seat->wheel_items = rows; seat->wheel_capacity = total;
    }
    if (!qa_inventory_item_definitions(inventory, actor, seat->wheel_definitions, total, &total, error)) return false;
    size_t bytes = 0;
    for (size_t i = 0; i < total; ++i) {
        const char *label = seat->wheel_definitions[i].label ? seat->wheel_definitions[i].label : "Item";
        size_t length = strlen(label) + 1;
        if (length > SIZE_MAX - bytes) return frontend_fail(error, QA_ERROR_MEMORY, "wheel label storage overflow");
        bytes += length;
    }
    if (bytes > seat->wheel_label_capacity) {
        char *labels = realloc(seat->wheel_labels, bytes);
        if (!labels) return frontend_fail(error, QA_ERROR_MEMORY, "allocating wheel labels");
        seat->wheel_labels = labels; seat->wheel_label_capacity = bytes;
    }
    size_t offset = 0;
    for (size_t i = 0; i < total; ++i) {
        const char *label = seat->wheel_definitions[i].label ? seat->wheel_definitions[i].label : "Item";
        size_t length = strlen(label) + 1;
        memcpy(seat->wheel_labels + offset, label, length);
        seat->wheel_definitions[i].label = seat->wheel_labels + offset; offset += length;
    }
    for (size_t i = 0; i < total; ++i) {
        qa_item_definition definition = seat->wheel_definitions[i];
        if (definition.weapon != (mode == QA_HUD_WHEEL_WEAPONS) || !(definition.actions & QA_ITEM_USE)) continue;
        qa_inventory_entry entry;
        if (!qa_inventory_entry_read(inventory, actor, definition.item, &entry, error)) return false;
        bool ammunition = entry.count > 0;
        double amount = entry.count;
        if (definition.ammo) {
            qa_inventory_entry ammo;
            if (!qa_inventory_entry_read(inventory, actor, definition.ammo, &ammo, error)) return false;
            ammunition = ammo.count > 0; amount = ammo.count;
        }
        seat->wheel_items[(*count)++] = (qa_hud_wheel_item){
            .identity = {.key = definition.item, .item = definition.item, .source_ordinal = (int32_t)i},
            .sort_order = (int32_t)i, .label = definition.label,
            .owned = entry.count > 0, .has_ammunition = ammunition, .has_count = true, .count = amount};
    }
    *out = seat->wheel_items;
    return true;
}
static bool active(void *context, uint32_t id, uint64_t *out, qa_error *error)
{
    frontend_seat *seat = context; (void)id; (void)error;
    *out = 0;
    qa_actor_id actor;
    qa_item_id weapon;
    if (!qa_application_player_actor(seat->frontend->application, seat->id, &actor)) return true;
    if (!qa_application_weapon_read(seat->frontend->application, actor, &weapon, error)) return false;
    *out = weapon; return true;
}
static bool select_item(void *context, uint32_t id, qa_hud_wheel_mode mode,
                        qa_hud_wheel_identity identity, qa_error *error)
{
    frontend_seat *seat = context; (void)id; (void)mode;
    qa_actor_id actor;
    if (!qa_application_player_actor(seat->frontend->application, seat->id, &actor) ||
        identity.key != identity.item || !identity.item)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "wheel selection no longer belongs to a live player");
    return qa_inventory_item_action(qa_application_inventory(seat->frontend->application), actor, identity.item, QA_ITEM_USE, error);
}
bool frontend_wheel_create(frontend_seat *seat, qa_error *error)
{
    qa_hud_wheel_options options = qa_hud_wheel_defaults(seat->id);
    options.context = seat; options.items = items; options.active = active; options.select = select_item;
    return qa_hud_wheel_create(&options, seat->frontend->time_ns, &seat->wheel, error);
}
void frontend_wheel_command(void *context, qa_input_seat *input, bool powerups, bool down)
{
    qa_frontend *frontend = context;
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        frontend_seat *seat = &frontend->seats[i];
        if (seat->input != input || !seat->wheel) continue;
        qa_error error = {0}; bool opened;
        bool ok = down ? qa_hud_wheel_open(seat->wheel,
            powerups ? QA_HUD_WHEEL_POWERUPS : QA_HUD_WHEEL_WEAPONS, &opened, &error) :
            qa_hud_wheel_close(seat->wheel, true, &error);
        if (!ok) frontend_print(frontend, error.message);
    }
}
static bool wheel_item(void *context, qa_hud_wheel_mode mode, uint64_t item,
    uint64_t *out, qa_error *error)
{
    frontend_seat *seat = context;
    qa_frontend *frontend = seat ? seat->frontend : NULL;
    if (!frontend || !frontend->application || !frontend->seats || !out ||
        seat->id >= frontend->options.seats || frontend->seats + seat->id != seat || !seat->wheel ||
        mode > QA_HUD_WHEEL_POWERUPS || !item || item > UINT32_MAX ||
        !qa_strings_cstr(qa_session_strings(qa_application_session(frontend->application)), (qa_string_id)item))
        return frontend_fail(error, QA_ERROR_FORMAT, "Wheel item lacks its actual stable seat and saved item-string identity");
    /* This producer uses the inventory's item string as its key. The complete
     * string foundation preserves ordinals, including retired item names. */
    *out = item; return true;
}
qa_hud_wheel_checkpoint_refs frontend_wheel_refs(frontend_seat *seat)
{ return (qa_hud_wheel_checkpoint_refs){seat, wheel_item, wheel_item}; }
