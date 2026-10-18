#include "bots_private.h"
#include "bots_guests.h"
#include "qa/game_q2_bots.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q3_bots.h"
#include "guest_q3_private.h"
#include "guest_q3_restart.h"
#include "guest_q3_catalog.h"
#include "guest_q3_weapons.h"
#include "qa/text.h"
#include <limits.h>
#include <math.h>
#include <string.h>

static double count(application_bots *bots,qa_actor_id actor,qa_item_id item) {
    qa_inventory_entry entry;
    return item && qa_inventory_entry_read(bots->application->inventory,actor,item,&entry,NULL)?entry.count:0;
}
static q3g_role *q3_guest_arsenal(const application_provider *provider) {
    struct application_q3_guest *engine=q3g_engine(provider);
    return engine && engine->game && !engine->game->retired && engine->game->catalog?engine->game:NULL;
}
bool application_bot_arsenal_supported(const application_provider *provider) {
    if(!provider) return false;
    if(provider->kind==APPLICATION_PROVIDER_Q1 || provider->kind==APPLICATION_PROVIDER_Q2 ||
       provider->kind==APPLICATION_PROVIDER_Q3) return true;
    q3g_role *role=q3_guest_arsenal(provider);
    return role && application_bots_guest_runtime(provider->application,role->engine->provider) &&
        (application_world_provider(provider->application,QA_ROLE_ENTITIES,"")==provider ||
         role->input_profile.bot_setup.entry);
}
bool application_bot_q3_arsenal_read(application_provider *provider,
    application_bot_q3_arsenal *out,bool *found,qa_error *error) {
    *out=(application_bot_q3_arsenal){0};*found=false;
    if(provider && provider->kind==APPLICATION_PROVIDER_Q3) {
        if(!qa_q3_bot_arsenal_product_read(provider->state.q3,&out->product,error)) return false;
        out->stock_rules=true;
        int maximum=out->product==QA_Q3_ARENA?QA_Q3_W_GRAPPLE:QA_Q3_WEAPON_COUNT-1;
        for(int source=1;source<=maximum;++source) {
            out->weapons[source]=qa_q3_weapon_item(provider->state.q3,(qa_q3_weapon)source,false);
            out->ammo[source]=qa_q3_weapon_item(provider->state.q3,(qa_q3_weapon)source,true);
        }
    } else {
        q3g_role *role=q3_guest_arsenal(provider);
        if(!role || !application_bot_arsenal_supported(provider)) return true;
        out->runtime=application_bots_guest_runtime(provider->application,provider);
        if(!application_q3_catalog_weapons(role->catalog,&out->rows,&out->count,error)) return false;
        out->guest=true;
        out->stock_rules=application_q3_catalog_bot_product(role->catalog,&out->product);
    }
    *found=true;return true;
}
bool application_bot_q3_weapon_state(application_provider *provider,qa_actor_id actor,
    int32_t *weapon,int32_t *phase,int32_t *time,bool *found,qa_error *error) {
    *weapon=*phase=*time=0;*found=false;
    if(provider && provider->kind==APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state player;
        if(!qa_q3_player_read(provider->state.q3,actor,&player)) return true;
        *weapon=(int32_t)player.weapon;*phase=(int32_t)player.weapon_phase;*time=player.weapon_time_ms;
    } else if(provider && provider->kind==APPLICATION_PROVIDER_NATIVE) {
        struct application_q3_guest *engine=q3g_engine(provider);
        if(!engine || !engine->game || engine->game->retired) return true;
        qa_q3_player player;
        if(!application_q3_guest_player_state_read(provider,actor,&player,found,error)) return false;
        if(!*found) return true;
        *weapon=player.weapon;*phase=player.weaponState;*time=player.weaponTime;
    } else {
        struct application_q3_guest *engine=q3g_engine(provider);
        q3g_role *role=engine?engine->game:NULL;
        if(!role || role->retired || !role->weapons) return true;
        application_q3_weapon_prediction player;
        if(!application_q3_weapons_prediction_read(role->weapons,actor,&player,error)) return false;
        *weapon=player.source_weapon;*phase=player.state;*time=player.time_ms;
    }
    *found=true;return true;
}
static float q2_spread(float endpoint,float range,float degrees) {
    float angular=range>0?atanf(endpoint/range)*(180.0f/3.14159265358979323846f):0;
    return (angular+degrees)/6;
}
static float input_pitch(float angle) {
    float pitch=qa_angle_delta(angle,0);
    return pitch==180?-180:pitch;
}
static qa_vec3 q1_launch_velocity(const qa_bot_weapon_knowledge *knowledge,qa_vec3 angles) {
    angles.x=input_pitch(angles.x);
    qa_q1_weapon_view view={.speed=knowledge->weapon.speed,
        .extra_z_velocity=knowledge->weapon.extra_z_velocity,.thrown=knowledge->thrown};
    return qa_q1_weapon_launch_velocity(&view,angles);
}
static qa_vec3 q2_launch_velocity(const qa_bot_weapon_knowledge *knowledge,qa_vec3 angles) {
    angles.x=input_pitch(angles.x);
    qa_q2_bot_weapon_fact fact={.speed=knowledge->weapon.speed,
        .extra_z_velocity=knowledge->weapon.extra_z_velocity,
        .ballistic=knowledge->thrown,.deployable=knowledge->deployable,
        .pitch_clamped=knowledge->pitch_clamped,.launch_yaw_offset=knowledge->launch_yaw_offset};
    return qa_q2_bot_weapon_launch_velocity(&fact,angles);
}
static void q3_stock_weapon_rules(qa_bot_weapon_knowledge *value,int32_t source) {
    value->personality_role=source;
    value->melee=source==QA_Q3_W_GAUNTLET;
    value->ranged_limit=value->melee;
    value->maximum_range=value->melee?60:0;
    value->travel_modes=source==QA_Q3_W_ROCKET?QA_NAV_CAPABILITY(QA_NAV_ROCKET_JUMP):
        source==QA_Q3_W_BFG?QA_NAV_CAPABILITY(QA_NAV_BFG_JUMP):
        source==QA_Q3_W_GRAPPLE?QA_NAV_CAPABILITY(QA_NAV_GRAPPLE):0;
}
static bool observe_arsenal(application_bots *bots,qa_actor_id actor,qa_error *error) {
    application_provider *provider=application_provider_for(bots->application,actor,QA_ROLE_ARSENAL,NULL);
    bots->knowledge_count=0;
    application_bot_q3_arsenal q3;bool has_q3;
    if(!application_bot_q3_arsenal_read(provider,&q3,&has_q3,error)) return false;
    if(has_q3 && q3.guest) {
        for(size_t i=0;i<q3.count;++i) {
            const application_q3_catalog_weapon *row=q3.rows+i;
            qa_bot_weapon_knowledge value={.personality_role=-1,.has_supply=true};bool found;
            if(!qa_bot_runtime_weapon_config_info(q3.runtime,(uint32_t)row->weapon,
                &value.weapon,&value.projectile,&found,error)) return false;
            if(!found || !value.weapon.valid) continue;
            if(bots->knowledge_count>=sizeof(bots->knowledge)/sizeof(*bots->knowledge))
                return application_fail(error,QA_ERROR_MEMORY,"native bot weapon observation capacity exceeded");
            value.weapon.number=row->weapon;
            value.selected_projectile_damage=value.projectile.damage;
            value.supply_weapon=row->item;value.supply_ammo=row->ammo;
            value.ammo_per_shot=value.weapon.ammo_amount;
            value.owned=count(bots,actor,row->item)>0;
            value.available=value.owned && (value.weapon.ammo_amount<=0 ||
                (row->ammo && count(bots,actor,row->ammo)>=value.weapon.ammo_amount));
            if(q3.stock_rules) q3_stock_weapon_rules(&value,row->weapon);
            bots->knowledge[bots->knowledge_count++]=value;
        }
    } else if(has_q3) {
        uint32_t handle;
        if(!qa_bots_source_weapon_handle(bots->population,actor,&handle,error)) return false;
        int maximum=q3.product==QA_Q3_ARENA?QA_Q3_W_GRAPPLE:QA_Q3_WEAPON_COUNT-1;
        for(int source=1;source<=maximum;++source) {
            if(!q3.weapons[source]) continue;
            qa_bot_weapon_knowledge value={.has_supply=true};bool found;
            if(!qa_bot_runtime_weapon_info(bots->runtime,handle,(uint32_t)source,
                    &value.weapon,&value.projectile,&found,error)) return false;
            if(!found || !value.weapon.valid) continue;
            value.selected_projectile_damage=value.projectile.damage;
            value.supply_weapon=q3.weapons[source];value.supply_ammo=q3.ammo[source];
            value.ammo_per_shot=value.supply_ammo?1:0;
            value.owned=count(bots,actor,value.supply_weapon)>0;
            value.available=value.owned && (!value.supply_ammo || count(bots,actor,value.supply_ammo)!=0);
            q3_stock_weapon_rules(&value,source);
            bots->knowledge[bots->knowledge_count++]=value;
        }
    } else if(provider && provider->kind==APPLICATION_PROVIDER_Q1) {
        for(int source=0;source<QA_Q1_WEAPON_COUNT;++source) {
            qa_q1_weapon_view view;bool found;
            if(!qa_q1_bot_weapon_read(provider->state.q1,actor,(qa_q1_weapon)source,&view,&found,error)) return false;
            if(!found) continue;
            if(bots->knowledge_count>=sizeof(bots->knowledge)/sizeof(*bots->knowledge))
                return application_fail(error,QA_ERROR_MEMORY,"native bot weapon observation capacity exceeded");
            int32_t ammo_amount;
            if (!application_bot_integer(view.ammo_per_shot,&ammo_amount,error)) return false;
            qa_bot_weapon_knowledge *value=&bots->knowledge[bots->knowledge_count++];
            *value=(qa_bot_weapon_knowledge){.weapon={.valid=true,.number=source+1,
                .weapon_inventory=65+source,.ammo_inventory=97+source,
                .ammo_amount=ammo_amount,.projectile_count=(int32_t)view.shots,
                .reload=view.fire_interval,.speed=view.speed,.extra_z_velocity=view.extra_z_velocity,
                .horizontal_spread=(float)(atan(view.horizontal_spread)*(180.0/3.14159265358979323846)/6),
                .vertical_spread=(float)(atan(view.vertical_spread)*(180.0/3.14159265358979323846)/6)},
                .projectile={.damage=qa_source_to_i32(view.damage),.radius=view.blast_radius,
                    .gravity=view.gravity,.detonation=view.lifetime,
                    .damage_type=1|(view.blast_radius>0?2:0)},
                .selected_projectile_damage=view.damage,.selected_splash_damage=view.blast_damage,
                .maximum_range=view.range,.ranged_limit=true,.melee=view.melee,
                .personality_role=-1,.has_supply=true,.owned=view.owned,.available=view.available,
                .supply_weapon=view.item,.supply_ammo=view.ammo,.ammo_per_shot=view.ammo_per_shot,
                .muzzle_count=view.muzzle_count,.launch_delay=view.launch_delay,
                .gravity_acceleration=view.gravity_acceleration,
                .discharge=view.discharge,.grapple=view.grapple,.deployable=view.deployable,
                .homing=view.homes_monsters,.conditional_strike=view.conditional_strike,
                .conditional_trajectory=view.conditional_trajectory,.thrown=view.thrown,
                .has_cycle=view.fire_interval>0,.timed_detonation=view.timed_detonation,
                .launch_velocity=q1_launch_velocity};
            memcpy(value->muzzle_offsets,view.muzzle_offsets,sizeof(value->muzzle_offsets));
            if(source==QA_Q1_ROCKET)
                value->travel_modes=QA_NAV_CAPABILITY(QA_NAV_ROCKET_JUMP);
            if(view.grapple) value->travel_modes=QA_NAV_CAPABILITY(QA_NAV_GRAPPLE);
        }
    } else if(provider && provider->kind==APPLICATION_PROVIDER_Q2) {
        uint32_t registered_count;
        if(!qa_q2_bot_arsenal_definition_count(provider->state.q2,&registered_count,error)) return false;
        for(uint32_t ordinal=0;ordinal<registered_count;++ordinal) {
            const qa_q2_weapon_definition *definition;
            if(!qa_q2_bot_arsenal_definition_read(provider->state.q2,ordinal,&definition,error)) return false;
            qa_q2_bot_weapon_fact fact;bool found;
            if(!qa_q2_bot_weapon_read(provider->state.q2,actor,definition->weapon,&fact,&found,error)) return false;
            if(!found) continue;
            if(bots->knowledge_count>=sizeof(bots->knowledge)/sizeof(*bots->knowledge))
                return application_fail(error,QA_ERROR_MEMORY,"native bot weapon observation capacity exceeded");
            int32_t ammo_amount,damage;
            if (!application_bot_integer(fact.required_ammo,&ammo_amount,error) ||
                !application_bot_integer(fact.damage,&damage,error)) return false;
            qa_bot_weapon_knowledge *value=&bots->knowledge[bots->knowledge_count++];
            *value=(qa_bot_weapon_knowledge){.weapon={.valid=true,.number=(int32_t)ordinal+1,
                .weapon_inventory=(int32_t)ordinal+65,.ammo_inventory=(int32_t)ordinal+97,
                .ammo_amount=ammo_amount,.projectile_count=(int32_t)fact.shots,
                .reload=fact.cycle,.activate=fact.activate,.spin_up=fact.spin_up,
                .speed=fact.speed,.extra_z_velocity=fact.extra_z_velocity,.offset=fact.offset,
                .horizontal_spread=q2_spread(fact.spread_x,fact.range,fact.spread_degrees_x),
                .vertical_spread=q2_spread(fact.spread_y,fact.range,fact.spread_degrees_y)},
                .projectile={.damage=damage,.radius=fact.radius,
                    .gravity=fact.ballistic?1:0,.detonation=fact.fuse,
                    .damage_type=1|(fact.radius>0?2:0)},
                .selected_projectile_damage=fact.damage,.selected_splash_damage=fact.splash_damage,
                .effect_damage=fact.effect_damage,.effect_radius=fact.effect_radius,
                .maximum_range=fact.range,.ranged_limit=fact.range>0,.melee=fact.melee,
                .personality_role=-1,.has_supply=true,.owned=fact.owned,.available=fact.available,
                .supply_weapon=fact.item,.supply_ammo=fact.ammo,.ammo_per_shot=fact.ammo_per_shot,
                .muzzle_count=fact.muzzle_count,.launch_delay=fact.launch_delay,
                .gravity_acceleration=fact.gravity_acceleration,.launch_yaw_offset=fact.launch_yaw_offset,
                .grapple=fact.grapple,.deployable=fact.deployable,.homing=fact.homing,
                .conditional_strike=fact.conditional,.thrown=fact.ballistic,
                .has_cycle=fact.has_cycle,.timed_detonation=fact.timed_detonation,
                .requires_release=fact.requires_release,.ammo_reserved=fact.ammo_reserved,
                .pitch_clamped=fact.pitch_clamped,
                .range_from_bounds=fact.range_from_bounds,.launch_velocity=q2_launch_velocity};
            memcpy(value->muzzle_offsets,fact.muzzle_offsets,sizeof(value->muzzle_offsets));
            if(definition->weapon==QA_Q2_ROCKETLAUNCHER)
                value->travel_modes=QA_NAV_CAPABILITY(QA_NAV_ROCKET_JUMP);
            if(fact.grapple) value->travel_modes=QA_NAV_CAPABILITY(QA_NAV_GRAPPLE);
        }
    }
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
    application_bot_q3_arsenal q3;bool has_q3;
    if(!application_bot_q3_arsenal_read(provider,&q3,&has_q3,error)) return false;
    if(has_q3) {
        int32_t weapon,phase,time;bool present;
        if(!application_bot_q3_weapon_state(provider,actor,&weapon,&phase,&time,&present,error)) return false;
        if(!present) actor=(qa_actor_id){0};
    }
    for(int32_t index=0;index<200;++index)
        if(!qa_bot_inventory_write(inventory,index,0,error)) return false;
    qa_combat_state combat;
    bool present=qa_combat_read(bots->application->combat,actor,&combat,NULL);
    int32_t health=0,armor=0;
    if (present && (!application_bot_integer(combat.health,&health,error) ||
        !application_bot_integer(combat.armor.regular.kind!=QA_ARMOR_NONE?combat.armor.regular.points:0,&armor,error))) return false;
    if(!qa_bot_inventory_write(inventory,QA_BOT_INV_HEALTH,health,error) ||
       !qa_bot_inventory_write(inventory,QA_BOT_INV_ARMOR,armor,error)) return false;
    if(provider && provider->kind==APPLICATION_PROVIDER_Q1) {
        double time,started;bool intermission;
        if(!qa_q1_bot_clock_read(provider->state.q1,&time,&intermission,&started,error)) return false;
        if(!qa_bot_inventory_write(inventory,QA_BOT_INV_QUAD,
            qa_q1_game_power_expires(provider->state.q1,actor,QA_Q1_QUAD)>time,error)) return false;
        for(int source=0;source<QA_Q1_WEAPON_COUNT;++source) {
            qa_item_id item,ammo;bool covered,usable;
            if(!qa_q1_bot_weapon_items(provider->state.q1,(qa_q1_weapon)source,&item,&ammo,&covered,error)) return false;
            if(!covered) continue;
            if(!qa_q1_bot_weapon_usable(provider->state.q1,actor,(qa_q1_weapon)source,&usable,error)) return false;
            int32_t ammunition;
            if (!application_bot_integer(count(bots,actor,ammo),&ammunition,error)) return false;
            if(!qa_bot_inventory_write(inventory,65+source,usable,error) ||
               !qa_bot_inventory_write(inventory,97+source,ammunition,error)) return false;
        }
    } else if(provider && provider->kind==APPLICATION_PROVIDER_Q2) {
        uint32_t registered_count;
        if(!qa_q2_bot_arsenal_definition_count(provider->state.q2,&registered_count,error)) return false;
        for(uint32_t ordinal=0;ordinal<registered_count;++ordinal) {
            const qa_q2_weapon_definition *definition;
            if(!qa_q2_bot_arsenal_definition_read(provider->state.q2,ordinal,&definition,error)) return false;
            qa_q2_bot_weapon_fact fact;bool found;
            if(!qa_q2_bot_weapon_read(provider->state.q2,actor,definition->weapon,&fact,&found,error)) return false;
            if(!found) continue;
            int32_t ammunition;
            if (!application_bot_integer(count(bots,actor,fact.ammo),&ammunition,error)) return false;
            if(!qa_bot_inventory_write(inventory,(int32_t)(65+ordinal),fact.owned,error) ||
               !qa_bot_inventory_write(inventory,(int32_t)(97+ordinal),ammunition,error)) return false;
        }
    } else if(has_q3 && q3.guest) {
        for(size_t i=0;i<q3.count;++i) {
            const application_q3_catalog_weapon *row=q3.rows+i;
            qa_bot_weapon_info weapon;qa_bot_projectile_info projectile;bool found;
            if(!qa_bot_runtime_weapon_config_info(q3.runtime,(uint32_t)row->weapon,
                &weapon,&projectile,&found,error)) return false;
            if(!found || !weapon.valid) continue;
            if(!qa_bot_inventory_write(inventory,weapon.weapon_inventory,count(bots,actor,row->item)>0,error)) return false;
            if(row->ammo || weapon.ammo_amount) {
                int32_t ammunition;
                if(!application_bot_integer(count(bots,actor,row->ammo),&ammunition,error) ||
                   !qa_bot_inventory_write(inventory,weapon.ammo_inventory,ammunition,error)) return false;
            }
        }
    } else if(has_q3) {
        int maximum=q3.product==QA_Q3_ARENA?QA_Q3_W_GRAPPLE:QA_Q3_WEAPON_COUNT-1;
        static const int weapon_indices[QA_Q3_WEAPON_COUNT]={0,4,6,5,7,8,9,10,11,13,14,15,16,17};
        static const int ammo_indices[QA_Q3_WEAPON_COUNT]={0,0,19,18,20,23,22,24,21,25,0,26,27,28};
        static const int weapons[]={QA_Q3_W_GAUNTLET,QA_Q3_W_SHOTGUN,QA_Q3_W_MACHINEGUN,
            QA_Q3_W_GRENADE,QA_Q3_W_ROCKET,QA_Q3_W_LIGHTNING,QA_Q3_W_RAIL,QA_Q3_W_PLASMA,
            QA_Q3_W_BFG,QA_Q3_W_GRAPPLE,QA_Q3_W_NAIL,QA_Q3_W_PROX,QA_Q3_W_CHAINGUN};
        for(size_t index=0;index<sizeof(weapons)/sizeof(*weapons);++index) {
            int source=weapons[index];
            if(source>maximum) continue;
            qa_item_id item=q3.weapons[source];
            if(!qa_bot_inventory_write(inventory,weapon_indices[source],count(bots,actor,item)>0,error)) return false;
        }
        static const int ammunition[]={QA_Q3_W_SHOTGUN,QA_Q3_W_MACHINEGUN,QA_Q3_W_GRENADE,
            QA_Q3_W_PLASMA,QA_Q3_W_LIGHTNING,QA_Q3_W_ROCKET,QA_Q3_W_RAIL,QA_Q3_W_BFG,
            QA_Q3_W_NAIL,QA_Q3_W_PROX,QA_Q3_W_CHAINGUN};
        for(size_t index=0;index<sizeof(ammunition)/sizeof(*ammunition);++index) {
            int source=ammunition[index];
            if(source>maximum) continue;
            qa_item_id ammo=q3.ammo[source];
            int32_t ammo_count;
            if (!application_bot_integer(count(bots,actor,ammo),&ammo_count,error) ||
                !qa_bot_inventory_write(inventory,ammo_indices[source],ammo_count,error)) return false;
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
            if((int32_t)definition->weapon==source) {
                *slot=(int32_t)ordinal+1;break;
            }
        }
    } else {
        application_bot_q3_arsenal q3;bool found;
        if(!application_bot_q3_arsenal_read(provider,&q3,&found,error)) return false;
        if(found && q3.guest) {
            for(size_t i=0;i<q3.count;++i) if(q3.rows[i].weapon==source) {
                qa_bot_weapon_info weapon;qa_bot_projectile_info projectile;bool present;
                if(!qa_bot_runtime_weapon_config_info(q3.runtime,(uint32_t)source,
                    &weapon,&projectile,&present,error)) return false;
                if(present && weapon.valid) *slot=source;
                break;
            }
        } else if(found && source>0 && source<QA_Q3_WEAPON_COUNT && q3.weapons[source]) *slot=source;
    }
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
        qa_q2_bot_weapon_fact fact;bool found;
        if(!qa_q2_bot_weapon_read(provider->state.q2,actor,definition->weapon,&fact,&found,error)) return false;
        if(found && fact.available) *out=fact.item;
    } else {
        application_bot_q3_arsenal q3;bool found,present;int32_t weapon,phase,time;
        if(!application_bot_q3_arsenal_read(provider,&q3,&found,error)) return false;
        if(!found) return true;
        if(!q3.guest && (slot>=QA_Q3_WEAPON_COUNT || !q3.weapons[slot])) return true;
        if(!application_bot_q3_weapon_state(provider,actor,&weapon,&phase,&time,&present,error)) return false;
        if(!present) return true;
        if(q3.guest) {
            for(size_t i=0;i<q3.count;++i) if(q3.rows[i].weapon==slot) {
                const application_q3_catalog_weapon *row=q3.rows+i;
                qa_bot_weapon_info info;qa_bot_projectile_info projectile;
                if(!qa_bot_runtime_weapon_config_info(q3.runtime,(uint32_t)slot,
                    &info,&projectile,&found,error)) return false;
                if(found && info.valid && count(bots,actor,row->item)>0 &&
                    (info.ammo_amount<=0 || (row->ammo && count(bots,actor,row->ammo)>=info.ammo_amount)))
                    *out=row->item;
                break;
            }
            return true;
        }
        qa_item_id item=q3.weapons[slot],ammo=q3.ammo[slot];
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
        if(!candidate->weapon.valid || !candidate->available ||
           !(candidate->travel_modes&QA_NAV_CAPABILITY(mode))) continue;
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
