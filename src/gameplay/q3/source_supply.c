#include "internal.h"
#include "qa/game_q3_bots.h"

bool qa_q3_bot_arsenal_product_read(const qa_q3_game *game,qa_q3_product *out,qa_error *error) {
    if(!game || !out)
        return q3_fail(error,"Q3 arsenal product requires its GAME owner and output");
    *out=game->options.product;return true;
}

bool qa_q3_bot_supply_preview(qa_q3_game *game,qa_actor_id pickup,qa_actor_id recipient,
    const qa_q3_supply_services *services,qa_supply_preview_result *out,
    bool *eligible,bool *found,qa_error *error) {
    if(!game || !out || !eligible || !found || game->source_restored ||
       game->observation_depth==SIZE_MAX)
        return q3_fail(error,"Q3 supply preview requires its live source owner and outputs");
    *out=(qa_supply_preview_result){0};*eligible=false;*found=false;
    if(game->options.product!=QA_Q3_ARENA || !services || !services->preview) return true;
    ++game->observation_depth;bool ok=true;
    uint32_t item_slot,player_slot,client;
    const q3_actor *entry=q3_actor_const(game,pickup);
    if(!entry || entry->kind!=Q3_ACTOR_ITEM ||
       !q3_source_client_pointer(game,recipient,&client) ||
       !qa_q3_source_actor_slot(game,pickup,&item_slot,NULL) ||
       !qa_q3_source_actor_slot(game,recipient,&player_slot,NULL) ||
       !game->source_entities[item_slot].in_use || !game->source_entities[player_slot].in_use)
        goto done;
    qa_q3_item_spawn spawn=entry->state.item.spawn;
    size_t count;const qa_q3_item *items=qa_q3_items(game->options.product,&count);
    if(!spawn.item_index || spawn.item_index>=count) {
        ok=q3_fail(error,"Q3 supply item lost its actual product declaration");goto done;
    }
    const qa_q3_item *item=items+spawn.item_index;
    if(item->kind!=QA_Q3_ITEM_WEAPON && item->kind!=QA_Q3_ITEM_AMMO) goto done;
    q3_wire_entity_source *source=q3_wire_entity(game,pickup);
    if(!source || source->model!=(int32_t)spawn.item_index) {
        ok=q3_fail(error,"Q3 supply item model differs from its actual item declaration");goto done;
    }
    qa_q3_supply_descriptor descriptor={.pickup=pickup,.recipient=recipient,.item=item,
        .count=spawn.count,.generic1=source->generic1,
        .game_type=game->options.rules.game_type,.dropped=spawn.dropped,
        .weapon_respawn_seconds=game->options.rules.weapon_respawn_seconds,
        .team_weapon_respawn_seconds=game->options.rules.team_weapon_respawn_seconds};
    qa_q3_supply_kind kind=QA_Q3_SUPPLY_NATIVE;
    ok=services->preview(services->context,&descriptor,&kind,out,error);
    if(!ok) goto discard;
    if(kind!=QA_Q3_SUPPLY_NATIVE && kind!=QA_Q3_SUPPLY_REJECTED &&
       kind!=QA_Q3_SUPPLY_SELECTED) {
        ok=q3_fail(error,"Q3 supply preview returned an invalid admission kind");goto discard;
    }
    if(kind!=QA_Q3_SUPPLY_SELECTED) goto discard;
    entry=q3_actor_const(game,pickup);
    if(!entry || entry->kind!=Q3_ACTOR_ITEM ||
       !q3_source_client_pointer(game,recipient,&client) ||
       !game->source_entities[item_slot].in_use || !game->source_entities[player_slot].in_use ||
       !qa_actor_id_equal(game->source_entities[item_slot].actor,pickup) ||
       !qa_actor_id_equal(game->source_entities[player_slot].actor,recipient)) goto discard;
    *found=true;
    source=q3_wire_entity(game,pickup);
    qa_q3_wire_body body;
    if(!qa_q3_wire_body_read(game,item_slot,&body,error)) {ok=false;goto discard;}
    if(!source || entry->state.item.hidden || !body.colliding ||
       !(qa_collision_bits_overlap(body.collision.contents, qa_collision_bit(QA_CONTENT_TRIGGER))) || (source->flags&0x80) ||
       (game->source_entities[item_slot].server_flags&1) ||
       source->free_after_event || source->unlink_after_event) goto done;
    qa_combat_state combat;
    if(!qa_combat_read(game->options.services.combat,recipient,&combat,error)) {
        ok=false;goto discard;
    }
    *eligible=combat.health>=1;
    goto done;
discard:
    qa_supply_preview_free(out);*eligible=false;*found=false;
done:
    --game->observation_depth;return ok;
}
