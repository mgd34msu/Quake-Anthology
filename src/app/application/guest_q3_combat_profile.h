#ifndef QA_APPLICATION_GUEST_Q3_COMBAT_PROFILE_H
#define QA_APPLICATION_GUEST_Q3_COMBAT_PROFILE_H

#include "qa/gameplay.h"
#include "qa/qvm.h"
#include "qa/native.h"
#include "qa/strings.h"

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
    int32_t words[QA_QVM_ARGUMENTS];
    qa_native_value_type types[QA_QVM_ARGUMENTS];
    size_t count;
} application_q3_combat_call;
typedef struct application_q3_reaction_call {
    uint32_t count, target, amount, attacker, inflictor, method;
    bool arguments_present;
} application_q3_reaction_call;
typedef struct application_q3_combat_mode { uint32_t offset; int32_t value; bool equal; } application_q3_combat_mode;
typedef struct application_q3_combat_tier { int32_t value; double protection; } application_q3_combat_tier;
typedef struct application_q3_combat_definition {
    uint32_t entity_stride, client_stride;
    struct { uint32_t inuse, health, takedamage, parent, client; } fields;
    struct {
        uint32_t allocate, free, damage;
        bool has_allocate, has_free, has_damage;
    } callbacks;
    application_q3_combat_call damage_call;
    struct {
        uint32_t flags, pain, die, enemy, world_slot;
        application_q3_reaction_call pain_call, die_call;
    } reactions;
    struct { uint32_t radius, no_armor, no_knockback, no_protection, no_team_protection; } damage_flags;
    bool has_damage_flags;
    struct {
        uint32_t health_stat, team_stat, notarget, invulnerable, no_knockback;
        enum { Q3_COMBAT_MASS_CONSTANT, Q3_COMBAT_MASS_INT32, Q3_COMBAT_MASS_FLOAT32 } mass_kind;
        union { double constant; uint32_t offset; } mass;
        const qa_team_source_value *teams;
        size_t team_count;
    } state;
    struct {
        uint32_t check, points_stat, tier_stat;
        application_q3_combat_call call;
        bool has_check;
        double protection, fallback;
        const application_q3_combat_mode *modes;
        size_t mode_count;
        const application_q3_combat_tier *tiers;
        size_t tier_count;
    } armor;
    uint32_t grapple_method, scratch;
    bool has_grapple_method;
} application_q3_combat_definition;
typedef struct application_q3_combat_profile application_q3_combat_profile;

/* primary belongs to this retained artifact. Callback addresses are QVM function
 * indices or native RVAs, qualified against that same retained artifact. */
bool application_q3_combat_profile_create(qa_qvm_image *, qa_native_module *, qa_qvm_role, qa_qvm_abi,
    const char *path, qa_bytes primary, qa_strings *, application_q3_combat_profile **, qa_error *);
void application_q3_combat_profile_destroy(application_q3_combat_profile *);
const application_q3_combat_definition *application_q3_combat_profile_definition(const application_q3_combat_profile *);
const qa_qvm_image *application_q3_combat_profile_image(const application_q3_combat_profile *);
const qa_native_module *application_q3_combat_profile_module(const application_q3_combat_profile *);
qa_qvm_abi application_q3_combat_profile_abi(const application_q3_combat_profile *);
const char *application_q3_combat_profile_path(const application_q3_combat_profile *);

#endif
