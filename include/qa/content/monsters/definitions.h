/*
 * Port of src/content/monsters/definitions.ts: monster source
 * definitions (provider/edition/family/program plus a classname ->
 * resources record, represented as an ordered entry list), the
 * combined monsterSources table, monsterSource() lookup (aborts
 * with the TS RangeError message when unavailable), and
 * monsterTiming(). GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MONSTERS_DEFINITIONS_H
#define QA_CONTENT_MONSTERS_DEFINITIONS_H

#include <stddef.h>

#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"

/* TS Record<string, {resources}> entry. */
typedef struct qa_monster_creature_entry {
  const char *classname;
  const char *const *resources;
  size_t resource_count;
} qa_monster_creature_entry;

/* TS MonsterSourceDefinition: edition "classic"|"rerelease",
 * family "q1"(program "id1"|"hipnotic"|"rogue"|"dopa"|"mg1"|"mg3")
 * or "q2"(program "baseq2"|"xatrix"|"rogue"|"mg2"). */
typedef struct qa_monster_source_definition {
  qa_provider_id provider;
  const char *edition;
  const char *family;
  const char *program;
  const qa_monster_creature_entry *creatures;
  size_t creature_count;
} qa_monster_source_definition;

/* Lookup helper: entry for `classname`, or NULL when absent
 * (TS Object.hasOwn check). */
const qa_monster_creature_entry *qa_monster_source_creature(
    const qa_monster_source_definition *source, const char *classname);

/* TS monsterSources. */
extern const qa_monster_source_definition qa_monster_sources[];
extern const size_t qa_monster_source_count;

/* TS monsterSource: aborts (TS RangeError) when no source provides
 * the classname. */
const qa_monster_source_definition *qa_monster_source(
    qa_monster_definition_reference definition);

/* TS monsterTiming. */
qa_provider_timing qa_monster_timing(const qa_monster_source_definition *source);

#endif
