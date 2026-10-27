/*
 * Umbrella for src/content/q1/addons/index.ts. registerAddonMonsters
 * and the monsters re-exports belong to the monsters slice and are
 * absent until it lands. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q1_ADDONS_H
#define QA_CONTENT_Q1_ADDONS_H
#include "qa/content/q1/addons/base_triggers.h"
#include "qa/content/q1/addons/brushes.h"
#include "qa/content/q1/addons/campaign.h"
#include "qa/content/q1/addons/commands.h"
#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/addons/corpses.h"
#include "qa/content/q1/addons/ctf.h"
#include "qa/content/q1/addons/effects.h"
#include "qa/content/q1/addons/field_triggers.h"
#include "qa/content/q1/addons/horde.h"
#include "qa/content/q1/addons/items.h"
#include "qa/content/q1/addons/lights.h"
#include "qa/content/q1/addons/rope.h"
#include "qa/content/q1/addons/travel.h"
#include "qa/content/q1/addons/triggers.h"
/** Register before map spawning; call qa_q1ad_frame from the shared Q1 source frame phase. */
qa_q1ad_context *qa_q1ad_register_q1_campaign_addons(qa_q1ad_context *context, qa_q1ad_program program,
                                                  qa_q1ad_services services, qa_q1ad_game game);
#endif
