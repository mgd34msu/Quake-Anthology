#include "bots_private.h"
#include "bots_knowledge.h"
#include "qa/game_q2_bots.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q3_bots.h"
#include <limits.h>
#include <math.h>
#include <string.h>

static double count(application_bots *bots,qa_actor_id actor,qa_item_id item) {
    qa_inventory_entry entry;
    return item && qa_inventory_entry_read(bots->application->inventory,actor,item,&entry,NULL)?entry.count:0;
}
static int32_t integer(double value) {
    if(!isfinite(value) || value==0) return 0;
    double reduced=fmod(trunc(value),4294967296.0);
    if(reduced<0) reduced+=4294967296.0;
    uint32_t bits=(uint32_t)reduced;int32_t result;
    memcpy(&result,&bits,sizeof(result));return result;
}
typedef struct ballistics {
    float damage,speed,cycle,range,radius;
    int pellets,ammo;
    float horizontal,vertical;
    bool melee;
} ballistics;
static const ballistics q2_base[QA_Q2_WEAPON_COUNT]={
    [QA_Q2_BLASTER]={10,1000,.5f,2000,0,1,0},
    [QA_Q2_SHOTGUN]={4,0,1.2f,8192,0,12,1,500,500},
    [QA_Q2_SUPERSHOTGUN]={6,0,1.2f,8192,0,20,2,1000,500},
    [QA_Q2_MACHINEGUN]={8,0,.1f,8192,0,1,1,300,500},
    [QA_Q2_CHAINGUN]={8,0,.1f,8192,0,3,3,300,500},
    [QA_Q2_ROCKETLAUNCHER]={109.5f,650,.9f,8000,120,1,1},
    [QA_Q2_HYPERBLASTER]={20,1000,.1f,2000,0,1,1},
    [QA_Q2_RAILGUN]={150,0,1.6f,8192,0,1,1},
    [QA_Q2_IONRIPPER]={50,500,.3f,1500,0,1,2},
    [QA_Q2_PHALANX]={74.5f,725,1.6f,8000,120,2,1},
    [QA_Q2_ETF_RIFLE]={10,750,.1f,8000,0,1,1},
    [QA_Q2_HEATBEAM]={15,0,.1f,8192,0,1,2}
};
static void describe(qa_bot_weapon_knowledge *out,int source,int slot,qa_item_id weapon,
                     qa_item_id ammo,ballistics fact,bool owned) {
    *out=(qa_bot_weapon_knowledge){.weapon={.valid=true,.number=source,
        .weapon_inventory=64+slot,.ammo_inventory=96+slot,.ammo_amount=fact.ammo,
        .projectile_count=fact.pellets,.reload=fact.cycle,.speed=fact.speed,
        .horizontal_spread=fact.horizontal,.vertical_spread=fact.vertical},
        .projectile={.damage=(int32_t)fact.damage,.radius=fact.radius,.damage_type=1|(fact.radius?2:0)},
        .selected_projectile_damage=fact.damage,
        .maximum_range=fact.range,.ranged_limit=true,.melee=fact.melee,.personality_role=-1,
        .has_supply=true,.supply_weapon=weapon,.supply_ammo=ammo,.ammo_per_shot=fact.ammo,.owned=owned};
}
static bool observe_arsenal(application_bots *bots,qa_actor_id actor,qa_error *error) {
    application_provider *provider=application_provider_for(bots->application,actor,QA_ROLE_ARSENAL,NULL);
    bots->knowledge_count=0;
    uint32_t handle=0;
    if(provider && !qa_bots_source_weapon_handle(bots->population,actor,&handle,error)) return false;
    qa_actor_id observed=application_bots_knowledge_actor(bots,provider,handle);
    if(provider && provider->kind==APPLICATION_PROVIDER_Q3) {
        qa_q3_product product;
        if(!qa_q3_bot_arsenal_product_read(provider->state.q3,&product,error)) return false;
        int maximum=product==QA_Q3_ARENA?QA_Q3_W_GRAPPLE:QA_Q3_WEAPON_COUNT-1;
        for(int source=1;source<=maximum;++source) {
            qa_bot_weapon_knowledge value={.personality_role=source,.has_supply=true};bool found;
            if(!qa_bot_runtime_weapon_info(bots->runtime,handle,(uint32_t)source,
                    &value.weapon,&value.projectile,&found,error)) return false;
            if(!found || !value.weapon.valid) continue;
            value.selected_projectile_damage=value.projectile.damage;
            value.supply_weapon=qa_q3_weapon_item(provider->state.q3,(qa_q3_weapon)source,false);
            value.supply_ammo=qa_q3_weapon_item(provider->state.q3,(qa_q3_weapon)source,true);
            value.ammo_per_shot=value.supply_ammo?1:0;
            value.owned=count(bots,observed,value.supply_weapon)>0;
            value.melee=source==QA_Q3_W_GAUNTLET;
            value.ranged_limit=value.melee;
            value.maximum_range=value.melee?60:0;
            value.travel_modes=source==QA_Q3_W_ROCKET?QA_NAV_CAPABILITY(QA_NAV_ROCKET_JUMP):
                source==QA_Q3_W_BFG?QA_NAV_CAPABILITY(QA_NAV_BFG_JUMP):
                source==QA_Q3_W_GRAPPLE?QA_NAV_CAPABILITY(QA_NAV_GRAPPLE):0;
            bots->knowledge[bots->knowledge_count++]=value;
        }
    } else if(provider && provider->kind==APPLICATION_PROVIDER_Q1) {
        for(int source=0;source<=QA_Q1_LIGHTNING;++source) {
            if(source==QA_Q1_GRENADE) continue;
            qa_q1_bot_weapon_fact view;bool found;
            if(!qa_q1_bot_weapon_read(provider->state.q1,observed,(qa_q1_weapon)source,&view,&found,error)) return false;
            if(!found) continue;
            if(bots->knowledge_count>=sizeof(bots->knowledge)/sizeof(*bots->knowledge))
                return application_fail(error,QA_ERROR_MEMORY,"native bot weapon observation capacity exceeded");
            qa_bot_weapon_knowledge *value=&bots->knowledge[bots->knowledge_count++];
            *value=(qa_bot_weapon_knowledge){.weapon={.valid=true,.number=source+1,
                .weapon_inventory=65+source,.ammo_inventory=97+source,
                .ammo_amount=integer(view.ammo_per_shot),.projectile_count=(int32_t)view.shots,
                .reload=(float)view.cycle,.speed=(float)view.speed,.offset=view.offset,
                .horizontal_spread=(float)(atan(view.spread_x)*(180.0/3.14159265358979323846)/6),
                .vertical_spread=(float)(atan(view.spread_y)*(180.0/3.14159265358979323846)/6)},
                .projectile={.damage=integer(view.damage),.radius=(float)view.radius,
                    .damage_type=1|(view.radius>0?2:0)},
                .selected_projectile_damage=view.damage,
                .maximum_range=(float)view.range,.ranged_limit=true,.melee=source==QA_Q1_AXE,
                .personality_role=-1,.has_supply=true,.owned=view.owned,
                .supply_weapon=view.item,.supply_ammo=view.ammo,.ammo_per_shot=view.ammo_per_shot};
            if(source==QA_Q1_ROCKET)
                value->travel_modes=QA_NAV_CAPABILITY(QA_NAV_ROCKET_JUMP);
        }
    } else if(provider && provider->kind==APPLICATION_PROVIDER_Q2) {
        qa_q2_bot_arsenal_configuration configuration;
        if(!qa_q2_bot_arsenal_configuration_read(provider->state.q2,&configuration,error)) return false;
        bool rerelease=configuration.edition==QA_Q2_RERELEASE,deathmatch=configuration.deathmatch;
        uint32_t registered_count;
        if(!qa_q2_bot_arsenal_definition_count(provider->state.q2,&registered_count,error)) return false;
        for(uint32_t ordinal=0;ordinal<registered_count;++ordinal) {
            const qa_q2_weapon_definition *definition;
            if(!qa_q2_bot_arsenal_definition_read(provider->state.q2,ordinal,&definition,error)) return false;
            int source=definition->weapon;
            ballistics fact=q2_base[source];
            if(!definition || !fact.pellets) continue;
            if(source==QA_Q2_BLASTER) {fact.damage=rerelease || deathmatch?15:10;fact.speed=rerelease?1500:1000;fact.range=rerelease?3000:2000;}
            if(source==QA_Q2_CHAINGUN) fact.damage=deathmatch?6:8;
            if(source==QA_Q2_HYPERBLASTER) fact.damage=deathmatch?15:20;
            if(source==QA_Q2_RAILGUN) fact.damage=deathmatch?100:rerelease?125:150;
            if(source==QA_Q2_IONRIPPER) fact.damage=deathmatch?30:50;
            if(source==QA_Q2_IONRIPPER) fact.horizontal=tanf(3.14159265358979323846f/180)*8192;
            if(source==QA_Q2_PHALANX) fact.horizontal=tanf(1.5f*3.14159265358979323846f/180)*8192;
            if(source==QA_Q2_ETF_RIFLE) fact.speed=rerelease?1150:750;
            const qa_q2_item_definition *item=qa_q2_item_lookup(provider->state.q2,definition->item);
            if(!item) return application_fail(error,QA_ERROR_FORMAT,"Q2 registered weapon lost its actual item declaration");
            qa_bot_weapon_knowledge *value=&bots->knowledge[bots->knowledge_count++];
            describe(value,(int)ordinal+1,(int)ordinal+1,item->item,item->ammo,fact,count(bots,observed,item->item)>0);
            if(source==QA_Q2_ROCKETLAUNCHER) value->travel_modes=QA_NAV_CAPABILITY(QA_NAV_ROCKET_JUMP);
            value->weapon.activate=(float)definition->activate_last*.1f;
            if(source==QA_Q2_CHAINGUN) value->weapon.spin_up=1;
            value->weapon.ammo_amount=definition->quantity;value->ammo_per_shot=definition->quantity;
            value->weapon.horizontal_spread=atanf(fact.horizontal/8192)*(180.0f/3.14159265358979323846f)/6;
            value->weapon.vertical_spread=atanf(fact.vertical/8192)*(180.0f/3.14159265358979323846f)/6;
            value->weapon.offset=source==QA_Q2_BLASTER || source==QA_Q2_HYPERBLASTER?qa_v3(24,8,-8):
                source==QA_Q2_IONRIPPER?qa_v3(16,7,-8):source==QA_Q2_ETF_RIFLE?qa_v3(15,8,-8):
                source==QA_Q2_HEATBEAM?qa_v3(7,2,-3):source==QA_Q2_RAILGUN?qa_v3(0,7,-8):
                source==QA_Q2_ROCKETLAUNCHER?qa_v3(8,8,-8):
                qa_v3(0,source==QA_Q2_PHALANX || !rerelease?8:0,-8);
        }
    } else return application_fail(error,QA_ERROR_UNSUPPORTED,"selected bot arsenal has no native observation adapter");
    return true;
}
bool application_bot_arsenal(void *opaque,qa_actor_id actor,const qa_bot_weapon_knowledge **out,
                            size_t *length,void **lease,qa_error *error) {
    application_bots *bots=opaque;
    if(bots->arsenal_leases) {
        application_fail(error,QA_ERROR_ARGUMENT,"bot arsenal observation is already borrowed");
        return false;
    }
    ++bots->arsenal_leases;
    if(!observe_arsenal(bots,actor,error)) {--bots->arsenal_leases;return false;}
    *out=bots->knowledge;*length=bots->knowledge_count;*lease=bots;return true;
}
void application_bot_arsenal_end(void *opaque,void *lease) {
    application_bots *bots=opaque;if(lease==bots && bots->arsenal_leases) --bots->arsenal_leases;
}
bool application_bot_inventory(application_bots *bots,qa_actor_id actor,const qa_bot_inventory_target *inventory,qa_error *error) {
    application_provider *provider=application_provider_for(bots->application,actor,QA_ROLE_ARSENAL,NULL);
    if(!provider || provider->kind>APPLICATION_PROVIDER_Q3)
        return application_fail(error,QA_ERROR_UNSUPPORTED,"Bot inventory has no actual native selected arsenal");
    if(provider->kind==APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state player;
        if(!qa_q3_player_read(provider->state.q3,actor,&player)) actor=(qa_actor_id){0};
    }
    for(int32_t index=0;index<200;++index)
        if(!qa_bot_inventory_write(inventory,index,0,error)) return false;
    qa_combat_state combat;
    bool present=qa_combat_read(bots->application->combat,actor,&combat,NULL);
    if(!qa_bot_inventory_write(inventory,QA_BOT_INV_HEALTH,present?integer(combat.health):0,error) ||
       !qa_bot_inventory_write(inventory,QA_BOT_INV_ARMOR,
           present && combat.armor.regular.kind!=QA_ARMOR_NONE?integer(combat.armor.regular.points):0,error)) return false;
    if(provider->kind==APPLICATION_PROVIDER_Q1) {
        double time,started;bool intermission;
        if(!qa_q1_bot_clock_read(provider->state.q1,&time,&intermission,&started,error)) return false;
        if(!qa_bot_inventory_write(inventory,QA_BOT_INV_QUAD,
            qa_q1_game_power_expires(provider->state.q1,actor,QA_Q1_QUAD)>time,error)) return false;
        for(int source=0;source<=QA_Q1_LIGHTNING;++source) {
            qa_item_id item,ammo;bool covered,usable;
            if(!qa_q1_bot_weapon_items(provider->state.q1,(qa_q1_weapon)source,&item,&ammo,&covered,error)) return false;
            if(!covered) continue;
            if(!qa_q1_bot_weapon_usable(provider->state.q1,actor,(qa_q1_weapon)source,&usable,error)) return false;
            if(!qa_bot_inventory_write(inventory,65+source,usable,error) ||
               !qa_bot_inventory_write(inventory,97+source,integer(count(bots,actor,ammo)),error)) return false;
        }
    } else if(provider->kind==APPLICATION_PROVIDER_Q2) {
        uint32_t registered_count;
        if(!qa_q2_bot_arsenal_definition_count(provider->state.q2,&registered_count,error)) return false;
        for(uint32_t ordinal=0;ordinal<registered_count;++ordinal) {
            const qa_q2_weapon_definition *definition;
            if(!qa_q2_bot_arsenal_definition_read(provider->state.q2,ordinal,&definition,error)) return false;
            if(!q2_base[definition->weapon].pellets) continue;
            const qa_q2_item_definition *item=qa_q2_item_lookup(provider->state.q2,definition->item);
            if(!item) return application_fail(error,QA_ERROR_FORMAT,"Q2 inventory lost its registered weapon declaration");
            if(!qa_bot_inventory_write(inventory,(int32_t)(65+ordinal),count(bots,actor,item->item)>0,error) ||
               !qa_bot_inventory_write(inventory,(int32_t)(97+ordinal),integer(count(bots,actor,item->ammo)),error)) return false;
        }
    } else {
        qa_q3_product product;
        if(!qa_q3_bot_arsenal_product_read(provider->state.q3,&product,error)) return false;
        int maximum=product==QA_Q3_ARENA?QA_Q3_W_GRAPPLE:QA_Q3_WEAPON_COUNT-1;
        static const int weapon_indices[QA_Q3_WEAPON_COUNT]={0,4,6,5,7,8,9,10,11,13,14,15,16,17};
        static const int ammo_indices[QA_Q3_WEAPON_COUNT]={0,0,19,18,20,23,22,24,21,25,0,26,27,28};
        static const int weapons[]={QA_Q3_W_GAUNTLET,QA_Q3_W_SHOTGUN,QA_Q3_W_MACHINEGUN,
            QA_Q3_W_GRENADE,QA_Q3_W_ROCKET,QA_Q3_W_LIGHTNING,QA_Q3_W_RAIL,QA_Q3_W_PLASMA,
            QA_Q3_W_BFG,QA_Q3_W_GRAPPLE,QA_Q3_W_NAIL,QA_Q3_W_PROX,QA_Q3_W_CHAINGUN};
        for(size_t index=0;index<sizeof(weapons)/sizeof(*weapons);++index) {
            int source=weapons[index];
            if(source>maximum) continue;
            qa_item_id item=qa_q3_weapon_item(provider->state.q3,(qa_q3_weapon)source,false);
            if(!qa_bot_inventory_write(inventory,weapon_indices[source],count(bots,actor,item)>0,error)) return false;
        }
        static const int ammunition[]={QA_Q3_W_SHOTGUN,QA_Q3_W_MACHINEGUN,QA_Q3_W_GRENADE,
            QA_Q3_W_PLASMA,QA_Q3_W_LIGHTNING,QA_Q3_W_ROCKET,QA_Q3_W_RAIL,QA_Q3_W_BFG,
            QA_Q3_W_NAIL,QA_Q3_W_PROX,QA_Q3_W_CHAINGUN};
        for(size_t index=0;index<sizeof(ammunition)/sizeof(*ammunition);++index) {
            int source=ammunition[index];
            if(source>maximum) continue;
            qa_item_id ammo=qa_q3_weapon_item(provider->state.q3,(qa_q3_weapon)source,true);
            if(!qa_bot_inventory_write(inventory,ammo_indices[source],integer(count(bots,actor,ammo)),error)) return false;
        }
    }
    return true;
}
bool application_bot_weapon_slot(application_provider *provider,int32_t source,int32_t *slot,qa_error *error) {
    *slot=0;
    if(provider->kind==APPLICATION_PROVIDER_Q1) {
        if(source<0 || source>=QA_Q1_WEAPON_COUNT) return true;
        qa_item_id item,ammo;bool covered;
        if(!qa_q1_bot_weapon_items(provider->state.q1,(qa_q1_weapon)source,&item,&ammo,&covered,error)) return false;
        if(covered) *slot=source+1;
    } else if(provider->kind==APPLICATION_PROVIDER_Q2) {
        uint32_t registered_count;
        if(!qa_q2_bot_arsenal_definition_count(provider->state.q2,&registered_count,error)) return false;
        for(uint32_t ordinal=0;ordinal<registered_count;++ordinal) {
            const qa_q2_weapon_definition *definition;
            if(!qa_q2_bot_arsenal_definition_read(provider->state.q2,ordinal,&definition,error)) return false;
            if((int32_t)definition->weapon==source && q2_base[definition->weapon].pellets) {
                *slot=(int32_t)ordinal+1;break;
            }
        }
    } else if(provider->kind==APPLICATION_PROVIDER_Q3) *slot=source;
    return true;
}
bool application_bot_weapon_resolve(application_bots *bots,qa_actor_id actor,int32_t slot,
                                     qa_item_id *out,qa_error *error) {
    *out=0;
    application_provider *provider=application_provider_for(bots->application,actor,QA_ROLE_ARSENAL,NULL);
    if(!provider || slot<=0) return true;
    if(provider->kind==APPLICATION_PROVIDER_Q1) {
        if(slot>QA_Q1_WEAPON_COUNT) return true;
        qa_item_id item,ammo;bool covered,usable;
        qa_q1_weapon weapon=(qa_q1_weapon)(slot-1);
        if(!qa_q1_bot_weapon_items(provider->state.q1,weapon,&item,&ammo,&covered,error)) return false;
        if(!covered) return true;
        if(!qa_q1_bot_weapon_usable(provider->state.q1,actor,weapon,&usable,error)) return false;
        if(usable) *out=item;
    } else if(provider->kind==APPLICATION_PROVIDER_Q2) {
        uint32_t registered_count;
        if(!qa_q2_bot_arsenal_definition_count(provider->state.q2,&registered_count,error)) return false;
        if((uint32_t)slot>registered_count) return true;
        const qa_q2_weapon_definition *definition;
        if(!qa_q2_bot_arsenal_definition_read(provider->state.q2,(uint32_t)slot-1,&definition,error)) return false;
        if(!q2_base[definition->weapon].pellets) return true;
        const qa_q2_item_definition *item=qa_q2_item_lookup(provider->state.q2,definition->item);
        if(!item) return application_fail(error,QA_ERROR_FORMAT,"Q2 weapon resolution lost its registered item");
        if(count(bots,actor,item->item)>0 && (!item->ammo || count(bots,actor,item->ammo)>=definition->quantity)) *out=item->item;
    } else if(provider->kind==APPLICATION_PROVIDER_Q3 && slot<QA_Q3_WEAPON_COUNT) {
        qa_q3_product product;
        if(!qa_q3_bot_arsenal_product_read(provider->state.q3,&product,error)) return false;
        if(product==QA_Q3_ARENA && slot>QA_Q3_W_GRAPPLE) return true;
        qa_q3_player_state player;
        if(!qa_q3_player_read(provider->state.q3,actor,&player)) return true;
        qa_item_id item=qa_q3_weapon_item(provider->state.q3,(qa_q3_weapon)slot,false);
        qa_item_id ammo=qa_q3_weapon_item(provider->state.q3,(qa_q3_weapon)slot,true);
        if(count(bots,actor,item)>0 && (!ammo || count(bots,actor,ammo)!=0)) *out=item;
    }
    return true;
}
bool application_bot_travel_weapon(void *opaque,int32_t client,qa_nav_travel mode,
                                    int32_t *weapon,bool *found,qa_error *error) {
    application_bots *bots=opaque;*found=false;
    qa_actor_id actor=application_bot_client_actor(bots,client);
    if(!actor.registry) return true;
    if(mode<0 || mode>=QA_NAV_TRAVEL_COUNT)
        return application_fail(error,QA_ERROR_ARGUMENT,"invalid bot weapon travel mode");
    const qa_bot_weapon_knowledge *knowledge;size_t length;void *lease;
    if(!application_bot_arsenal(bots,actor,&knowledge,&length,&lease,error)) return false;
    for(size_t i=0;i<length;++i) {
        const qa_bot_weapon_knowledge *candidate=&knowledge[i];
        if(!candidate->weapon.valid || !candidate->owned ||
           !(candidate->travel_modes&QA_NAV_CAPABILITY(mode))) continue;
        if(candidate->ammo_per_shot>0 && count(bots,actor,candidate->supply_ammo)<candidate->ammo_per_shot) continue;
        *weapon=candidate->weapon.number;*found=true;break;
    }
    application_bot_arsenal_end(bots,lease);
    if(!qa_actors_get(qa_session_actors(bots->application->session),actor)) *found=false;
    return true;
}
bool application_bot_grapple_state(void *opaque,int32_t client,qa_bot_grapple_observation *out,qa_error *error) {
    application_bots *bots=opaque;qa_application *application=bots->application;*out=QA_BOT_GRAPPLE_NONE;
    qa_actor_id actor=application_bot_client_actor(bots,client);
    if(!actor.registry) return true;
    application_provider *provider=application_provider_for(application,actor,QA_ROLE_ARSENAL,NULL);
    if(!provider) return true;
    if(provider->kind==APPLICATION_PROVIDER_Q3) {
        qa_q3_grapple_state state;
        if(qa_q3_grapple_read(provider->state.q3,actor,&state) &&
           qa_actors_get(qa_session_actors(application->session),state.hook))
            *out=state.active?QA_BOT_GRAPPLE_PULLING:QA_BOT_GRAPPLE_FLYING;
    } else if(provider->kind==APPLICATION_PROVIDER_Q2) {
        for(int i=QA_Q2_CTF_GRAPPLE;i<=QA_Q2_LMCTF_GRAPPLE;++i) {
            qa_q2_grapple_state state;
            if(!qa_q2_grapple_read(provider->state.q2,actor,(qa_q2_grapple_kind)i,&state,error)) return false;
            if(!qa_actors_get(qa_session_actors(application->session),state.hook)) continue;
            qa_bot_grapple_observation observation=state.phase==QA_Q2_GRAPPLE_FLY?
                QA_BOT_GRAPPLE_FLYING:QA_BOT_GRAPPLE_PULLING;
            if(observation>*out) *out=observation;
        }
    } else if(provider->kind==APPLICATION_PROVIDER_Q1 && qa_q1_grapple_pulling(provider->state.q1,actor))
        *out=QA_BOT_GRAPPLE_PULLING;
    if(!qa_actors_get(qa_session_actors(application->session),actor)) *out=QA_BOT_GRAPPLE_NONE;
    return true;
}
