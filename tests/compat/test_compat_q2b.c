/* Port of key TS cases for compat/q2 native-* modules. GPL-2.0-or-later. */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/compat/q2/native_combat_call.h"
#include "qa/compat/q2/native_damage.h"
#include "qa/compat/q2/native_input.h"
#include "qa/compat/q2/native_mod_client_stages.h"
#include "qa/compat/q2/native_mod_region.h"
#include "qa/compat/q2/native_primary_command_profile.h"
#include "qa/compat/q2/native_primary_drop_profile.h"
#include "qa/compat/q2/native_primary_inventory_profile.h"
#include "qa/compat/q2/native_primary_player.h"
#include "qa/compat/q2/native_primary_player_profile.h"
#include "qa/compat/q2/native_primary_profiles.h"
#include "qa/compat/q2/native_primary_reader.h"
#include "qa/compat/q2/native_primary_validation.h"
#include "qa/compat/q2/native_primary_weapon_profile.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)
#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static const char *XAT = "sha256:8187df3fd5b4d435d8227434d3351aad2b47e546236403e52adcd4d275810c45";
static const char *RET = "sha256:045d49c53722d9b922caf14f168dd28a97d4c514a6e443a3140560f8668baccd";

static void test_damage(void) {
  qa_armor_damage_flags f = qa_compat_q2_native_armor_flags(2 | 4 | 0x80 | 0x100);
  CHECK(f.no_armor && f.energy && f.no_regular_armor && f.no_power_armor);
  f = qa_compat_q2_native_armor_flags(1);
  CHECK(!f.no_armor && !f.energy && !f.no_regular_armor && !f.no_power_armor);
  qa_damage_request req; memset(&req, 0, sizeof req);
  req.attack.cause.kind = QA_ATTACK_CAUSE_ENVIRONMENT; req.delivery = "radius";
  qa_q2mp_native_cause_profile prof = {false, "base", false};
  qa_compat_q2_native_damage_arguments_result a = qa_compat_q2_native_damage_arguments(req, prof);
  CHECK(a.damage_flags == 1); /* radius only, no armor flags */
}

static void test_combat(void) {
  qa_compat_q2_native_combat_argument store[10];
  qa_native_call_abi abi32 = {"windows-i386", "pe32", 4, "cdecl"};
  qa_compat_q2_native_combat_call c = qa_compat_q2_stock_native_combat_call(QA_Q2CO_DAMAGE, true, abi32, store);
  CHECK(c.argument_count == 10 && strcmp(c.convention, "cdecl") == 0);
  qa_compat_q2_validate_native_combat_call(&c, QA_Q2CO_DAMAGE, abi32);
  qa_guest_value_layout params[10];
  qa_compat_q2_guest_call_signature sig = qa_compat_q2_native_combat_signature(&c, QA_Q2CO_DAMAGE, abi32, params);
  CHECK(sig.parameter_count == 10 && sig.result_is_void);
  CHECK(strcmp(params[9].storage, "int32") == 0); /* cause on i386 */
  qa_compat_q2_native_combat_argument store2[10];
  qa_native_call_abi abi64 = {"windows-x86-64", "pe32+", 8, "microsoft-x64"};
  qa_compat_q2_native_combat_call c8 = qa_compat_q2_stock_native_combat_call(QA_Q2CO_PAIN, true, abi64, store2);
  CHECK(c8.argument_count == 5 && strcmp(c8.convention, "microsoft-x64") == 0);
  CHECK_ABORTS(qa_compat_q2_validate_native_combat_call(&c, QA_Q2CO_DAMAGE, abi64));
  /* duplicate field aborts */
  store[1] = store[0];
  CHECK_ABORTS(qa_compat_q2_validate_native_combat_call(&c, QA_Q2CO_DAMAGE, abi32));
}

