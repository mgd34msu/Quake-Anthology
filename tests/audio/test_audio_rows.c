/* Rows read against src/audio/engine.ts: actorEntityKey, entity(),
 * retireAbsentActors and mixer entityActive. Reproduces the last two tests of
 * tests/audio/component-loops.test.ts on test_audio_engine.c's fixtures.
 * GPL-2.0-or-later. */
#define main test_audio_engine_main
#include "test_audio_engine.c"
#undef main

static qa_unified_audio *bounded_engine(double max_actors) {
  qa_unified_audio_options options = {0};
  options.milliseconds = milliseconds;
  options.random = zero_random;
  options.has_max_actors = true;
  options.max_actors = max_actors;
  qa_unified_audio *audio = qa_unified_audio_create(&options);
  CHECK(audio != NULL);
  qa_actor_id none;
  memset(&none, 0, sizeof none);
  qa_audio_listener listeners[1] = {listener_for(qa_identity_owner_seat(&identity, 0), false, none, 1)};
  qa_unified_audio_set_listeners(audio, listeners, 1);
  return audio;
}

static void test_retire_absent_actors(void) {
  /* Actors that leave the world release their entries: 1000 generations fit
   * in an eight-entry table. */
  qa_unified_audio *audio = bounded_engine(8);
  for (int64_t generation = 0; generation < 1000; generation++) {
    qa_actor_id spawned = qa_identity_owner_actor(&identity, 9, generation);
    qa_unified_audio_update_actor(audio, spawned, origin_zero);
    qa_unified_audio_retire_absent_actors(audio, &spawned, 1);
  }
  CHECK_ABORTS({
    for (int64_t slot = 0; slot < 8; slot++)
      qa_unified_audio_update_actor(audio, qa_identity_owner_actor(&identity, slot + 20, 0), origin_zero);
  });
  qa_unified_audio_destroy(audio);

  /* An actor with a playing loop keeps its entry until the loop ends. */
  audio = bounded_engine(3);
  qa_actor_id looping = qa_identity_owner_actor(&identity, 30, 0);
  qa_unified_audio_update_actor(audio, looping, origin_zero);
  qa_loop_sound loop = {0};
  loop.sound = &asset;
  loop.family = QA_SOUND_FAMILY_Q2;
  loop.actor = looping;
  loop.origin.kind = QA_SOUND_ORIGIN_FIXED;
  loop.origin.position = origin_zero;
  loop.audience.kind = QA_AUDIO_AUDIENCE_WORLD;
  loop.volume = 1;
  loop.attenuation = 1;
  loop.velocity = origin_zero;
  loop.frame_number = 1;
  loop.lifetime = QA_LOOP_LIFETIME_PERSISTENT;
  qa_unified_audio_loop(audio, &loop);
  qa_unified_audio_end_loop_frame(audio);
  qa_unified_audio_retire_absent_actors(audio, NULL, 0);
  qa_unified_audio_update_actor(audio, qa_identity_owner_actor(&identity, 31, 0), origin_zero);
  CHECK_ABORTS(qa_unified_audio_update_actor(audio, qa_identity_owner_actor(&identity, 32, 0), origin_zero));
  qa_unified_audio_destroy(audio);

  /* Without a loop, the same actor retires and its entity is reused. */
  audio = bounded_engine(3);
  qa_unified_audio_update_actor(audio, looping, origin_zero);
  qa_unified_audio_retire_absent_actors(audio, NULL, 0);
  qa_unified_audio_update_actor(audio, qa_identity_owner_actor(&identity, 31, 0), origin_zero);
  qa_unified_audio_update_actor(audio, qa_identity_owner_actor(&identity, 32, 0), origin_zero);
  CHECK_ABORTS(qa_unified_audio_update_actor(audio, qa_identity_owner_actor(&identity, 33, 0), origin_zero));
  qa_unified_audio_destroy(audio);
}

int main(void) {
  init_fixtures();
  test_retire_absent_actors();
  qa_identity_owner_destroy(&identity);
  printf("test_audio_rows: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
