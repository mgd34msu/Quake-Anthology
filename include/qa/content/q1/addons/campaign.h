/* Port of src/content/q1/addons/campaign.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CAMPAIGN_H
#define QA_CONTENT_Q1_ADDONS_CAMPAIGN_H
#include "qa/content/q1/addons/context.h"
#define QA_Q1AD_MG1_ALL_SIGILS 31
#define QA_Q1AD_MG1_LAST_SIGIL_SHIFT 6
#define QA_Q1AD_MG3_RUNE_MASK 15
#define QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE 64
#define QA_Q1AD_BLOODY_NIGHTMARE_DISCOVERED 128
#define QA_Q1AD_BLOODY_NIGHTMARE_NEWGAME 256
double qa_q1ad_mg1_last_sigil(double flags);
double qa_q1ad_mg1_clear_last_sigil(double flags);
int qa_q1ad_mg3_rune_count(double flags);
void qa_q1ad_register_campaign_addons(qa_q1ad_context *context);
/* Intermission rule closures (base.levelRules.registerIntermissionRule). */
void qa_q1ad_campaign_intermission_touch(qa_q1ad_context *context, qa_q1ad_actor *trigger, qa_actor_id player);
void qa_q1ad_campaign_intermission_begin(qa_q1ad_context *context, const char *map, const qa_actor_id *cause);
bool qa_q1ad_campaign_finale(qa_q1ad_context *context, double stage, qa_q1ad_finale *out);
bool qa_q1ad_campaign_travel_rule(qa_q1ad_context *context, const char *map, const qa_actor_id *cause);
qa_q1ad_actor *qa_q1ad_campaign_spawn_selection(qa_q1ad_context *context);
#endif
