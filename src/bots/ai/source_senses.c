#include "internal.h"
#include "source_view.h"
#include "source_player.h"
#include "source_combat_vectors.h"

static void memory_release(qa_bots *b,size_t slot)
{
    bot_actor_memory *row=qa_pool_at(&b->actor_memory,slot);
    qa_actor_index_remove(&b->actor_memory_index,row->actor);
    size_t moved=b->actor_memory_active[--b->actor_memory_count];
    b->actor_memory_active[row->active]=moved;
    ((bot_actor_memory *)qa_pool_at(&b->actor_memory,moved))->active=row->active;
    qa_pool_release(&b->actor_memory,slot);
}
bot_actor_memory *bot_ai_memory_read(const qa_bots *b,qa_actor_id actor)
{
    size_t slot;
    return qa_actor_index_find(&b->actor_memory_index,actor,&slot)?
        qa_pool_at(&b->actor_memory,slot):NULL;
}
bot_actor_memory *bot_ai_memory_admit(qa_bots *b,qa_actor_id actor)
{
    if(!b->actor_memory.capacity || !bot_ai_live(b,actor)) return NULL;
    bot_actor_memory *row=bot_ai_memory_read(b,actor);
    if(row) return row;
    const qa_actor_index_entry *old=b->actor_memory_index.entries+actor.slot;
    if(old->actor.registry) memory_release(b,old->value);
    size_t slot;row=qa_pool_take(&b->actor_memory,&slot);
    if(!row) return NULL;
    qa_stamp_set_restore_mark(&b->sense_visits,slot,0);
    qa_stamp_set_restore_mark(&b->sound_contacts,slot,0);
    *row=(bot_actor_memory){.actor=actor,.active=b->actor_memory_count};
    b->actor_memory_active[b->actor_memory_count++]=slot;
    qa_actor_index_put(&b->actor_memory_index,actor,slot);
    return row;
}
static bool memory_needed(const qa_bots *b,const bot_actor_memory *row)
{
    if(row->aware) return true;
    for(size_t i=0;i<64;++i) if(row->unreachable_until[i]>b->time) return true;
    return false;
}
void bot_ai_senses_clear_client(qa_bots *b,uint32_t client,bool cooldowns)
{
    uint64_t bit=UINT64_C(1)<<client;
    for(size_t i=0;i<b->actor_memory_count;) {
        size_t slot=b->actor_memory_active[i];
        bot_actor_memory *row=qa_pool_at(&b->actor_memory,slot);
        row->aware&=~bit;
        if(cooldowns) row->unreachable_until[client]=0;
        if(!memory_needed(b,row)) memory_release(b,slot);
        else ++i;
    }
}
void bot_ai_cooperative_cooldowns_clear(qa_bots *b)
{
    qa_actor_index_clear(&b->actor_memory_index);
    qa_pool_reset(&b->actor_memory);b->actor_memory_count=0;
}
void bot_ai_cooperative_cooldown_set(qa_bots *b,const bot_cooperative_cooldown *value)
{
    bot_actor_memory *row=bot_ai_memory_admit(b,value->actor);
    if(row) row->unreachable_until[value->client]=value->until;
}
bool bot_ai_cooperative_cooldown_next(const qa_bots *b,size_t *cursor,bot_cooperative_cooldown *out)
{
    while(*cursor<b->actor_memory_count*64) {
        size_t at=(*cursor)++;uint32_t client=(uint32_t)(at%64);
        const bot_actor_memory *row=qa_pool_at(&b->actor_memory,b->actor_memory_active[at/64]);
        if(row->unreachable_until[client]>b->time && b->source_cells[client] &&
           b->source_cells[client]->inuse && bot_ai_live(b,row->actor)) {
            *out=(bot_cooperative_cooldown){row->actor,client,row->unreachable_until[client]};return true;
        }
    }
    return false;
}
const qa_bot_awareness *bot_ai_senses_awareness(const qa_bots *b,const bot_ai_state *s,qa_actor_id actor)
{
    const bot_actor_memory *row=bot_ai_memory_read(b,actor);
    return row && (row->aware&(UINT64_C(1)<<s->acquired_source_client))?
        row->awareness+s->acquired_source_client:NULL;
}
bool bot_ai_awareness_next(const qa_bots *b,size_t *cursor,bot_awareness_record *out)
{
    while(*cursor<b->actor_memory_count*64) {
        size_t at=(*cursor)++;uint32_t client=(uint32_t)(at%64);
        const bot_actor_memory *row=qa_pool_at(&b->actor_memory,b->actor_memory_active[at/64]);
        if((row->aware&(UINT64_C(1)<<client)) && b->source_cells[client] &&
           b->source_cells[client]->inuse && bot_ai_live(b,row->actor)) {
            *out=(bot_awareness_record){row->awareness[client],client};return true;
        }
    }
    return false;
}
void bot_ai_awareness_set(qa_bots *b,const bot_awareness_record *value)
{
    bot_actor_memory *row=bot_ai_memory_admit(b,value->value.actor);
    if(row) {
        row->awareness[value->client]=value->value;
        row->aware|=UINT64_C(1)<<value->client;
    }
}
float bot_ai_senses_weapon_cone(qa_bots *b,bot_ai_state *s,qa_item_id weapon)
{
    const qa_bot_source_skill *settings=s->source_settings;
    if(settings->weapon_fov_scalar<=0) return settings->weapons.fov_angle;
    const qa_bot_source_weapon *source=qa_bot_source_weapon_read(b->services.source_knowledge,weapon);
    float ideal=source?source->ideal_fov:0;
    return (ideal>0?ideal:settings->senses.fov_angle)*settings->weapon_fov_scalar;
}
typedef struct hearing_visit {qa_bots *bots;bot_ai_state *state;} hearing_visit;
static bool hear(void *opaque,qa_actor_id actor,const qa_bot_sound *sound,qa_error *error)
{
    hearing_visit *visit=opaque;qa_bots *b=visit->bots;bot_ai_state *s=visit->state;
    (void)error;
    if(!actor.registry || qa_actor_id_equal(actor,s->view.actor) || !bot_ai_live(b,actor) ||
       !qa_bot_sound_audible(sound,bot_ai_origin(s),&s->source_settings->senses,b->time)) return true;
    bot_actor_memory *row=bot_ai_memory_admit(b,actor);
    if(row) {
        size_t slot=b->actor_memory_active[row->active];
        qa_stamp_set_mark(&b->sound_contacts,slot);
    }
    return true;
}
static bool candidate(qa_bots *b,bot_ai_state *s,qa_actor_id actor,qa_bot_player *player,
    qa_bot_entity *entity,bool *present,qa_error *error)
{
    *present=false;
    if(!bot_ai_live(b,actor) || qa_actor_id_equal(actor,s->view.actor)) return true;
    const qa_actor_player *identity=qa_actors_player(qa_session_actors(b->services.shared.session),actor);
    qa_builtin_actor_traits traits={0};
    bool monster=b->services.shared.actor_traits &&
        b->services.shared.actor_traits(b->services.shared.context,actor,&traits) && traits.monster;
    if((!identity || !identity->present) && !monster) return true;
    bool found;
    if(!bot_ai_target(b,s,actor,player,&found,error)) return false;
    if(!found || player->dead || player->observer || !bot_ai_live(b,actor)) return true;
    if(!b->services.entity(b->services.context,actor,entity,error)) return false;
    *present=entity->present && entity->linked && !entity->hidden && bot_ai_live(b,actor);
    return true;
}
bool bot_ai_senses_update(qa_bots *b,bot_ai_state *s,float elapsed,qa_error *error)
{
    if(!s->source_settings) return true;
    if(s->player.dead || s->player.observer) {
        bot_ai_senses_clear_client(b,s->acquired_source_client,false);return true;
    }
    qa_stamp_set_begin(&b->sense_visits);qa_stamp_set_begin(&b->sound_contacts);
    hearing_visit hearing={b,s};
    if(b->services.hearing && !b->services.hearing(b->services.context,s->view.actor,hear,&hearing,error)) return false;
    if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
    qa_item_id selected=0;const qa_bot_weapon_knowledge *weapons;size_t count;void *lease;
    if(!b->services.arsenal(b->services.context,s->view.actor,&weapons,&count,&lease,error)) return false;
    for(size_t i=0;i<count;++i) if(weapons[i].weapon.number==bot_ai_firing_weapon_number(s)) {selected=weapons[i].supply_weapon;break;}
    b->services.arsenal_end(b->services.context,lease);
    qa_bot_weapon_senses cone=s->source_settings->weapons;
    cone.fov_angle=bot_ai_senses_weapon_cone(b,s,selected);
    uint32_t client=s->acquired_source_client;uint64_t bit=UINT64_C(1)<<client;
    qa_bot_navigation *nav=qa_bot_runtime_navigation(b->runtime,(int32_t)s->view.client);
    for(size_t i=0;i<b->entities.count;++i) {
        qa_actor_id actor=b->entities.actors[i];qa_bot_player player={0};qa_bot_entity entity;bool present;
        if(!candidate(b,s,actor,&player,&entity,&present,error)) return false;
        if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
        if(!present) {
            bot_actor_memory *old=bot_ai_memory_read(b,actor);
            if(old && player.dead) old->aware&=~bit;
            continue;
        }
        bot_actor_memory *row=bot_ai_memory_admit(b,actor);if(!row) continue;
        size_t slot=b->actor_memory_active[row->active];qa_stamp_set_mark(&b->sense_visits,slot);
        qa_vec3 center=qa_vec_add(entity.observation.origin,
            qa_vec_scale(qa_vec_add(entity.observation.mins,entity.observation.maxs),.5f));
        qa_vec3 angles=bot_ai_view_angles(s);
        qa_bot_sight_geometry sight=qa_bot_sight(bot_ai_eye(s),angles.x,angles.y,center,
            player.invisible,&s->source_settings->senses,&cone);
        qa_bot_contact contact={.in_sight_fov=sight.in_sight_fov,.in_weapon_fov=sight.in_weapon_fov,
            .audible=qa_stamp_set_test(&b->sound_contacts,slot),.invisible=player.invisible,
            .distance=sight.distance,.origin=center};
        if(sight.within_invisible_range && (sight.in_sight_fov || sight.in_weapon_fov)) {
            qa_trace_result hit;
            if(!qa_bot_navigation_trace(nav,bot_ai_eye(s),center,NULL,s->view.actor,
                1u|0x10000u,&hit,error)) return false;
            if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
            if(!bot_ai_live(b,actor)) continue;
            contact.line_of_sight=hit.fraction>=1 || qa_actor_id_equal(hit.actor,actor);
        }
        if(!(row->aware&bit)) {row->awareness[client]=qa_bot_awareness_new(actor,b->time,center);row->aware|=bit;}
        qa_bot_sense_step(row->awareness+client,&contact,&s->source_settings->senses,&cone,elapsed,b->time);
    }
    for(size_t i=0;i<b->actor_memory_count;) {
        size_t slot=b->actor_memory_active[i];bot_actor_memory *row=qa_pool_at(&b->actor_memory,slot);
        if(!bot_ai_live(b,row->actor)) {memory_release(b,slot);continue;}
        if(row->aware&bit) {
            qa_bot_awareness *awareness=row->awareness+client;
            if(!qa_stamp_set_test(&b->sense_visits,slot)) {
                qa_bot_contact absent={.origin=awareness->last_known_origin};
                qa_bot_sense_step(awareness,&absent,&s->source_settings->senses,&cone,elapsed,b->time);
            }
            if(qa_bot_should_forget(awareness,&s->source_settings->senses,b->time)) row->aware&=~bit;
        }
        if(!memory_needed(b,row)) memory_release(b,slot);
        else ++i;
    }
    return true;
}
bool bot_ai_senses_enemy(qa_bots *b,bot_ai_state *s,qa_actor_id *out,bool *found,qa_error *error)
{
    *found=false;*out=(qa_actor_id){0};float best=-INFINITY;
    for(size_t i=0;i<b->entities.count;++i) {
        qa_actor_id actor=b->entities.actors[i];
        const qa_bot_awareness *awareness=bot_ai_senses_awareness(b,s,actor);
        if(!bot_ai_senses_aware(awareness) || !bot_ai_live(b,actor)) continue;
        qa_bot_player player;qa_bot_entity entity;bool present,same;
        if(!candidate(b,s,actor,&player,&entity,&present,error)) return false;
        if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
        if(!present) continue;
        if(!bot_ai_same_team(b,s,actor,&same,error)) return false;
        if(same || !bot_ai_live(b,actor)) continue;
        float score=4096-qa_vec_length(qa_vec_sub(bot_ai_origin(s),awareness->last_known_origin))+
            awareness->weapon*256;
        const qa_actor_player *identity=qa_actors_player(qa_session_actors(b->services.shared.session),actor);
        if(identity && identity->present) score+=512;
        if(player.carrying_objective) score+=1536;
        if(qa_actor_id_equal(s->view.enemy,actor)) score+=128;
        if(score>best) {*out=actor;best=score;*found=true;}
        if(!s->source_settings->allow_combat) return true;
    }
    return true;
}
