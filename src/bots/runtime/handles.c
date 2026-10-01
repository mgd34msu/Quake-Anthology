#include "internal.h"
#include "../checkpoint_internal.h"
#include <stdio.h>

void bot_runtime_handles_close(qa_bot_runtime *r) {
    for (uint32_t i = 0; i < r->options.maximum_states; ++i) {
        if (r->characters) { qa_bot_character_release(r->characters[i]); r->characters[i] = NULL; }
        if (r->chats) { qa_bot_chat_destroy(r->chats[i]); r->chats[i] = NULL; }
        if (r->weapons) {
            qa_bot_weapon_selector_destroy(r->weapons[i].selector);
            qa_bot_weights_release(r->weapons[i].weights);
            r->weapons[i] = (bot_weapon_state){0};
        }
    }
}
const qa_bot_character *qa_bot_runtime_character(const qa_bot_runtime *r, uint32_t id) {
    return r && id && id <= r->options.maximum_states ? r->characters[id - 1] : NULL;
}
bool qa_bot_runtime_character_load(qa_bot_runtime *r, const char *path, float skill,
                                   uint32_t *out, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    if (!out || !path) return bot_runtime_fail(e, "missing character path/handle output");
    r->busy = true;
    qa_bot_character *character;
    bool ok = qa_bot_character_load(r->library, path, skill, &character, e);
    r->busy = false;
    if (!ok) return false;
    *out = 0;
    uint32_t available = 0;
    for (uint32_t i = 0; i < r->options.maximum_states; ++i) {
        if (r->characters[i] == character) { *out = i + 1; break; }
        if (!available && !r->characters[i]) available = i + 1;
    }
    if (*out || !available) qa_bot_character_release(character);
    else { r->characters[available - 1] = character; *out = available; }
    return true;
}
bool qa_bot_runtime_character_free(qa_bot_runtime *r, uint32_t id, qa_error *e) {
    if (r && r->closed) return true;
    if (!bot_runtime_mutable(r, e)) return false;
    const qa_bot_variable *reload = qa_bot_library_variable(r->library, "bot_reloadcharacters");
    if (!(reload ? reload->value != 0 : r->options.library.reload_characters)) return true;
    if (!qa_bot_runtime_character(r, id)) {
        if (r->services.diagnostic) {
            r->busy = true;
            r->services.diagnostic(r->services.context, QA_SCRIPT_ERROR, "invalid character handle");
            r->busy = false;
        }
        return true;
    }
    qa_bot_character_release(r->characters[id - 1]);
    r->characters[id - 1] = NULL;
    return true;
}
static bot_weapon_state *weapon_state(const qa_bot_runtime *r, uint32_t id, qa_error *e) {
    if (!r || !id || id > r->options.maximum_states || !r->weapons[id - 1].used) {
        bot_runtime_fail(e, "invalid weapon AI handle");
        return NULL;
    }
    return &r->weapons[id - 1];
}
bool qa_bot_runtime_weapon_has_handle(const qa_bot_runtime *r, uint32_t id) {
    return r && id && id <= r->options.maximum_states && r->weapons[id - 1].used;
}
bool qa_bot_runtime_weapon_allocate(qa_bot_runtime *r, uint32_t *out, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    if (!out) return bot_runtime_fail(e, "missing weapon AI handle output");
    *out = 0;
    for (uint32_t i = 0; i < r->options.maximum_states; ++i)
        if (!r->weapons[i].used) {
            r->weapons[i] = (bot_weapon_state){.used = true};
            *out = i + 1;
            break;
        }
    return true;
}
bool qa_bot_runtime_weapon_free(qa_bot_runtime *r, uint32_t id, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    bot_weapon_state *s = weapon_state(r, id, e);
    if (!s) return false;
    qa_bot_weapon_selector_destroy(s->selector);
    qa_bot_weights_release(s->weights);
    *s = (bot_weapon_state){0};
    return true;
}
bool qa_bot_runtime_weapon_reset(qa_bot_runtime *r, uint32_t id, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    /* Source reset retains its only fields: weights and their index map. */
    return weapon_state(r, id, e) != NULL;
}
bool qa_bot_runtime_weapon_capture(const qa_bot_runtime *r, uint32_t id,
                                     qa_bot_weights **out, qa_error *e) {
    bot_weapon_state *s=weapon_state(r,id,e);
    if(!s || !out) return bot_runtime_fail(e,"missing weapon AI checkpoint output");
    *out=NULL;
    return !s->weights || qa_bot_weights_clone(s->weights,out,e);
}
bool qa_bot_runtime_weapon_restore(qa_bot_runtime *r, uint32_t id,
                                     qa_bot_weights *weights, qa_error *e) {
    if(!bot_runtime_mutable(r,e)) return false;
    bot_weapon_state *s=weapon_state(r,id,e);
    if(!s) return false;
    qa_bot_weapon_selector *selector=NULL;
    if(weights && r->weapon_config &&
       !qa_bot_weapon_selector_create(r->weapon_config,weights,&selector,e)) return false;
    if(weights) qa_bot_weights_retain(weights);
    qa_bot_weapon_selector_destroy(s->selector);qa_bot_weights_release(s->weights);
    s->selector=selector;s->weights=weights;
    return true;
}
struct bot_weapon_restore {
    bot_weapon_state *destination;
    qa_bot_weights *weights;
    qa_bot_weapon_selector *selector;
};
bool bot_weapon_restore_prepare(qa_bot_runtime *r, uint32_t id, qa_bot_weights *weights,
                                bot_weapon_restore **out, qa_error *e) {
    bot_weapon_state *destination=weapon_state(r,id,e);
    if (!destination) return false;
    bot_weapon_restore *prepared=calloc(1,sizeof(*prepared));
    if (!prepared) { qa_error_set(e,QA_ERROR_MEMORY,0,"preparing weapon checkpoint");return false; }
    if (weights && r->weapon_config &&
        !qa_bot_weapon_selector_create(r->weapon_config,weights,&prepared->selector,e)) {
        free(prepared);return false;
    }
    qa_bot_weights_retain(weights);
    prepared->weights=weights;prepared->destination=destination;
    *out=prepared;return true;
}
void bot_weapon_restore_finish(bot_weapon_restore *prepared, bool commit) {
    if (!prepared) return;
    if (commit) {
        qa_bot_weapon_selector_destroy(prepared->destination->selector);
        qa_bot_weights_release(prepared->destination->weights);
        prepared->destination->selector=prepared->selector;
        prepared->destination->weights=prepared->weights;
    } else {
        qa_bot_weapon_selector_destroy(prepared->selector);
        qa_bot_weights_release(prepared->weights);
    }
    free(prepared);
}
static bool weapon_weights(qa_bot_runtime *r, uint32_t id, const char *path, void *context,
                             bool (*read)(void *, const char **, qa_error *),
                             int32_t *result, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    bot_weapon_state *s = weapon_state(r, id, e);
    if (!s) return false;
    if ((!path && !read) || !result) return bot_runtime_fail(e, "missing weapon weight path/result");
    qa_bot_weapon_selector_destroy(s->selector); s->selector = NULL;
    qa_bot_weights_release(s->weights); s->weights = NULL;
    r->busy = true;
    if (read && !read(context, &path, e)) { r->busy = false; return false; }
    if (!path) { r->busy = false; return bot_runtime_fail(e, "missing weapon weight path"); }
    qa_error local = {0};
    bool ok = qa_bot_weights_load(r->library, path, &s->weights, &local);
    if (!ok && (local.code == QA_ERROR_FORMAT || local.code == QA_ERROR_NOT_FOUND)) {
        if (r->services.diagnostic) r->services.diagnostic(r->services.context, QA_SCRIPT_ERROR, local.message);
        *result = 11;
        ok = true;
    } else if (ok && !r->weapon_config) *result = 12;
    else if (ok) {
        ok = qa_bot_weapon_selector_create(r->weapon_config, s->weights, &s->selector, &local);
        if (ok) *result = 0;
    }
    r->busy = false;
    if (!ok && e) *e = local;
    return ok;
}
bool qa_bot_runtime_weapon_weights(qa_bot_runtime *r, uint32_t id, const char *path,
                                    int32_t *result, qa_error *e) {
    return weapon_weights(r, id, path, NULL, NULL, result, e);
}
bool qa_bot_runtime_weapon_weights_from(qa_bot_runtime *r, uint32_t id, void *context,
                                          bool (*read)(void *, const char **, qa_error *),
                                          int32_t *result, qa_error *e) {
    return weapon_weights(r, id, NULL, context, read, result, e);
}
bool qa_bot_runtime_weapon_info(qa_bot_runtime *r, uint32_t id, uint32_t number,
                                 qa_bot_weapon_info *weapon, qa_bot_projectile_info *projectile,
                                 bool *found, qa_error *e) {
    if (!r || !weapon || !projectile || !found) return bot_runtime_fail(e, "missing weapon AI output");
    *found = false;
    if (!r->weapon_config) return true;
    const qa_bot_weapons_view *config = qa_bot_weapons_read(r->weapon_config);
    if (!number || number > config->weapon_capacity) {
        if (r->services.diagnostic) {
            bool previous = r->busy; r->busy = true;
            r->services.diagnostic(r->services.context, QA_SCRIPT_ERROR, "weapon number out of range");
            r->busy = previous;
        }
        return true;
    }
    if (!qa_bot_runtime_weapon_has_handle(r, id)) {
        if (r->services.diagnostic) {
            char text[80];
            int32_t signed_id;
            memcpy(&signed_id, &id, sizeof(id));
            if (!id || id > r->options.maximum_states)
                (void)snprintf(text, sizeof(text), "move state handle %d out of range", signed_id);
            else (void)snprintf(text, sizeof(text), "invalid move state %d", signed_id);
            bool previous = r->busy; r->busy = true;
            r->services.diagnostic(r->services.context, QA_SCRIPT_FATAL, text);
            r->busy = previous;
        }
        return true;
    }
    if (number == config->weapon_capacity)
        return bot_runtime_fail(e, "weapon record exceeds configured allocation");
    *weapon = config->weapons[number];
    *projectile = weapon->valid && weapon->projectile_index < config->projectile_count ?
        config->projectiles[weapon->projectile_index] : (qa_bot_projectile_info){0};
    *found = true;
    return true;
}
bool qa_bot_runtime_weapon_choose(qa_bot_runtime *r, uint32_t id, const int32_t *inventory,
                                   size_t count, uint32_t *out, qa_error *e) {
    qa_bot_inventory_view view = {.data = inventory, .count = count};
    return qa_bot_runtime_weapon_choose_view(r, id, &view, out, e);
}
bool qa_bot_runtime_weapon_choose_view(qa_bot_runtime *r, uint32_t id,
                                        const qa_bot_inventory_view *inventory,
                                        uint32_t *out, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    bot_weapon_state *s = weapon_state(r, id, e);
    if (!s) return false;
    if (!out) return bot_runtime_fail(e, "missing chosen weapon output");
    *out = 0;
    r->busy = true;
    bool ok = !s->selector || qa_bot_weapon_choose_view(s->selector, inventory, out, e);
    r->busy = false;
    return ok;
}
bool qa_bot_runtime_weapon_weight(qa_bot_runtime *r, uint32_t id, uint32_t weapon,
                                   const int32_t *inventory, size_t count, float *out,
                                   bool *found, qa_error *e) {
    qa_bot_inventory_view view={.data=inventory,.count=count};
    return qa_bot_runtime_weapon_weight_view(r,id,weapon,&view,out,found,e);
}
bool qa_bot_runtime_weapon_weight_view(qa_bot_runtime *r,uint32_t id,uint32_t weapon,
                                        const qa_bot_inventory_view *inventory,float *out,
                                        bool *found,qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    bot_weapon_state *s = weapon_state(r, id, e);
    if (!s) return false;
    if (!out || !found) return bot_runtime_fail(e, "missing weapon weight output");
    *found = false;
    r->busy=true;
    bool okay=!s->selector || qa_bot_weapon_weight_view(s->selector,weapon,inventory,out,found,e);
    r->busy=false;
    return okay;
}
bool qa_bot_runtime_chat_allocate(qa_bot_runtime *r, uint32_t *out, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    if (!out || !r->chat_system) return bot_runtime_fail(e, "bot chat requires a live owner and handle output");
    *out = 0;
    for (uint32_t i = 0; i < r->options.maximum_states; ++i)
        if (!r->chats[i]) {
            if (!qa_bot_chat_create(r->chat_system, 0, &r->chats[i], e)) return false;
            *out = i + 1;
            break;
        }
    return true;
}
qa_bot_chat *qa_bot_runtime_chat(qa_bot_runtime *r, uint32_t id) {
    return r && id && id <= r->options.maximum_states ? r->chats[id - 1] : NULL;
}
bool qa_bot_runtime_chat_free(qa_bot_runtime *r, uint32_t id, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    qa_bot_chat *chat = qa_bot_runtime_chat(r, id);
    if (!chat) return bot_runtime_fail(e, "invalid bot chat handle");
    qa_bot_chat_destroy(chat);
    r->chats[id - 1] = NULL;
    return true;
}
bool qa_bot_runtime_chat_load(qa_bot_runtime *r, uint32_t id, const char *path, const char *name,
                               int32_t *result, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    qa_bot_chat *chat = qa_bot_runtime_chat(r, id);
    if (!chat) return bot_runtime_fail(e, "invalid bot chat handle");
    const qa_bot_variable *developer = qa_bot_library_variable(r->library, "bot_developer");
    r->busy = true;
    bool ok = qa_bot_chat_load_initial(chat, r->library, path, name,
        developer && developer->value != 0, result, e);
    r->busy = false;
    return ok;
}
bool qa_bot_runtime_goal_weights(qa_bot_runtime *r, uint32_t id, const char *path,
                                  int32_t *result, qa_error *e) {
    if (!bot_runtime_mutable(r, e)) return false;
    r->busy = true;
    bool ok = qa_bot_goals_load_weights(r->goals, id, r->library, path, result, e);
    r->busy = false;
    return ok;
}
