#include "qa/bot_knowledge.h"
#include <math.h>
#include <string.h>

typedef struct role_profile { int16_t weapon,ammo,accuracy,skill; } role_profile;
static const role_profile profiles[]={
    {-1,-1,-1,-1},{4,-1,-1,-1},{6,19,8,-1},{5,18,9,-1},{7,20,11,18},
    {8,23,10,17},{9,22,12,-1},{10,24,14,-1},{11,21,13,19},{13,25,15,20},
    {14,-1,-1,-1},{15,26,-1,-1},{16,27,-1,-1},{17,28,-1,-1}
};
static const role_profile *profile(int32_t role)
{
    return role>0 && (size_t)role<sizeof(profiles)/sizeof(*profiles) ? profiles+role : NULL;
}
int32_t qa_bot_weapon_role(const qa_bot_weapon_knowledge *c)
{
    if (!c) return 0;
    if (c->personality_role>=0) return c->personality_role;
    if (c->melee) return 1;
    if (c->projectile.gravity>0) return 4;
    if (c->weapon.speed>0) return c->projectile.damage_type&2 ? 5 : 8;
    if (c->weapon.projectile_count>=4) return 3;
    if (c->weapon.horizontal_spread>0 || c->weapon.vertical_spread>0) return 2;
    return c->weapon.reload<=.2f ? 6 : 7;
}
qa_bot_weapon_tactics qa_bot_weapon_tactics_for(const qa_bot_weapon_knowledge *c)
{
    int32_t role=qa_bot_weapon_role(c);
    const role_profile *p=profile(role);
    return (qa_bot_weapon_tactics){.melee=c && c->melee,.ranged_limit=c && c->ranged_limit,
        .maximum_range=c ? c->maximum_range : 0,.weakness=role==2 ? 90 : 0,
        .predict_occluded_splash=role==4 || role==5 || role==9,
        .accuracy_characteristic=p ? p->accuracy : -1,.skill_characteristic=p ? p->skill : -1};
}
static int32_t inventory_value(qa_bot_inventory_bytes inventory,int32_t index)
{
    return qa_bot_inventory_value(inventory,index);
}
static bool owned(const qa_bot_weapon_knowledge *c,qa_bot_inventory_bytes inventory)
{
    if(c->personality_role<0) return inventory_value(inventory,c->weapon.weapon_inventory)>0;
    const role_profile *p=profile(c->personality_role);
    return p && inventory_value(inventory,p->weapon)>0;
}
static int32_t ammunition(const qa_bot_weapon_knowledge *c,qa_bot_inventory_bytes inventory)
{
    if(c->personality_role<0)
        return c->weapon.ammo_amount==0 ? 999 : inventory_value(inventory,c->weapon.ammo_inventory);
    const role_profile *p=profile(c->personality_role);
    return !p?0:p->ammo<0?999:inventory_value(inventory,p->ammo);
}
static bool source_inventory_read(void *context,int32_t index,int32_t *out,qa_error *error)
{
    const qa_bot_inventory_bytes *inventory=context;
    if(index<0 || (size_t)index>=inventory->count) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Fuzzy inventory index exceeds its source array");return false;
    }
    *out=inventory_value(*inventory,index);return true;
}
bool qa_bot_knowledge_choose(qa_bot_runtime *runtime,uint32_t handle,
                              const qa_bot_weapon_knowledge *candidates,size_t count,
                              qa_bot_inventory_bytes inventory,
                              int32_t scratch[QA_BOT_INVENTORY_SIZE],int32_t *out,qa_error *e)
{
    if (!runtime || !inventory.data || inventory.count!=QA_BOT_INVENTORY_SIZE ||
        !scratch || !out || (count && !candidates)) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid bot arsenal choice input"); return false;
    }
    float best=0; double best_rate=0;
    int32_t choice=0,best_role=-1;
    for (size_t i=0;i<count;++i) {
        const qa_bot_weapon_knowledge *c=candidates+i;
        int32_t role=qa_bot_weapon_role(c);
        qa_bot_inventory_view observed={.count=inventory.count,.context=&inventory,.read=source_inventory_read};
        if (!c->weapon.valid) continue;
        const role_profile *p=profile(role);
        if (!p) continue;
        bool projection=c->personality_role<0;
        if (projection) {
            if(!owned(c,inventory) || ammunition(c,inventory)<c->weapon.ammo_amount) continue;
            double distance=hypot((double)inventory_value(inventory,QA_BOT_INV_ENEMY_DISTANCE),
                (double)inventory_value(inventory,QA_BOT_INV_ENEMY_HEIGHT));
            if(c->ranged_limit && distance>c->maximum_range) continue;
            for(int32_t index=0;index<QA_BOT_INVENTORY_SIZE;++index)
                scratch[index]=inventory_value(inventory,index);
            scratch[p->weapon]=1;
            if (p->ammo>=0) scratch[p->ammo]=ammunition(c,inventory);
            observed=(qa_bot_inventory_view){.data=scratch,.count=QA_BOT_INVENTORY_SIZE};
        }
        float weight=0; bool found;
        bool ok=qa_bot_runtime_weapon_weight_view(runtime,handle,(uint32_t)role,&observed,
                                              &weight,&found,e);
        if (!ok) return false;
        if (!found) continue;
        double rate=c->weapon.reload>0 ? c->selected_projectile_damage*c->weapon.projectile_count/c->weapon.reload : 0;
        if (weight>best || (weight>0 && weight==best && role==best_role && rate>best_rate)) {
            best=weight; choice=c->weapon.number; best_role=role; best_rate=rate;
        }
    }
    *out=choice;
    return true;
}
int32_t qa_bot_knowledge_activation(const qa_bot_weapon_knowledge *candidates,size_t count,
                                    qa_bot_inventory_bytes inventory,bool team_arena)
{
    static const uint8_t roles[]={2,3,8,6,13,11,7,5,9};
    if (!inventory.data || inventory.count!=QA_BOT_INVENTORY_SIZE || (count && !candidates)) return -1;
    for (size_t r=0;r<sizeof(roles)/sizeof(*roles);++r) {
        if (!team_arena && (roles[r]==13 || roles[r]==11)) continue;
        for (size_t i=0;i<count;++i) {
            const qa_bot_weapon_knowledge *c=candidates+i;
            if (!c->weapon.valid || c->melee || c->projectile.gravity!=0 ||
                !owned(c,inventory) || qa_bot_weapon_role(c)!=roles[r]) continue;
            if (c->personality_role<0?ammunition(c,inventory)>=c->weapon.ammo_amount:
                ammunition(c,inventory)>0)
                return c->weapon.number;
        }
    }
    return -1;
}
int32_t qa_bot_knowledge_travel(const qa_bot_weapon_knowledge *candidates,size_t count,
                                qa_bot_inventory_bytes inventory,qa_nav_travel mode)
{
    if (!inventory.data || inventory.count!=QA_BOT_INVENTORY_SIZE || (count && !candidates) || mode<0 || mode>=QA_NAV_TRAVEL_COUNT)
        return -1;
    for (size_t i=0;i<count;++i) {
        const qa_bot_weapon_knowledge *c=candidates+i;
        if (c->weapon.valid && (c->travel_modes&QA_NAV_CAPABILITY(mode)) && owned(c,inventory) &&
            ammunition(c,inventory)>=c->weapon.ammo_amount)
            return c->weapon.number;
    }
    return -1;
}
float qa_bot_knowledge_aggression(const qa_bot_weapon_knowledge *candidates,size_t count,
                                   int32_t weapon,qa_bot_inventory_bytes inventory)
{
    static const struct { uint8_t role,ammo,value; } ranks[]={
        {9,7,100},{7,5,95},{6,50,90},{5,5,90},{8,40,85},{4,10,80},{3,10,50}
    };
    if (!inventory.data || inventory.count!=QA_BOT_INVENTORY_SIZE || (count && !candidates)) return 0;
    bool melee=false;
    for (size_t i=0;i<count;++i) if (candidates[i].weapon.number==weapon) { melee=candidates[i].melee; break; }
    if (inventory_value(inventory,QA_BOT_INV_QUAD) && (!melee || inventory_value(inventory,QA_BOT_INV_ENEMY_DISTANCE)<80)) return 70;
    int32_t health=inventory_value(inventory,QA_BOT_INV_HEALTH),armor=inventory_value(inventory,QA_BOT_INV_ARMOR);
    if (inventory_value(inventory,QA_BOT_INV_ENEMY_HEIGHT)>200 || health<60 || (health<80 && armor<40)) return 0;
    for (size_t r=0;r<sizeof(ranks)/sizeof(*ranks);++r)
        for (size_t i=0;i<count;++i)
            if (qa_bot_weapon_role(candidates+i)==ranks[r].role && owned(candidates+i,inventory) &&
                ammunition(candidates+i,inventory)>ranks[r].ammo) return ranks[r].value;
    return 0;
}
static bool gained(const qa_supply_preview_result *preview,qa_item_id item)
{
    for (size_t i=0;i<preview->weapon_count;++i)
        if (preview->weapons[i].item==item && preview->weapons[i].before<=0 && preview->weapons[i].given>0) return true;
    return false;
}
bool qa_bot_knowledge_pickup(const qa_bot_weapon_knowledge *candidates,size_t count,
                              const qa_supply_preview_result *preview,double *out,qa_error *e)
{
    if (!preview || !out || (count && !candidates) ||
        (preview->weapon_count && !preview->weapons) || (preview->ammo_count && !preview->ammo)) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid bot pickup utility preview"); return false;
    }
    *out=0;
    if (!preview->accepted) return true;
    for (size_t i=0;i<preview->weapon_count;++i) {
        const qa_pickup_receipt *receipt=preview->weapons+i;
        if (!(receipt->before<=0 && receipt->given>0)) continue;
        bool earlier=false;
        for (size_t j=0;j<i;++j)
            if (preview->weapons[j].item==receipt->item && preview->weapons[j].before<=0 && preview->weapons[j].given>0) { earlier=true; break; }
        if (earlier) continue;
        for (size_t j=0;j<count;++j)
            if (candidates[j].weapon.valid && candidates[j].has_supply && candidates[j].supply_weapon==receipt->item) { *out+=100; break; }
    }
    for (size_t i=0;i<preview->ammo_count;++i) {
        const qa_pickup_receipt *receipt=preview->ammo+i;
        if (!(receipt->given>0)) continue;
        double cost=INFINITY;
        for (size_t j=0;j<count;++j) {
            const qa_bot_weapon_knowledge *c=candidates+j;
            if (c->weapon.valid && c->has_supply && c->supply_ammo==receipt->item && c->ammo_per_shot>0 &&
                (c->owned || gained(preview,c->supply_weapon)) && c->ammo_per_shot<cost) cost=c->ammo_per_shot;
        }
        if (isfinite(cost)) *out+=receipt->given/cost;
    }
    return true;
}
