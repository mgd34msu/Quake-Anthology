#include "qa/q1_text.h"
#include "qa/campaign_q1.h"
#include "qa/arena.h"
#include "interpolation.h"
#include <string.h>

/* Classic triggers.qc/doors.qc/Rogue and client.qc authored text. GPL-2.0-or-later. */
typedef struct q1_text_entry { const char *key,*text; } q1_text_entry;
static const q1_text_entry messages[]={
    {"$qc_found_secret","You found a secret area!"},
    {"$qc_more_go","There are more to go..."},
    {"$qc_three_more","Only 3 more to go..."},
    {"$qc_two_more","Only 2 more to go..."},
    {"$qc_one_more","Only 1 more to go..."},
    {"$qc_sequence_completed","Sequence completed!"},
    {"$qc_need_gold_key","You need the gold key"},
    {"$qc_need_gold_runekey","You need the gold runekey"},
    {"$qc_need_gold_keycard","You need the gold keycard"},
    {"$qc_need_silver_key","You need the silver key"},
    {"$qc_need_silver_runekey","You need the silver runekey"},
    {"$qc_need_silver_keycard","You need the silver keycard"},
    {"$qc_already_have_rune","You already have a rune\n"},
    {"$qc_rune_resistance","Earth Magic\n\nRESISTANCE"},
    {"$qc_rune_strength","Black Magic\n\nSTRENGTH"},
    {"$qc_rune_haste","Hell Magic\n\nHASTE"},
    {"$qc_rune_regeneration","Edler Magic\n\nRegeneration"},
    {"$qc_color_games","You were told you can't change teams.\nGo play color games somewhere else.\n"},
    {"$qc_cannot_change_teams","You cannot change teams.\n"},
    {"$qc_ctf_disabled","Capture the Flag is not enabled.\n"},
    {"$qc_flag_missing","The flag is missing!\n"},
    {"$qc_flag_at_base","The flag is at base!\n"},
    {"$qc_flag_lying_about","The flag is lying about!\n"},
    {"$qc_you_have_flag","You have the flag!\n"},
    {"$qc_flag_screwed_up","The flag is screwed up!\n"},
    {"$qc_you_have_enemy_flag","You have the enemy flag.\n"},
    {"$qc_flag_returned","The flag has been returned!\n"},
    {"$qc_your_flag_returned_base","Your flag has been returned to base!\n"},
    {"$qc_enemy_flag_returned_base","Enemy flag has been returned to base!\n"},
    {"$qc_your_team_captured","Your team captured the flag!\n"},
    {"$qc_your_flag_captured","Your flag was captured!\n"},
    {"$qc_flag_taken","The flag has been taken!\n"},
    {"$qc_your_flag_taken","Your flag has been taken!\n"},
    {"$qc_enemy_killed_bonus","Enemy flag carrier killed: {0} bonus frags\n"},
    {"$qc_enemy_killed_no_bonus","Enemy flag carrier killed, no bonus\n"},
    {"$qc_has_token","{0} has the tag token!\n"},
    {"$qc_lost_token","{0} lost the tag token!\n"},
    {"$qc_got_token","{0} got the tag token!\n"},
    {"$qc_got_quad","You got the Quad Damage\n"}
};
static const q1_text_entry obituaries[]={
    {"$qc_telefragged","{0} was telefragged by {1}\n"},
    {"$qc_satans_power","Satan's power deflects {0}'s telefrag\n"},
    {"$qc_discharge_water","{0} discharges into the water.\n"},
    {"$qc_discharge_slime","{0} discharges into the water.\n"},
    {"$qc_discharge_lava","{0} discharges into the water.\n"},
    {"$qc_suicide_pin","{0} tries to put the pin back in\n"},
    {"$qc_suicide_bored","{0} becomes bored with life\n"},
    {"$qc_suicide_loaded","{0} becomes bored with life\n"},
    {"$qc_ff_teammate","{0} mows down a teammate\n"},
    {"$qc_ff_glasses","{0} checks his glasses\n"},
    {"$qc_ff_otherteam","{0} gets a frag for the other team\n"},
    {"$qc_ff_friend","{0} loses another friend\n"},
    {"$qc_death_ax","{0} was ax-murdered by {1}\n"},
    {"$qc_death_sg","{0} chewed on {1}'s boomstick\n"},
    {"$qc_death_dbl","{0} ate 2 loads of {1}'s buckshot\n"},
    {"$qc_death_nail","{0} was nailed by {1}\n"},
    {"$qc_death_sng","{0} was punctured by {1}\n"},
    {"$qc_death_gl1","{0} was gibbed by {1}'s grenade\n"},
    {"$qc_death_gl2","{0} eats {1}'s pineapple\n"},
    {"$qc_death_rl2","{0} was gibbed by {1}'s rocket\n"},
    {"$qc_death_rl3","{0} rides {1}'s rocket\n"},
    {"$qc_death_lg1","{0} accepts {1}'s discharge\n"},
    {"$qc_death_lg2","{0} accepts {1}'s shaft\n"},
    {"$qc_death_drown1","{0} sleeps with the fishes\n"},
    {"$qc_death_drown2","{0} sucks it down\n"},
    {"$qc_death_slime1","{0} gulped a load of slime\n"},
    {"$qc_death_slime2","{0} can't exist on slime alone\n"},
    {"$qc_death_lava1","{0} burst into flames\n"},
    {"$qc_death_lava2","{0} turned into hot slag\n"},
    {"$qc_death_lava3","{0} visits the Volcano God\n"},
    {"$qc_death_squish","{0} was squished\n"},
    {"$qc_death_fall","{0} fell to his death\n"},
    {"$qc_death_died","{0} died\n"}
};
static const char *lookup(const q1_text_entry *entries,size_t count,const char *key)
{
    for (size_t i=0;i<count;++i) if (!strcmp(entries[i].key,key)) return entries[i].text;
    return NULL;
}
static qa_bytes message_argument(void *context, const char *text,
                                 char storage[QA_INTERPOLATION_ARGUMENT_CAPACITY])
{
    (void)context;
    (void)storage;
    const char *mapped=lookup(messages,sizeof(messages)/sizeof(*messages),text);
    if (mapped) text=mapped;
    return (qa_bytes){(const uint8_t *)text,strlen(text)};
}
bool qa_q1_classic_text(const char *text,const char *const *arguments,size_t count,
    qa_arena *scratch,char *out,size_t capacity,qa_error *error)
{
    if (!text || (count && !arguments) || !out || !capacity) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Classic source text requires its actual argument tuple"); return false;
    }
    for (size_t i=0;i<count;++i) if (!arguments[i]) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Classic source text lost an argument"); return false;
    }
    const char *format=lookup(messages,sizeof(messages)/sizeof(*messages),text);
    qa_text_interpolation interpolation={.values=arguments,.value_count=count,
        .truncation=QA_INTERPOLATION_PREFIX_UTF8};
    if (!format) {
        format=lookup(obituaries,sizeof(obituaries)/sizeof(*obituaries),text);
        if (!format) format=text;
        size_t size;
        if (!qa_text_interpolate(format,&interpolation,NULL,0,&size,NULL,error)) return false;
        char *first=qa_arena_alloc(scratch,size+1,1,error);
        if (!first || !qa_text_interpolate(format,&interpolation,first,size+1,NULL,NULL,error))
            return false;
        format=qa_q1_finale_text(false,first);
    }
    interpolation.keep_missing=true;
    interpolation.resolve=message_argument;
    size_t size;
    return qa_text_interpolate(format,&interpolation,out,capacity,&size,NULL,error);
}
