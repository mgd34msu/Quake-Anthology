/* C checks for TS tests/content/q1-monster-frame-tables.test.ts.
 * The TS test proves each committed frame table equals the table generated from the
 * shipped progs.dat. Here every C frame table (and the composed add-on source frame
 * maps) is compared entry for entry, in map order, with the TS tables as dumped by
 * tests/content/oracles/q1_frame_tables.ts into q1_frame_tables.txt.
 * GPL-2.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/base/frames.h"
#include "qa/content/q1/addons/monsters/demodog_frames.h"
#include "qa/content/q1/addons/monsters/infected/frames.h"
#include "qa/content/q1/addons/monsters/ordinary/source_frames.h"
#include "qa/content/q1/addons/monsters/bosses/frames/final.h"
#include "qa/content/q1/addons/monsters/bosses/frames/ghost.h"
#include "qa/content/q1/addons/monsters/bosses/frames/oldnew.h"
#include "qa/content/q1/addons/monsters/bosses/frames/orb.h"
#include "qa/content/q1/addons/monsters/bosses/frames/szombie.h"
#include "qa/content/q1/addons/monsters/heavy/tables/mg3_lavaman.h"
#include "qa/content/q1/addons/monsters/heavy/tables/mg3_rknight.h"
#include "qa/content/q1/addons/monsters/heavy/tables/mg3_super_shambler.h"
#include "qa/content/q1/missionpacks/monsters/tables/grunt.h"
#include "qa/content/q1/missionpacks/monsters/tables/rottweiler.h"
#include "qa/content/q1/missionpacks/monsters/tables/hiparma.h"
#include "qa/content/q1/missionpacks/monsters/tables/hipdecoy.h"
#include "qa/content/q1/missionpacks/monsters/tables/hipgrem.h"
#include "qa/content/q1/missionpacks/monsters/tables/hipscrge.h"
#include "qa/content/q1/missionpacks/monsters/tables/dragon.h"
#include "qa/content/q1/missionpacks/monsters/tables/eel.h"
#include "qa/content/q1/missionpacks/monsters/tables/invis_sw.h"
#include "qa/content/q1/missionpacks/monsters/tables/lavaman.h"
#include "qa/content/q1/missionpacks/monsters/tables/morph.h"
#include "qa/content/q1/missionpacks/monsters/tables/mummy.h"
#include "qa/content/q1/missionpacks/monsters/tables/s_wrath.h"
#include "qa/content/q1/missionpacks/monsters/tables/wrath.h"
#include "qa/network/common/js_number.h"

static int checks, failures;

static const struct { const char *name; const qa_q1_monster_frame_table *table; } TABLES[] = {
  {"monsterFrames", &QA_Q1_MONSTER_FRAMES},
  {"demodogFrames", &qa_q1am_demodog_frames},
  {"dogFrames", &qa_q1am_dog_frames},
  {"infectedFrames", &qa_q1am_infected_frames},
  {"hangFrames", &qa_q1am_ordinary_hang_frames},
  {"sourceFrames:mg1", &qa_q1am_ordinary_mg1_frames},
  {"sourceFrames:mg1:rocket", &qa_q1am_ordinary_mg1_rocket_frames},
  {"sourceFrames:mg3", &qa_q1am_ordinary_mg3_frames},
  {"sourceFrames:mg3:rocket", &qa_q1am_ordinary_mg3_rocket_frames},
  {"sourceFrames:other", &qa_q1am_ordinary_other_frames},
  {"sourceFrames:other:rocket", &qa_q1am_ordinary_other_rocket_frames},
  {"bosses/final", &qa_q1am_final_boss_frames},
  {"bosses/ghost", &qa_q1am_ghost_frames},
  {"bosses/oldnew", &qa_q1am_oldnew_frames},
  {"bosses/orb", &qa_q1am_orb_frames},
  {"bosses/szombie", &qa_q1am_szombie_frames},
  {"heavy/mg3_lavaman", &qa_q1am_mg3_lavaman_frames},
  {"heavy/mg3_rknight", &qa_q1am_mg3_rknight_frames},
  {"heavy/mg3_super_shambler", &qa_q1am_mg3_super_shambler_frames},
  {"mp/grunt", &qa_q1mpm_tables_grunt_frames},
  {"mp/rottweiler", &qa_q1mpm_tables_rottweiler_frames},
  {"mp/hiparma", &qa_q1mpm_tables_hiparma_frames},
  {"mp/hipdecoy", &qa_q1mpm_tables_hipdecoy_frames},
  {"mp/hipgrem", &qa_q1mpm_tables_hipgrem_frames},
  {"mp/hipscrge", &qa_q1mpm_tables_hipscrge_frames},
  {"mp/dragon", &qa_q1mpm_tables_dragon_frames},
  {"mp/eel", &qa_q1mpm_tables_eel_frames},
  {"mp/invis_sw", &qa_q1mpm_tables_invis_sw_frames},
  {"mp/lavaman", &qa_q1mpm_tables_lavaman_frames},
  {"mp/morph", &qa_q1mpm_tables_morph_frames},
  {"mp/mummy", &qa_q1mpm_tables_mummy_frames},
  {"mp/s_wrath", &qa_q1mpm_tables_s_wrath_frames},
  {"mp/wrath", &qa_q1mpm_tables_wrath_frames},
};

/* Appends one operation in the oracle's op() format. */
static void append_op(char *line, size_t capacity, const qa_q1_monster_operation *op) {
  size_t used = strlen(line);
  char a[40], b[40];
  switch (op->kind) {
    case QA_Q1_OP_AI:
      (void)snprintf(line + used, capacity - used, "ai:%s:%s", qa_q1_monster_ai_name(op->mode),
                     qa_net_js_number_text(op->distance, a));
      break;
    case QA_Q1_OP_SOUND:
      if (op->has_chance) qa_net_js_number(op->chance, b, sizeof b);
      else (void)snprintf(b, sizeof b, "null");
      (void)snprintf(line + used, capacity - used, "sound:%s:%s:%s:%s:%s", op->path, op->channel,
                     qa_net_js_number_text(op->attenuation, a), op->comparison, b);
      break;
    case QA_Q1_OP_SOLID: (void)snprintf(line + used, capacity - used, "solid:%s", op->solid); break;
    case QA_Q1_OP_LIGHTSTYLE: (void)snprintf(line + used, capacity - used, "lightstyle:%s", op->pattern); break;
    case QA_Q1_OP_ACTION: (void)snprintf(line + used, capacity - used, "action:%s", op->name); break;
  }
}

