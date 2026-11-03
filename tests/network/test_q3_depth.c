/* Q3 entity and player delta writers against bytes captured from TS, with the same streams
 * decoded back. Covers every field of both state tables. GPL-2.0-or-later. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/network/q3/message.h"
#include "qa/network/q3/state_delta.h"
#include "q3_depth_corpus.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void check_hex(const char *label, const uint8_t *got, size_t length, const char *hex) {
  checks++;
  size_t want = strlen(hex) / 2;
  bool same = want == length;
  for (size_t i = 0; same && i < want; i++) {
    unsigned v;
    sscanf(hex + 2 * i, "%2x", &v);
    if (got[i] != v) same = false;
  }
  if (!same) {
    failures++;
    fprintf(stderr, "FAIL %s\n got ", label);
    for (size_t i = 0; i < length; i++) fprintf(stderr, "%02x", got[i]);
    fprintf(stderr, "\n exp %s\n", hex);
  }
}
static void make_entity(qa_q3_entity_state_record *e) {
  qa_q3_entity_state_init(e);
  e->number = 7; e->e_type = 3; e->e_flags = 0x1234;
  e->pos.type = 2; e->pos.time = 1000; e->pos.duration = 250;
  e->pos.base = (qa_vec3){100.5, -20, 42}; e->pos.delta = (qa_vec3){1.25, 2, -3};
  e->apos.type = 1; e->apos.time = 50; e->apos.duration = 0;
  e->apos.base = (qa_vec3){10, 20, 30}; e->apos.delta = (qa_vec3){0.5, 0, 0};
  e->time = 77; e->time2 = -5;
  e->origin = (qa_vec3){100.5, -20, 9000}; e->origin2 = (qa_vec3){1, 2, 3};
  e->angles = (qa_vec3){90, 180, -45}; e->angles2 = (qa_vec3){0.125, 0, 0};
  e->other_entity_num = 12; e->other_entity_num2 = 13; e->ground_entity_num = 1022;
  e->constant_light = 0xffffff; e->loop_sound = 4; e->modelindex = 5; e->modelindex2 = 6;
  e->client_num = 3; e->frame = 17; e->solid = 0x10203; e->event = 9; e->event_parm = 200;
  e->powerups = 0xffff; e->weapon = 4; e->legs_anim = 33; e->torso_anim = 44; e->generic1 = 8;
}
static void make_player(qa_q3_player_state_record *p) {
  qa_q3_player_state_record_init(p, QA_Q3_PRODUCT_BASEQ3, 0, 0, 0);
  p->command_time = 1000; p->pm_type = 1; p->bob_cycle = 9; p->pm_flags = 0x40; p->pm_time = 3;
  p->origin = (qa_vec3){1.5, -2, 3000}; p->velocity = (qa_vec3){320, 0.25, -8};
  p->weapon_time = 7; p->gravity = 800; p->speed = 320; p->delta_angles = (qa_vec3){100, -200, 300};
  p->ground_entity_num = 1022; p->legs_timer = 5; p->legs_anim = 33; p->torso_timer = 6; p->torso_anim = 44;
  p->movement_dir = 2; p->grapple_point = (qa_vec3){1, 2, 3}; p->e_flags = 0x1234; p->event_sequence = 4;
  p->events[0] = 1; p->events[1] = 2; p->event_parms[0] = 3; p->event_parms[1] = 4;
  p->external_event = 5; p->external_event_parm = 6; p->external_event_time = 7; p->client_num = 2;
  p->weapon = 3; p->weapon_state = 1; p->viewangles = (qa_vec3){10, 20.5, 30}; p->viewheight = 26;
  p->damage_event = 1; p->damage_yaw = 2; p->damage_pitch = 3; p->damage_count = 4;
  p->stats[0] = 100; p->stats[15] = -1; p->persistant[1] = 5; p->powerups[2] = 70000; p->ammo[3] = 50;
  p->generic1 = 8; p->loop_sound = 9; p->jumppad_ent = 10;
}
static uint8_t *entity_bytes(const qa_q3_entity_state_record *from, const qa_q3_entity_state_record *to, bool force, size_t *length) {
  qa_q3_message_writer *w = NULL;
  CHECK(qa_q3_message_writer_init_default(&w));
  qa_q3_write_delta_entity(w, from, to, force);
  uint8_t *bytes = NULL;
  CHECK(qa_q3_message_writer_to_bytes(w, &bytes, length));
  qa_q3_message_writer_destroy(w);
  return bytes;
}
static uint8_t *player_bytes(const qa_q3_player_state_record *from, const qa_q3_player_state_record *to, size_t *length) {
  qa_q3_message_writer *w = NULL;
  CHECK(qa_q3_message_writer_init_default(&w));
  qa_q3_write_delta_player_state(w, from, to);
  uint8_t *bytes = NULL;
  CHECK(qa_q3_message_writer_to_bytes(w, &bytes, length));
  qa_q3_message_writer_destroy(w);
  return bytes;
}
int main(void) {
  qa_q3_entity_state_record zero, a, b, back;
  qa_q3_entity_state_init(&zero);
  make_entity(&a);
  make_entity(&b);
  b.origin = (qa_vec3){101, -20, 9000}; b.event = 0; b.frame = 18;
  size_t n;
  uint8_t *bytes = entity_bytes(&zero, &a, false, &n);
  check_hex("entity full", bytes, n, q3_entity_full);
  qa_q3_message_reader *r = NULL;
  CHECK(qa_q3_message_reader_init(&r, bytes, n, QA_Q3_MESSAGE_BITSTREAM, NULL));
  CHECK(qa_q3_message_reader_read_bits(r, 10) == 7);
  qa_q3_read_delta_entity(r, &zero, 7, NULL, &back);
  /* Every field and binary32 value survives. */
