/*
 * src/network/q1/codecs/codec.ts and src/network/q2/codecs/codec.ts are type
 * declarations plus default-valued data classes. These checks pin every TS field
 * default and clear(); the protocol codecs that implement the interface have their
 * own tests. GPL-2.0-or-later.
 */
#include <stdio.h>
#include <stdlib.h>

#include "qa/network/q1/codecs/codec.h"
#include "qa/network/q2/codecs/codec.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define VEC_ZERO(w_) ((w_).v[0] == 0 && (w_).v[1] == 0 && (w_).v[2] == 0)

int main(void) {
  qa_q1_entity_update u; qa_q1_entity_update_init(&u);
  CHECK(VEC_ZERO(u.origin) && VEC_ZERO(u.angles));
  CHECK(u.modelindex == 0 && u.frame == 0 && u.colormap == 0 && u.skin == 0 && u.effects == 0);
  CHECK(u.alpha == QA_Q1_ENTALPHA_DEFAULT && u.scale == QA_Q1_ENTSCALE_DEFAULT);
  CHECK(!u.movetype_step && !u.sendinterval && u.lerpfinish == 0);
  qa_q1_wire_entity_state baseline; qa_q1_wire_entity_state_init(&baseline);
  CHECK(u.baseline.alpha == baseline.alpha && u.baseline.scale == baseline.scale);

  qa_q1_clientdata_codec c; qa_q1_clientdata_codec_init(&c);
  CHECK(c.viewheight == 0 && c.idealpitch == 0 && VEC_ZERO(c.punchangle) && VEC_ZERO(c.velocity));
  CHECK(c.items == 0 && !c.onground && !c.inwater && c.weaponframe == 0 && c.armorvalue == 0 && c.weaponmodelindex == 0);
  CHECK(c.health == 0 && c.currentammo == 0 && c.ammo_shells == 0 && c.ammo_nails == 0 && c.ammo_rockets == 0 && c.ammo_cells == 0 && c.weapon == 0);
  CHECK(c.standard_quake && c.alpha == QA_Q1_ENTALPHA_DEFAULT);

  qa_q1_sound_message s; qa_q1_sound_message_init(&s);
  CHECK(s.ent == 0 && s.channel == 0 && s.sound_num == 0 && s.volume == 0 && s.attenuation == 0 && VEC_ZERO(s.origin));

  qa_q1_entity_update_tail t; qa_q1_entity_update_tail_init(&t);
  CHECK(!t.has_alpha && t.alpha == QA_Q1_ENTALPHA_DEFAULT && !t.has_scale && t.scale == QA_Q1_ENTSCALE_DEFAULT);
  CHECK(!t.has_frame2 && t.frame_high == 0 && !t.has_model2 && t.model_high == 0 && !t.has_lerpfinish && t.lerpfinish == 0);
  t.has_alpha = true; t.alpha = 9; t.has_scale = true; t.scale = 9; t.has_frame2 = true; t.frame_high = 9; t.has_model2 = true; t.model_high = 9; t.has_lerpfinish = true; t.lerpfinish = 9;
  qa_q1_entity_update_tail_clear(&t);
  CHECK(!t.has_alpha && t.alpha == QA_Q1_ENTALPHA_DEFAULT && !t.has_scale && t.scale == QA_Q1_ENTSCALE_DEFAULT);
  CHECK(!t.has_frame2 && t.frame_high == 0 && !t.has_model2 && t.model_high == 0 && !t.has_lerpfinish && t.lerpfinish == 0);

  qa_q1_clientdata_tail d; qa_q1_clientdata_tail_init(&d);
  CHECK(d.weaponalpha == QA_Q1_ENTALPHA_DEFAULT && d.weapon_high == 0);
  d.weapon_high = d.armor_high = d.ammo_high = d.shells_high = d.nails_high = d.rockets_high = d.cells_high = d.weaponframe_high = d.weaponalpha = 5;
  qa_q1_clientdata_tail_clear(&d);
  CHECK(d.weapon_high == 0 && d.armor_high == 0 && d.ammo_high == 0 && d.shells_high == 0 && d.nails_high == 0 && d.rockets_high == 0 && d.cells_high == 0 && d.weaponframe_high == 0);
  CHECK(d.weaponalpha == QA_Q1_ENTALPHA_DEFAULT);

  qa_q1_sound_header h; qa_q1_sound_header_init(&h);
  CHECK(h.ent == 0 && h.channel == 0 && h.sound_num == 0);

  qa_qw_entity_word w; qa_qw_entity_word_init(&w);
  CHECK(w.bits == 0 && w.ext == 0 && w.number == 0 && !w.remove);
  w.bits = 1; w.ext = 1; w.number = 1; w.remove = true;
  qa_qw_entity_word_clear(&w);
  CHECK(w.bits == 0 && w.ext == 0 && w.number == 0 && !w.remove);

  /* q2 owned batch-move results release and reset. */
  qa_q2_clc_batch_move move; move.lastframe = 3; move.num_dups = 1; move.frame_count = 2;
  move.frames = (qa_q2_clc_batch_move_frame *)calloc(2, sizeof *move.frames);
  move.frames[0].cmds = (qa_q2_wire_usercmd *)calloc(1, sizeof *move.frames[0].cmds); move.frames[0].cmd_count = 1;
  qa_q2_clc_batch_move_free(&move);
  CHECK(move.frames == NULL && move.frame_count == 0);
  qa_q2_clc_batch_move_free(&move);
  printf("network codec shapes: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
