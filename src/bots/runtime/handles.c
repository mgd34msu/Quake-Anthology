#include "internal.h"
#include "../checkpoint_internal.h"
#include "../library/character_load.h"
#include "../library/internal.h"
#include "qa/bots_log_consumers.h"
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
    *out = 0;
    r->busy = true;
    qa_bot_character *character;
    bool interpolated;
    bool ok = bot_character_load(r->library, path, skill, &character, &interpolated, e);
    if (!ok) { r->busy = false; return false; }
    uint32_t available = 0, handle = 0;
    for (uint32_t i = 0; i < r->options.maximum_states; ++i) {
        if (r->characters[i] == character) { handle = i + 1; break; }
        if (!available && !r->characters[i]) available = i + 1;
    }
    if (handle || !available) qa_bot_character_release(character);
    else { r->characters[available - 1] = character; handle = available; }
    if (handle && interpolated)
        ok = qa_bot_character_dump(r->library, r->log, r->characters[handle - 1], e);
    if (ok) *out = handle;
    r->busy = false;
    return ok;
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
    r->busy=true;
    if(s->weights && !qa_bot_weights_free(s->weights,e)) {r->busy=false;return false;}
    qa_bot_weapon_selector_destroy(s->selector);
    qa_bot_weights_release(s->weights);
    *s = (bot_weapon_state){0};
    r->busy=false;
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
struct bot_weapon_history {
    qa_bot_weights *weights;
    qa_bot_weapons *config;
    int32_t *indices;
    size_t count;
    bool has_selector;
};
void bot_weapon_history_destroy(bot_weapon_history *image) {
    if(!image) return;
    qa_bot_weights_release(image->weights);qa_bot_weapons_release(image->config);
    free(image->indices);free(image);
}
bool bot_weapon_checkpoint_capture(qa_bot_runtime *runtime,uint32_t id,bot_fuzzy_history *fuzzy,
    bot_weapon_history **out,qa_error *error) {
    bot_weapon_state *state=weapon_state(runtime,id,error);
    if(!state || !out || *out)
        return bot_runtime_fail(error,"Weapon checkpoint requires actual state and empty references");
    bot_weapon_history *image=calloc(1,sizeof(*image));
    if(!image) {qa_error_set(error,QA_ERROR_MEMORY,0,"Capturing actual weapon configuration binding");return false;}
    image->weights=state->weights;qa_bot_weights_retain(image->weights);
    if(state->weights && !bot_fuzzy_history_include(fuzzy,state->weights,error)) goto failed;
    if(state->selector) {
        const qa_bot_weapon_selector *selector=state->selector;
        if(selector->weights!=state->weights || !selector->config || !selector->workspace || selector->workspace->busy) {
            bot_runtime_fail(error,"Weapon checkpoint selector has another configuration or active workspace");goto failed;
        }
        image->has_selector=true;image->config=selector->config;qa_bot_weapons_retain(image->config);
        image->count=selector->config->view.weapon_capacity;
        if(image->count>SIZE_MAX/sizeof(*image->indices) || (image->count && !selector->indices)) {
            bot_runtime_fail(error,"Weapon checkpoint index extent is invalid");goto failed;
        }
        if(image->count) {
            image->indices=malloc(image->count*sizeof(*image->indices));
            if(!image->indices) {qa_error_set(error,QA_ERROR_MEMORY,0,"Capturing actual weapon index mapping");goto failed;}
            memcpy(image->indices,selector->indices,image->count*sizeof(*image->indices));
        }
    }
    *out=image;return true;
failed:
    bot_weapon_history_destroy(image);return false;
}
bool bot_weapon_restore_prepare(qa_bot_runtime *r, uint32_t id,const bot_weapon_history *image,
                                bot_weapon_restore **out, qa_error *e) {
    bot_weapon_state *destination=weapon_state(r,id,e);
    if (!destination) return false;
    if(!image || !out || *out || (image->has_selector && (!image->weights || !image->config ||
       image->count!=image->config->view.weapon_capacity || (image->count && !image->indices))))
        return bot_runtime_fail(e,"Weapon snapshot differs from its actual captured selector");
    bot_weapon_restore *prepared=calloc(1,sizeof(*prepared));
    if (!prepared) { qa_error_set(e,QA_ERROR_MEMORY,0,"preparing weapon checkpoint");return false; }
    if(image->has_selector) {
        prepared->selector=calloc(1,sizeof(*prepared->selector));
        if(!prepared->selector) {qa_error_set(e,QA_ERROR_MEMORY,0,"Preparing captured weapon selector");goto failed;}
        qa_bot_weapon_selector *selector=prepared->selector;
        selector->weights=image->weights;selector->config=image->config;
        qa_bot_weights_retain(selector->weights);qa_bot_weapons_retain(selector->config);
        if(image->count) {
            selector->indices=malloc(image->count*sizeof(*selector->indices));
            if(!selector->indices) {qa_error_set(e,QA_ERROR_MEMORY,0,"Preparing captured weapon indices");goto failed;}
            memcpy(selector->indices,image->indices,image->count*sizeof(*selector->indices));
        }
        if(!qa_bot_weight_workspace_create(&selector->workspace,e)) goto failed;
    }
    qa_bot_weights_retain(image->weights);
    prepared->weights=image->weights;prepared->destination=destination;
    *out=prepared;return true;
failed:
    qa_bot_weapon_selector_destroy(prepared->selector);free(prepared);return false;
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
    r->busy=true;
    if(s->weights && !qa_bot_weights_free(s->weights,e)) {r->busy=false;return false;}
    qa_bot_weapon_selector_destroy(s->selector); s->selector = NULL;
    qa_bot_weights_release(s->weights); s->weights = NULL;
    if (read && !read(context, &path, e)) { r->busy = false; return false; }
    if (!path) { r->busy = false; return bot_runtime_fail(e, "missing weapon weight path"); }
    qa_error local = {0};
    bool source_failure=false;
    bool ok = qa_bot_weights_load_result(r->library, path, &s->weights, &source_failure, &local);
    if (!ok && source_failure) {
        static const char prefix[]="couldn't load weapon config ";size_t length=strlen(path);
        if(length>SIZE_MAX-sizeof(prefix)) {
            qa_error_set(&local,QA_ERROR_MEMORY,0,"Weapon weight diagnostic exceeds native extent");
        } else {
            char *message=malloc(sizeof(prefix)+length);
            if(!message) qa_error_set(&local,QA_ERROR_MEMORY,0,"Retaining weapon weight source diagnostic");
            else {
                memcpy(message,prefix,sizeof(prefix)-1);memcpy(message+sizeof(prefix)-1,path,length+1);
                ok=qa_bot_log_print(r->log,QA_SCRIPT_FATAL,message,&local);free(message);
                if(ok) *result=11;
            }
        }
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