#define SAME(f) CHECK(back.f == a.f)
#define SAME_V(f) do { SAME(f.x); SAME(f.y); SAME(f.z); } while (0)
  SAME(number); SAME(e_type); SAME(e_flags); SAME(pos.type); SAME(pos.time); SAME(pos.duration); SAME_V(pos.base); SAME_V(pos.delta);
  SAME(apos.type); SAME(apos.time); SAME(apos.duration); SAME_V(apos.base); SAME_V(apos.delta); SAME(time); SAME(time2);
  SAME_V(origin); SAME_V(origin2); SAME_V(angles); SAME_V(angles2); SAME(other_entity_num); SAME(other_entity_num2);
  SAME(ground_entity_num); SAME(constant_light); SAME(loop_sound); SAME(modelindex); SAME(modelindex2); SAME(client_num);
  SAME(frame); SAME(solid); SAME(event); SAME(event_parm); SAME(powerups); SAME(weapon); SAME(legs_anim); SAME(torso_anim); SAME(generic1);
#undef SAME
#undef SAME_V
  qa_q3_message_reader_destroy(r);
  free(bytes);
  bytes = entity_bytes(&a, &b, false, &n); check_hex("entity delta", bytes, n, q3_entity_delta); free(bytes);
  bytes = entity_bytes(&a, &a, true, &n); check_hex("entity forced", bytes, n, q3_entity_same_force); free(bytes);
  bytes = entity_bytes(&a, &a, false, &n); check_hex("entity unchanged", bytes, n, q3_entity_same); free(bytes);
  bytes = entity_bytes(&a, NULL, false, &n); check_hex("entity removed", bytes, n, q3_entity_remove); free(bytes);
  bytes = entity_bytes(NULL, &a, false, &n); check_hex("entity from null", bytes, n, q3_entity_null_from); free(bytes);

  qa_q3_player_state_record pz, p1, p2, pback;
  qa_q3_player_state_record_init(&pz, QA_Q3_PRODUCT_BASEQ3, 0, 0, 0);
  make_player(&p1);
  make_player(&p2);
  p2.origin = (qa_vec3){2, -2, 3000}; p2.stats[0] = 75; p2.ammo[3] = 49; p2.command_time = 1050;
  bytes = player_bytes(&pz, &p1, &n);
  check_hex("player full", bytes, n, q3_player_full);
  CHECK(qa_q3_message_reader_init(&r, bytes, n, QA_Q3_MESSAGE_BITSTREAM, NULL));
  qa_q3_read_delta_player_state(r, &pz, QA_Q3_PRODUCT_BASEQ3, NULL, &pback);
  CHECK(pback.command_time == 1000 && pback.origin.x == 1.5 && pback.origin.z == 3000 && pback.velocity.y == 0.25);
  CHECK(pback.stats[15] == -1 && pback.powerups[2] == 70000 && pback.ammo[3] == 50 && pback.events[1] == 2);
  CHECK(pback.viewangles.y == 20.5 && pback.jumppad_ent == 10);
  qa_q3_message_reader_destroy(r);
  free(bytes);
  bytes = player_bytes(NULL, &p1, &n); check_hex("player from null", bytes, n, q3_player_null); free(bytes);
  bytes = player_bytes(&p1, &p2, &n); check_hex("player delta", bytes, n, q3_player_delta); free(bytes);
  bytes = player_bytes(&p1, &p1, &n); check_hex("player unchanged", bytes, n, q3_player_same); free(bytes);
  printf("q3_depth: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
