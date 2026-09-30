#include "bots_private.h"
#include <limits.h>
#include <math.h>
#include <string.h>

static double count(application_bots *bots,qa_actor_id actor,qa_item_id item) {
    qa_inventory_entry entry;
    return item && qa_inventory_entry_read(bots->application->inventory,actor,item,&entry,NULL)?entry.count:0;
}
static int32_t integer(double value) {
    return !isfinite(value) || value<=0?0:value>=INT32_MAX?INT32_MAX:(int32_t)value;
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
    [QA_Q2_GRENADELAUNCHER]={120,600,1.2f,1500,160,1,1},
    [QA_Q2_ROCKETLAUNCHER]={109.5f,650,.9f,8000,120,1,1},
    [QA_Q2_HYPERBLASTER]={20,1000,.1f,2000,0,1,1},
    [QA_Q2_RAILGUN]={150,0,1.6f,8192,0,1,1},
    [QA_Q2_BFG]={500,400,2.5f,8000,1000,1,50},
    [QA_Q2_IONRIPPER]={50,500,.3f,1500,0,1,2,143},
    [QA_Q2_PHALANX]={74.5f,725,1.6f,8000,120,2,1,214},
    [QA_Q2_ETF_RIFLE]={10,750,.1f,8000,0,1,1},
    [QA_Q2_HEATBEAM]={15,0,.1f,8192,0,1,2},
    [QA_Q2_DISINTEGRATOR]={45,1000,.6f,10000,0,1,1},
    [QA_Q2_CHAINFIST]={15,0,.1f,64,0,1,0,0,0,true}
};
static void describe(qa_bot_weapon_knowledge *out,int source,int slot,qa_item_id weapon,
                     qa_item_id ammo,ballistics fact,bool owned) {
    *out=(qa_bot_weapon_knowledge){.weapon={.valid=true,.number=source,
        .weapon_inventory=64+slot,.ammo_inventory=96+slot,.ammo_amount=fact.ammo,
        .projectile_count=fact.pellets,.reload=fact.cycle,.speed=fact.speed,
        .horizontal_spread=fact.horizontal,.vertical_spread=fact.vertical},
        .projectile={.damage=(int32_t)fact.damage,.radius=fact.radius,.damage_type=1|(fact.radius?2:0)},
        .maximum_range=fact.range,.ranged_limit=true,.melee=fact.melee,.personality_role=-1,
        .has_supply=true,.supply_weapon=weapon,.supply_ammo=ammo,.ammo_per_shot=fact.ammo,.owned=owned};
}
static bool observe_arsenal(application_bots *bots,qa_actor_id actor,qa_error *error) {
    qa_application *application=bots->application;
    application_provider *provider=application_provider_for(application,actor,QA_ROLE_ARSENAL,NULL);
    bots->knowledge_count=0;
    if(provider && provider->kind==APPLICATION_PROVIDER_Q3) {
        for(int source=1;source<QA_Q3_WEAPON_COUNT;++source) {
            qa_bot_weapon_knowledge value={.personality_role=source,.has_supply=true};bool found;
            if(!qa_bot_runtime_weapon_info(bots->runtime,bots->metadata_weapon,(uint32_t)source,
                    &value.weapon,&value.projectile,&found,error)) return false;
            if(!found) continue;
            value.supply_weapon=qa_q3_weapon_item(provider->state.q3,(qa_q3_weapon)source,false);
            value.supply_ammo=qa_q3_weapon_item(provider->state.q3,(qa_q3_weapon)source,true);
            value.ammo_per_shot=value.weapon.ammo_amount;
            value.owned=count(bots,actor,value.supply_weapon)>0;
            value.melee=source==QA_Q3_W_GAUNTLET;
            value.ranged_limit=value.melee || source==QA_Q3_W_LIGHTNING;
            value.maximum_range=value.melee?64:source==QA_Q3_W_LIGHTNING?768:8192;
            value.travel_modes=source==QA_Q3_W_ROCKET?QA_NAV_CAPABILITY(QA_NAV_ROCKET_JUMP):
                source==QA_Q3_W_BFG?QA_NAV_CAPABILITY(QA_NAV_BFG_JUMP):
                source==QA_Q3_W_GRAPPLE?QA_NAV_CAPABILITY(QA_NAV_GRAPPLE):0;
            bots->knowledge[bots->knowledge_count++]=value;
        }
    } else if(provider && provider->kind==APPLICATION_PROVIDER_Q1) {
        for(int source=0;source<QA_Q1_WEAPON_COUNT;++source) {
            qa_q1_weapon_view view;bool found;
            if(!qa_q1_player_weapon_read(provider->state.q1,actor,(qa_q1_weapon)source,&view,&found,error)) return false;
            if(!qa_actors_get(qa_session_actors(application->session),actor) ||
               application_provider_for(application,actor,QA_ROLE_ARSENAL,NULL)!=provider) {
                bots->knowledge_count=0;return true;
            }
            if(!found) continue;
            if(bots->knowledge_count>=sizeof(bots->knowledge)/sizeof(*bots->knowledge))
                return application_fail(error,QA_ERROR_MEMORY,"native bot weapon observation capacity exceeded");
            qa_bot_weapon_knowledge *value=&bots->knowledge[bots->knowledge_count++];
            *value=(qa_bot_weapon_knowledge){.weapon={.valid=view.available,.number=source+1,
                .weapon_inventory=65+source,.ammo_inventory=97+source,
                .ammo_amount=integer(view.ammo_per_shot),.projectile_count=(int32_t)view.shots,
                .reload=view.fire_interval,.speed=view.speed,.extra_z_velocity=view.extra_z_velocity,
                .horizontal_spread=atanf(view.horizontal_spread)*(180.0f/3.14159265358979323846f)/6,
                .vertical_spread=atanf(view.vertical_spread)*(180.0f/3.14159265358979323846f)/6},
                .projectile={.damage=integer(view.damage),.radius=view.blast_radius,
                    .damage_type=1|(view.blast_radius>0?2:0),.gravity=view.gravity,.detonation=view.lifetime},
                .maximum_range=view.range,.ranged_limit=true,.melee=view.melee,
                .personality_role=view.grapple?10:-1,.has_supply=true,.owned=view.owned,
                .supply_weapon=view.item,.supply_ammo=view.ammo,.ammo_per_shot=view.ammo_per_shot,
                .muzzle_count=view.muzzle_count,.launch_delay=view.launch_delay,
                .gravity_acceleration=view.gravity_acceleration};
            memcpy(value->muzzle_offsets,view.muzzle_offsets,sizeof(value->muzzle_offsets));
            if(source==QA_Q1_ROCKET || source==QA_Q1_MULTI_ROCKET)
                value->travel_modes=QA_NAV_CAPABILITY(QA_NAV_ROCKET_JUMP);
            if(view.grapple) value->travel_modes=QA_NAV_CAPABILITY(QA_NAV_GRAPPLE);
        }
    } else if(provider && provider->kind==APPLICATION_PROVIDER_Q2) {
        const qa_product *product=qa_catalog_product(application->catalog,provider->launch->selection.product);
        bool rerelease=product && product->edition==QA_EDITION_RERELEASE,deathmatch=false;
        if(application->modes && application->primary_mode_ready) {
            qa_mode_view mode;if(!qa_modes_read(application->modes,application->primary_mode,&mode,error)) return false;
            deathmatch=mode.rules.kind!=QA_MODE_COOPERATIVE && mode.rules.kind!=QA_MODE_SINGLE_PLAYER && mode.rules.kind!=QA_MODE_HORDE;
        }
        for(int source=1;source<QA_Q2_WEAPON_COUNT;++source) {
            const qa_q2_weapon_definition *definition=qa_q2_weapon_definition_at(provider->state.q2,(qa_q2_weapon)source);
            ballistics fact=q2_base[source];
            if(!definition || !fact.pellets) continue;
            if(source==QA_Q2_BLASTER) {fact.damage=rerelease || deathmatch?15:10;fact.speed=rerelease?1500:1000;fact.range=rerelease?3000:2000;}
            if(source==QA_Q2_CHAINGUN) fact.damage=deathmatch?6:8;
            if(source==QA_Q2_HYPERBLASTER) fact.damage=deathmatch?15:20;
            if(source==QA_Q2_RAILGUN) fact.damage=deathmatch?100:rerelease?125:150;
            if(source==QA_Q2_BFG) fact.damage=deathmatch?200:500;
            if(source==QA_Q2_DISINTEGRATOR) fact.damage=rerelease?(deathmatch?45:135):(deathmatch?30:45);
            if(source==QA_Q2_CHAINFIST) fact.damage=rerelease?(deathmatch?15:7):(deathmatch?30:15);
            if(source==QA_Q2_IONRIPPER) fact.damage=deathmatch?30:50;
            if(source==QA_Q2_ETF_RIFLE) fact.speed=rerelease?1150:750;
            const qa_q2_item_definition *item=qa_q2_item_lookup(provider->state.q2,definition->item);
            if(!item) continue;
            qa_bot_weapon_knowledge *value=&bots->knowledge[bots->knowledge_count++];
            describe(value,source,source,item->item,item->ammo,fact,count(bots,actor,item->item)>0);
            if(source==QA_Q2_ROCKETLAUNCHER) value->travel_modes=QA_NAV_CAPABILITY(QA_NAV_ROCKET_JUMP);
            if(source==QA_Q2_BFG) value->personality_role=9;
            value->weapon.activate=(float)definition->activate_last*.1f;
            if(source==QA_Q2_CHAINGUN) value->weapon.spin_up=1;
            value->weapon.ammo_amount=definition->quantity;value->ammo_per_shot=definition->quantity;
            value->weapon.horizontal_spread=atanf(fact.horizontal/8192)*(180.0f/3.14159265358979323846f)/6;
            value->weapon.vertical_spread=atanf(fact.vertical/8192)*(180.0f/3.14159265358979323846f)/6;
            value->weapon.offset=source==QA_Q2_BLASTER || source==QA_Q2_HYPERBLASTER || source==QA_Q2_DISINTEGRATOR?qa_v3(24,8,-8):
                source==QA_Q2_IONRIPPER?qa_v3(16,7,-8):source==QA_Q2_ETF_RIFLE?qa_v3(15,8,-8):
                source==QA_Q2_HEATBEAM?qa_v3(7,2,-3):source==QA_Q2_RAILGUN?qa_v3(0,7,-8):
                source==QA_Q2_CHAINFIST?qa_v3(0,rerelease?0:8,-4):
                source==QA_Q2_ROCKETLAUNCHER || source==QA_Q2_BFG?qa_v3(8,8,-8):
                source==QA_Q2_GRENADELAUNCHER?qa_v3(8,rerelease?0:8,-8):qa_v3(0,source==QA_Q2_PHALANX || !rerelease?8:0,-8);
            if(source==QA_Q2_GRENADELAUNCHER) {value->projectile.gravity=1;value->projectile.detonation=2.5f;value->projectile.bounce=1.5f;value->weapon.extra_z_velocity=200;}
        }
    } else return application_fail(error,QA_ERROR_UNSUPPORTED,"selected bot arsenal has no native observation adapter");
    return true;
}
bool application_bot_arsenal(void *opaque,qa_actor_id actor,const qa_bot_weapon_knowledge **out,
                            size_t *length,void **lease,qa_error *error) {
    application_bots *bots=opaque;
    if(bots->arsenal_leases) return application_fail(error,QA_ERROR_ARGUMENT,"bot arsenal observation is already borrowed");
    ++bots->arsenal_leases;
    if(!observe_arsenal(bots,actor,error)) {--bots->arsenal_leases;return false;}
    *out=bots->knowledge;*length=bots->knowledge_count;*lease=bots;return true;
}
void application_bot_arsenal_end(void *opaque,void *lease) {
    application_bots *bots=opaque;if(lease==bots && bots->arsenal_leases) --bots->arsenal_leases;
}
bool application_bot_inventory(application_bots *bots,qa_actor_id actor,int32_t inventory[QA_BOT_INVENTORY_SIZE],qa_error *error) {
    const qa_bot_weapon_knowledge *knowledge;size_t length;void *lease;
    if(!application_bot_arsenal(bots,actor,&knowledge,&length,&lease,error)) return false;
    for(size_t i=0;i<length;++i) {
        const qa_bot_weapon_knowledge *weapon=&knowledge[i];
        if(weapon->weapon.weapon_inventory>=0 && weapon->weapon.weapon_inventory<QA_BOT_INVENTORY_SIZE)
            inventory[weapon->weapon.weapon_inventory]=weapon->owned;
        if(weapon->weapon.ammo_inventory>=0 && weapon->weapon.ammo_inventory<QA_BOT_INVENTORY_SIZE)
            inventory[weapon->weapon.ammo_inventory]=integer(count(bots,actor,weapon->supply_ammo));
    }
    application_bot_arsenal_end(bots,lease);return true;
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
