#ifndef QA_APPLICATION_GUEST_Q3_COMBAT_PROFILE_H
#define QA_APPLICATION_GUEST_Q3_COMBAT_PROFILE_H

#include "qa/gameplay.h"
#include "qa/qvm.h"
#include "qa/strings.h"

enum { APPLICATION_Q3_COMBAT_ARGUMENTS = 62 };
typedef enum application_q3_damage_word {
    Q3_DAMAGE_TARGET, Q3_DAMAGE_INFLICTOR, Q3_DAMAGE_ATTACKER,
    Q3_DAMAGE_DIRECTION, Q3_DAMAGE_POINT, Q3_DAMAGE_AMOUNT,
    Q3_DAMAGE_FLAGS, Q3_DAMAGE_METHOD, Q3_DAMAGE_WORDS
} application_q3_damage_word;
typedef enum application_q3_armor_word {
    Q3_ARMOR_TARGET, Q3_ARMOR_AMOUNT, Q3_ARMOR_FLAGS, Q3_ARMOR_WORDS
} application_q3_armor_word;
typedef struct application_q3_combat_call {
    uint32_t positions[Q3_DAMAGE_WORDS];
    int32_t words[APPLICATION_Q3_COMBAT_ARGUMENTS];
    size_t count;
} application_q3_combat_call;
typedef struct application_q3_reaction_call { uint32_t count, target, amount; } application_q3_reaction_call;
typedef struct application_q3_combat_team { int32_t value; qa_team_id team; } application_q3_combat_team;
typedef struct application_q3_combat_mode { uint32_t offset; int32_t value; bool equal; } application_q3_combat_mode;
typedef struct application_q3_combat_tier { int32_t value; float protection; } application_q3_combat_tier;
typedef struct application_q3_combat_definition {
    uint32_t entity_stride, client_stride;
    struct { uint32_t inuse, health, takedamage, parent, client; } fields;
    struct { uint32_t allocate, free, damage; } callbacks;
    application_q3_combat_call damage_call;
    struct {
        uint32_t flags, pain, die;
        application_q3_reaction_call pain_call, die_call;
    } reactions;
    struct { uint32_t radius, no_armor, no_knockback, no_protection, no_team_protection; } damage_flags;
    struct {
        uint32_t health_stat, team_stat, notarget, invulnerable, no_knockback;
        enum { Q3_COMBAT_MASS_CONSTANT, Q3_COMBAT_MASS_INT32, Q3_COMBAT_MASS_FLOAT32 } mass_kind;
        union { float constant; uint32_t offset; } mass;
        const application_q3_combat_team *teams;
        size_t team_count;
    } state;
    struct {
        uint32_t check, points_stat, tier_stat;
        application_q3_combat_call call;
        float protection, fallback;
        const application_q3_combat_mode *modes;
        size_t mode_count;
        const application_q3_combat_tier *tiers;
        size_t tier_count;
    } armor;
    uint32_t grapple_method, scratch;
} application_q3_combat_definition;
typedef struct application_q3_combat_profile application_q3_combat_profile;

/* primary is already matched to these held artifact bytes. Without a declared
 * primary, only the two exact source-authored executable cards are admitted. */
bool application_q3_combat_profile_create(qa_qvm_image *, qa_qvm_role, qa_qvm_abi,
    const char *path, qa_bytes primary, qa_strings *, application_q3_combat_profile **, qa_error *);
void application_q3_combat_profile_destroy(application_q3_combat_profile *);
const application_q3_combat_definition *application_q3_combat_profile_definition(const application_q3_combat_profile *);
const qa_qvm_image *application_q3_combat_profile_image(const application_q3_combat_profile *);
qa_qvm_abi application_q3_combat_profile_abi(const application_q3_combat_profile *);
const char *application_q3_combat_profile_path(const application_q3_combat_profile *);

#endif
