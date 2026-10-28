/* Port of src/content/q1/equipment/threewave-weapon.ts. GPL-2.0-or-later. */
#include "qa/content/q1/equipment/threewave_weapon.h"

#include <string.h>

#include "qa/foundation/support.h"

static double fr(double value) { return (float)value; }

static void frame(qa_q1_threewave_weapon *weapon, qa_actor_id actor, double value) {
  qa_q1_threewave_state_for(weapon->core, actor)->weapon_frame = value;
  weapon->hooks.frame(weapon->hooks.ctx, actor, value);
}

static double moving_frame(qa_q1_threewave_weapon *weapon, qa_actor_id actor) {
  qa_q1_actor *entity = qa_q1_get_entity(weapon->core->game, &actor);
  if (entity == NULL) qa_range_error("Threewave weapon owner requires a shared body");
  return qa_q1_length(entity->body.velocity) >= 750 ? 4 : 3;
}

static void launch_frame(void *ctx, qa_q1_services *game, qa_q1_actor *timer) {
  qa_q1_threewave_weapon *weapon = ctx;
  if (timer->has_owner) {
    const qa_actor_id actor = timer->owner;
    const bool live = qa_q1_get_entity(game, &actor) != NULL || qa_q1_is_player(game, &actor);
    if (live) {
      qa_q1_threewave_state_for(weapon->core, actor)->has_animation = false;
      if (qa_q1_health(game, actor) > 0 && weapon->hooks.selected(weapon->hooks.ctx, actor)) {
        frame(weapon, actor, 3);
        if (weapon->hooks.available(weapon->hooks.ctx, actor))
          (void)qa_q1_threewave_fire(weapon->core, actor);
      }
    }
  }
  qa_q1_remove(game, timer);
}

void qa_q1_threewave_weapon_init(qa_q1_threewave_weapon *weapon, qa_q1_threewave_grapple *core,
                                 qa_q1_threewave_weapon_hooks hooks) {
  weapon->core = core;
  weapon->hooks = hooks;
  qa_q1_callback_handlers h = {0};
  h.ctx = weapon;
  h.action = launch_frame;
  qa_q1_callback_registry_register(core->game->named, "ctf:hook_launch_frame", h);
}

bool qa_q1_threewave_weapon_attack(qa_q1_threewave_weapon *weapon, qa_actor_id actor) {
  if (!weapon->hooks.available(weapon->hooks.ctx, actor)) return false;
  qa_q1_services *game = weapon->core->game;
  qa_q1_threewave_state *state = qa_q1_threewave_state_for(weapon->core, actor);
  if (state->attack_finished > game->time) return false;
  state->attack_finished = fr(game->time + 0.1);
  if (qa_q1_threewave_hook(weapon->core, actor) != NULL) {
    frame(weapon, actor, moving_frame(weapon, actor));
    return true;
  }
  if (state->has_animation) return true;
  qa_q1_actor *timer = qa_q1_create(game, "ctf_hook_animation", NULL, NULL);
  if (timer == NULL) return true; /* OOM only; TS has no counterpart */
  timer->has_owner = true;
  timer->owner = actor;
  state = qa_q1_threewave_state_for(weapon->core, actor);
  state->has_animation = true;
  state->animation = timer->actor.id;
  frame(weapon, actor, 2);
  qa_q1_schedule(game, timer, 0.1, qa_q1_callback_registry_action(game->named, timer, "ctf:hook_launch_frame"));
  return true;
}

void qa_q1_threewave_weapon_animate(qa_q1_threewave_weapon *weapon, qa_actor_id actor) {
  qa_q1_threewave_state *state = qa_q1_threewave_state_for(weapon->core, actor);
  qa_q1_services *game = weapon->core->game;
  if (qa_q1_threewave_hook(weapon->core, actor) != NULL) {
    const double next = moving_frame(weapon, actor);
    if (state->weapon_frame != next) frame(weapon, actor, next);
  } else if (!state->has_animation && state->weapon_frame != 0) {
    if (state->weapon_frame != 5) {
      frame(weapon, actor, 5);
      qa_q1_threewave_state_for(weapon->core, actor)->release_time = fr(game->time + 0.1);
    } else if (game->time >= state->release_time) {
      frame(weapon, actor, 0);
    }
  }
}

void qa_q1_threewave_weapon_holster(qa_q1_threewave_weapon *weapon, qa_actor_id actor) {
  frame(weapon, actor, 0);
}
bool qa_q1_threewave_weapon_is_holstered(qa_q1_threewave_weapon *weapon) {
  (void)weapon;
  return true;
}
void qa_q1_threewave_weapon_resume(qa_q1_threewave_weapon *weapon, qa_actor_id actor) {
  frame(weapon, actor, 0);
}

void qa_q1_threewave_character_pose(qa_q1_threewave_grapple *core, qa_actor_id actor, bool *out_axe_pose,
                                    bool *out_has_frame, double *out_frame) {
  const double frame_value = qa_q1_threewave_state_for(core, actor)->weapon_frame;
  qa_q1_actor *hook = qa_q1_threewave_hook(core, actor);
  *out_axe_pose = true;
  if (frame_value == 2) {
    *out_has_frame = true;
    *out_frame = 137;
  } else if (frame_value == 3) {
    *out_has_frame = true;
    *out_frame = hook != NULL && core->game->time < qa_q1_actor_number(hook, "ctf.fired") + 0.1 ? 138 : 139;
  } else if (frame_value == 4) {
    *out_has_frame = true;
    *out_frame = 73;
  } else if (frame_value == 5) {
    *out_has_frame = true;
    *out_frame = 140;
  } else {
    *out_has_frame = false;
    *out_frame = 0;
  }
}