static void test_profiles(void) {
  const qa_compat_q2_native_primary_command_profile *cp = qa_compat_q2_get_native_primary_command_profile(XAT);
  CHECK(cp && cp->give_entry == 0x3140 && cp->items_count == 48 && strcmp(cp->ammo_kind, "name") == 0);
  cp = qa_compat_q2_get_native_primary_command_profile(RET);
  CHECK(cp && cp->items_count == 84 && !cp->has_client_ammo_index);
  CHECK(qa_compat_q2_get_native_primary_command_profile("sha256:0000") == NULL);
  const qa_compat_q2_native_primary_weapon_profile *wp = qa_compat_q2_get_native_primary_weapon_profile(RET);
  CHECK(wp && wp->decision_count == 11 && wp->client_byte_length == 7344);
  wp = qa_compat_q2_get_native_primary_weapon_profile(XAT);
  CHECK(wp && wp->decision_count == 7 && wp->active_count == 3);
  const qa_compat_q2_native_primary_inventory_profile *ip = qa_compat_q2_get_native_primary_inventory_profile(XAT);
  CHECK(ip && ip->count == 256 && ip->empty == -1);
  const qa_compat_q2_native_primary_player_profile *pp = qa_compat_q2_get_native_primary_player_profile(RET);
  CHECK(pp && pp->match.team_count == 2 && pp->has_forward);
  const qa_compat_q2_native_primary_drop_profile *dp = qa_compat_q2_get_native_primary_drop_profile(RET);
  CHECK(dp && dp->has_consumer && dp->named == 0x58410);
}

static void test_usercommand(void) {
  uint8_t bytes[16] = {9, 3, 0, 0x80, 0xff, 0x7f, 0, 0, 100, 0, 0xff, 0xff, 0, 0, 7, 2};
  qa_q2_user_command c = qa_compat_q2_read_classic_user_command(bytes);
  CHECK(c.milliseconds == 9 && c.buttons == 3 && c.angle_shorts.values[0] == -32768);
  CHECK(c.angle_shorts.values[1] == 32767 && c.forward_move == 100 && c.side_move == -1);
  qa_user_command u; memset(&u, 0, sizeof u); u.kind = QA_USER_COMMAND_Q2; u.as.q2 = c;
  uint8_t out[16]; qa_compat_q2_write_native_user_command(out, 16, u, NULL, NULL);
  CHECK(memcmp(out, bytes, 16) == 0);
  u.as.q2.forward_move = 40000;
  CHECK_ABORTS(qa_compat_q2_write_native_user_command(out, 16, u, NULL, NULL));
}

static void test_region(void) {
  qa_native_mod_protection_region r; memset(&r, 0, sizeof r);
  r.frame.entry = 0x100; r.entry = 0x110; r.join = 0x120; r.frame.exit = 0x130;
  r.frame.stack_bytes = 32; r.frame.argument_bytes = 8; r.result.kind = QA_NATIVE_MOD_LOCATION_STACK; r.result.offset = 40; r.result.storage = "int32";
  qa_compat_q2_validate_native_mod_region(&r, 8);
  r.join = r.entry;
  CHECK_ABORTS(qa_compat_q2_validate_native_mod_region(&r, 8));
}

static void test_reader_json(void) {
  const char *json = "{\"give\":{\"entry\":16,\"weapons\":32,\"ammo\":48,\"unknown\":{\"entry\":64,\"join\":80},\"ammoGrants\":[],\"argc\":96,\"argv\":112},\"drop\":{\"entry\":128,\"eligibility\":{\"entry\":144,\"join\":160}},\"client\":{\"pointer\":84,\"weapon\":176,\"ammoIndex\":null,\"inventory\":200},\"items\":{\"table\":1000,\"stride\":20,\"count\":3,\"classname\":0,\"flags\":4,\"weaponFlag\":1,\"ammunitionFlag\":2,\"icon\":8,\"ammo\":{\"kind\":\"index\",\"offset\":12}}}";
  qa_save_value *v = qa_parse_json_value((const uint8_t *)json, strlen(json), false);
  CHECK(v != NULL);
  qa_save_reader rd; CHECK(qa_save_reader_init(&rd, v, "test"));
  /* offset > PE space aborts via a crafted reader below */
  qa_save_reader give; qa_save_reader_field(&rd, "give", &give);
  qa_save_reader er; qa_save_reader_field(&give, "entry", &er);
  CHECK(qa_compat_q2_native_offset(&er) == 16); qa_save_reader_destroy(&er);
  qa_save_reader_destroy(&give); qa_save_reader_destroy(&rd);
  qa_save_value *v2 = qa_parse_json_value((const uint8_t *)json, strlen(json), false);
  qa_save_reader rd2; qa_save_reader_init(&rd2, v2, "test");
  qa_compat_q2_native_primary_command_profile p;
  qa_native_abi abi = {"windows-i386", "pe32", 4, "cdecl"};
  CHECK(qa_compat_q2_read_native_primary_commands(&rd2, XAT, abi, &p));
  CHECK(p.give_entry == 16 && p.items_table == 1000 && !p.has_client_ammo_index && strcmp(p.ammo_kind, "index") == 0);
  qa_compat_q2_native_primary_command_profile_free(&p);
  qa_save_reader_destroy(&rd2); qa_save_value_free(v); qa_save_value_free(v2);
}

