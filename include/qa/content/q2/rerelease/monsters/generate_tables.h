/* Port of src/content/q2/rerelease/monsters/generate-tables.ts:
 * build-time generator reading the Quake II rerelease DLL monster
 * sources and writing tables TS files. Errors abort via qa_q2f_error
 * with the TS Error text; output goes to output_dir (the TS
 * tables directory next to the script).
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_MONSTERS_GENERATE_TABLES_H
#define QA_CONTENT_Q2_RERELEASE_MONSTERS_GENERATE_TABLES_H
#include <stddef.h>

/* TS generate(root, species): writes <output_dir>/<species>.ts and
 * prints "<species>: N original moves" to stdout. TS resolves the
 * output under the script's tables/ directory; creating it is the
 * caller's job to override via output_dir. */
void qa_q2rm_generate_monster_table(const char *root, const char *output_dir, const char *species);
/* TS script entry: QTS_Q2_RERELEASE_SOURCE from the environment,
 * species from argv[2..], tables written to output_dir, then
 * flashes.ts written from game.h. */
void qa_q2rm_generate_tables(const char *output_dir, int argc, char **argv);
#endif
