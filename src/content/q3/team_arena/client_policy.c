/* Port of src/content/q3/team-arena/client-policy.ts. GPL-2.0-or-later. */
#include "qa/content/q3/team_arena/client_policy.h"

#include <stdint.h>

#include "qa/foundation/support.h"

enum { MASK_SPECTATOR = 1 | 0x10000, VOTE_FLAGS = 0x4000 | 0x80000, EF_TALK = 0x1000, EF_FIRING = 0x100 };

static qa_q3_game_client *client_of(qa_q3_game_entity *entity) {
  if (entity->client == NULL) qa_range_error("Client policy requires a client entity");
  return entity->client;
}

static double client_number(qa_q3_ta_client_policy_context *context, qa_q3_game_client *client) {
  for (int index = 0; index < QA_Q3_MAX_CLIENTS; index++)
    if (context->pool->clients[index] == client) return index;
  qa_range_error("Client policy received a client outside its entity pool");
}

void qa_q3_ta_spectator_think(qa_q3_ta_client_policy_context *context, qa_q3_game_entity *entity,
    qa_q3_base_user_command command) {
  qa_q3_game_client *client = client_of(entity);
  if (client->sess.spectator_state != QA_Q3_SPEC_FOLLOW) {
    client->ps.pm_type = QA_Q3_PM_SPECTATOR;
    /* TS client.ps.speed = 400: no speed field in the landed mirror. */
    qa_q3_ta_client_movement_options options = {MASK_SPECTATOR, false, 0, false, false, 0};
    (void)context->movement.move_client(context->movement.ctx, entity, command, options);
    entity->s.origin = client->ps.origin;
    context->touch_triggers(context->ctx, entity);
    context->world->unlink(context->world->ctx, entity->s.number);
  }
  client->old_buttons = client->buttons;
  client->buttons = command.buttons;
  if (((int32_t)client->buttons & QA_Q3_BUTTON_ATTACK) != 0 && ((int32_t)client->old_buttons & QA_Q3_BUTTON_ATTACK) == 0)
    context->follow_cycle(context->ctx, entity, 1);
}

void qa_q3_ta_spectator_client_end_frame(qa_q3_ta_client_policy_context *context, qa_q3_game_entity *entity) {
  qa_q3_game_client *client = client_of(entity);
  if (client->sess.spectator_state == QA_Q3_SPEC_FOLLOW) {
    double number = client->sess.spectator_client;
    if (number == -1) number = context->follow1;
    else if (number == -2) number = context->follow2;
    if (number >= 0) {
      qa_q3_game_client *followed = qa_q3_entity_pool_client_at(context->pool, number);
      if (followed->pers.connected == QA_Q3_CONN_CONNECTED &&
          followed->sess.session_team != QA_Q3_BASE_TEAM_SPECTATOR) {
        double flags = (double)(((int32_t)followed->ps.e_flags & ~VOTE_FLAGS) | ((int32_t)client->ps.e_flags & VOTE_FLAGS));
        client->ps = followed->ps; /* TS ps.copyFrom */
        client->ps.pm_flags = (double)((int32_t)client->ps.pm_flags | QA_Q3_FLAG_FOLLOW);
        client->ps.e_flags = flags;
        return;
      }
      if (client->sess.spectator_client >= 0) {
        client->sess.spectator_state = QA_Q3_SPEC_FREE;
        context->client_begin(context->ctx, client_number(context, client));
      }
    }
  }
  qa_q3_game_client *current = client_of(entity);
  if (current->sess.spectator_state == QA_Q3_SPEC_SCOREBOARD)
    current->ps.pm_flags = (double)((int32_t)current->ps.pm_flags | QA_Q3_FLAG_SCOREBOARD);
  else
    current->ps.pm_flags = (double)((int32_t)current->ps.pm_flags & ~QA_Q3_FLAG_SCOREBOARD);
}

bool qa_q3_ta_client_inactivity_timer(qa_q3_ta_client_policy_context *context, qa_q3_game_client *client) {
  qa_q3_game_user_command_mirror *command = &client->pers.cmd;
  if (context->inactivity_seconds == 0) {
    client->inactivity_time = (int32_t)(context->time + 60000);
    client->inactivity_warning = false;
  } else if (command->forwardmove != 0 || command->rightmove != 0 || command->upmove != 0 ||
      ((int32_t)command->buttons & QA_Q3_BUTTON_ATTACK) != 0) {
    client->inactivity_time = (int32_t)(context->time + (double)(int32_t)((int32_t)context->inactivity_seconds * 1000));
    client->inactivity_warning = false;
  } else if (!client->pers.local_client) {
    if (context->time > client->inactivity_time) {
      context->drop_client(context->ctx, client_number(context, client), "Dropped due to inactivity");
      return false;
    }
    if (context->time > (int32_t)(client->inactivity_time - 10000) && !client->inactivity_warning) {
      client->inactivity_warning = true;
      context->send_server_command(context->ctx, client_number(context, client),
          "cp \"Ten seconds until inactivity drop!\n\"");
    }
  }
  return true;
}

void qa_q3_ta_client_intermission_think(qa_q3_game_client *client) {
  client->ps.e_flags = (double)((int32_t)client->ps.e_flags & ~(EF_TALK | EF_FIRING));
  client->old_buttons = client->buttons;
  client->buttons = client->pers.cmd.buttons;
  if (((int32_t)client->buttons & (QA_Q3_BUTTON_ATTACK | QA_Q3_BUTTON_USE_HOLDABLE) &
       ((int32_t)client->old_buttons ^ (int32_t)client->buttons)) != 0)
    client->ready_to_exit = true;
}