/* Flat guest for the player service: entity at 0x1000, client at 0x2000. */
static uint8_t fake_bytes[0x4000];
static const void *fake_space = fake_bytes;
static qa_guest_address_result fm_pointer(void *ctx, int64_t raw) { (void)ctx; qa_guest_address_result r; r.has_value = raw != 0; r.value.address_space = fake_space; r.value.byte_offset = raw; return r; }
static qa_guest_address fm_offset(void *ctx, qa_guest_address a, int64_t d) { (void)ctx; a.byte_offset += d; return a; }
static void fm_check(void *ctx, qa_guest_address a, double n, const char *access) { (void)ctx; (void)access; CHECK(a.byte_offset >= 0 && a.byte_offset + (int64_t)n <= (int64_t)sizeof fake_bytes); }
static void fm_copy(void *ctx, qa_guest_address a, uint8_t *dest, double n) { (void)ctx; memcpy(dest, fake_bytes + a.byte_offset, (size_t)n); }
static void fm_write(void *ctx, qa_guest_address a, const uint8_t *bytes, size_t n) { (void)ctx; memcpy(fake_bytes + a.byte_offset, bytes, n); }
static bool fh_actor(void *ctx, qa_raw_entity_view record, qa_actor_id *out) { (void)ctx; (void)record; memset(out, 0, sizeof *out); return true; }
static bool fh_record_for(void *ctx, qa_actor_id actor, qa_raw_entity_view *out) { (void)ctx; (void)actor; memset(out, 0, sizeof *out); out->address = (qa_guest_address){fake_space, 0x1000}; return true; }
static void test_player(void) {
  const qa_compat_q2_native_primary_weapon_profile *wp = qa_compat_q2_get_native_primary_weapon_profile(RET);
  const qa_compat_q2_native_primary_player_profile *pp = qa_compat_q2_get_native_primary_player_profile(RET);
  qa_compat_q2_memory mem; memset(&mem, 0, sizeof mem);
  mem.module.digest = RET; mem.pointer_bytes = 8; mem.address_space = fake_space;
  mem.pointer = fm_pointer; mem.offset = fm_offset; mem.check = fm_check; mem.copy = fm_copy; mem.write = fm_write;
  qa_compat_q2_native_primary_weapon_host host; memset(&host, 0, sizeof host);
  host.memory = &mem; host.actor = fh_actor; host.record_for = fh_record_for;
  qa_compat_q2_mem_write_pointer(&mem, (qa_guest_address){fake_space, 0x1000 + (int64_t)wp->entity_client}, true, (qa_guest_address){fake_space, 0x2000});
  qa_compat_q2_native_primary_player pl;
  qa_compat_q2_native_primary_player_init(&pl, host, wp, pp);
  qa_actor_id actor; memset(&actor, 0, sizeof actor);
  qa_compat_q2_native_primary_player_set_score(&pl, actor, 42);
  CHECK(qa_compat_q2_native_primary_player_score(&pl, actor) == 42);
  CHECK_ABORTS(qa_compat_q2_native_primary_player_set_max_health(&pl, actor, 0));
}

int main(void) {
  test_damage(); test_combat(); test_profiles(); test_usercommand(); test_region(); test_reader_json(); test_player();
  if (failures) { fprintf(stderr, "%d/%d checks failed\n", failures, checks); return 1; }
  printf("all %d checks passed\n", checks); return 0;
}