static char *render_entry(const qa_q1_monster_frame_entry *entry) {
  const size_t capacity = 4096;
  char *line = malloc(capacity);
  if (line == NULL) abort();
  char frame[40];
  (void)snprintf(line, capacity, "%s\t%s\t%s\t", entry->name, qa_net_js_number_text(entry->frame.frame, frame),
                 entry->frame.next);
  for (size_t i = 0; i < entry->frame.operation_count; i++) {
    if (i != 0) strncat(line, "|", capacity - strlen(line) - 1);
    append_op(line, capacity, &entry->frame.operations[i]);
  }
  return line;
}

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "tests/content/oracles/q1_frame_tables.txt";
  FILE *file = fopen(path, "r");
  if (file == NULL) {
    fprintf(stderr, "FAIL: cannot open %s (run from the repository root)\n", path);
    return 1;
  }
  char buffer[8192];
  const qa_q1_monster_frame_table *table = NULL;
  size_t index = 0, expected_count = 0, reported = 0;
  char table_name[128] = "";
  while (fgets(buffer, sizeof buffer, file) != NULL) {
    buffer[strcspn(buffer, "\n")] = '\0';
    if (strncmp(buffer, "table\t", 6) == 0) {
      if (table != NULL) {
        checks++;
        if (table->count != expected_count) {
          failures++;
          fprintf(stderr, "FAIL %s: %zu entries, TS has %zu\n", table_name, table->count, expected_count);
        }
      }
      char *name = buffer + 6, *tab = strchr(name, '\t');
      if (tab == NULL) abort();
      *tab = '\0';
      (void)snprintf(table_name, sizeof table_name, "%.120s", name);
      expected_count = (size_t)strtoul(tab + 1, NULL, 10);
      table = NULL;
      for (size_t i = 0; i < sizeof TABLES / sizeof TABLES[0]; i++)
        if (strcmp(TABLES[i].name, name) == 0) table = TABLES[i].table;
      checks++;
      if (table == NULL) {
        failures++;
        fprintf(stderr, "FAIL: no C table for %s\n", name);
      }
      index = 0;
      continue;
    }
    if (table == NULL) continue;
    checks++;
    if (index >= table->count) {
      failures++;
      if (reported++ < 40) fprintf(stderr, "FAIL %s[%zu]: C table ends; TS %s\n", table_name, index, buffer);
      index++;
      continue;
    }
    char *actual = render_entry(&table->entries[index]);
    if (strcmp(actual, buffer) != 0) {
      failures++;
      if (reported++ < 40) fprintf(stderr, "FAIL %s[%zu]:\n  C  %s\n  TS %s\n", table_name, index, actual, buffer);
    }
    free(actual);
    index++;
  }
  if (table != NULL) {
    checks++;
    if (table->count != expected_count) {
      failures++;
      fprintf(stderr, "FAIL %s: %zu entries, TS has %zu\n", table_name, table->count, expected_count);
    }
  }
  fclose(file);
  printf("test_q1_frame_tables: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
