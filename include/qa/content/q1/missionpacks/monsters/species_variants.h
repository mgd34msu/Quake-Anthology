/*
 * Port of src/content/q1/missionpacks/monsters/species-variants.ts: Rogue
 * and Hipnotic changes to id1 species (knight, hknight, zombie, ogre,
 * tarbaby, fish), as PackMonsterDefinitions over the shared species.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_SPECIES_VARIANTS_H
#define QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_SPECIES_VARIANTS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q1/missionpacks/monsters/runtime.h"

/* TS speciesDefinition(spec): the shared species behavior as a definition
 * (baseBehavior, no frames or actions, pain/die defaults). The caller lays
 * a variant's members over *out (TS `...extra`). */
void qa_q1mpm_species_definition(const qa_q1_base_monster_species *spec, qa_q1mpm_pack_monster_definition *out);
/* TS hipnoticSpeciesVariants()[classname] laid over *out (a speciesDefinition);
 * false when the record has no entry for classname. */
bool qa_q1mpm_hipnotic_species_variant(const char *classname, qa_q1mpm_pack_monster_definition *out);
/* TS rogueSpeciesVariants(runtime): writes the five definitions (knight and
 * hell knight statues, zombie, ogre, tarbaby) to out and returns the count.
 * Frames and closure state live in runtime-owned storage. */
#define QA_Q1MPM_ROGUE_SPECIES_VARIANT_COUNT 5
size_t qa_q1mpm_rogue_species_variants(qa_q1mpm_runtime *runtime, qa_q1mpm_pack_monster_definition *out, size_t capacity);

#endif
