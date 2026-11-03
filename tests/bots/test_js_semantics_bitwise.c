/*
 * JS bitwise semantics in the rerelease bot knowledge (data/knowledge.ts): `&`
 * converts both operands with ToInt32. Expected values printed by bun from the
 * TS chooseWeapon / itemValue with the same inputs. GPL-2.0-or-later.
 */
#include <stdio.h>
#include <string.h>

#include "qa/bots/behavior/rerelease/data/knowledge.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

static bool chosen(double number, int64_t items) {
  qa_weapon_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.priority = 1;
  entry.number = number;
  entry.name = "w";
  entry.ammo_name = "";
  qa_bot_weapon_rec rec = {&entry, false, false, false};
  qa_bot_weapon_context ctx;
  memset(&ctx, 0, sizeof ctx);
  ctx.items = items;
  ctx.range = 100;
  ctx.allow_melee = true;
  qa_bot_weapon_pick pick;
  return qa_choose_weapon(&rec, 1, ctx, &pick);
}

static double mega_value(double mega_bit, int64_t spawnflags) {
  qa_item_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.name = "item_x";
  qa_bot_item_rec item;
  memset(&item, 0, sizeof item);
  item.entry = &entry;
  item.mega_spawnflag = mega_bit;
  qa_bot_item_context ctx;
  memset(&ctx, 0, sizeof ctx);
  ctx.spawnflags = spawnflags;
  ctx.health = 100;
  ctx.max_health = 100;
  ctx.allow_power_items = true;
  return qa_bot_item_value(&item, ctx);
}

int main(void) {
  /* bun: chooseWeapon([{number: n}], {items: i}) !== null */
  CHECK(!chosen(4294967296.0, 4294967296LL)); /* ToInt32(2^32) is 0: was chosen in int64 */
  CHECK(chosen(2147483648.0, 2147483648LL));
  CHECK(chosen(4294967297.0, 1));
  CHECK(chosen(8, 8 + 4294967296LL));
  /* bun: itemValue({megaSpawnflag: m}, {spawnflags: s}) */
  CHECK(mega_value(2147483648.0, 2147483648LL) == 0); /* (s & m) is -2^31, not m: was 700 */
  CHECK(mega_value(2, 2) == 700);
  CHECK(mega_value(4294967298.0, 2) == 0); /* was 0 in int64 too */
  CHECK(mega_value(2, 4294967298LL) == 700);
  printf("test_js_semantics_bitwise (bots): %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
