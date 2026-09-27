# B09 frozen source acceptance evidence

Snapshot HEAD: 9b1dfaef2d781f1246e244e039b97c01bdc8be1d. Includes dirty source frozen for audit. This packet contains explicit source selections, not a truncated full-source dump. Read scope and findings are recorded in docs/audit/q1-shared.md.

Goal and criteria:
{
  "id": "B09",
  "label": "Combat inventory and composition",
  "depends_on": [
    "B07"
  ],
  "goal": "Implement shared damage, inventory, pickups, capacities, effects, component operations, and attack provenance.",
  "criteria": [
    "Each mutation has one authority; ordered transforms, observers, replacements, and private state retain required behavior."
  ],
  "status": "pending"
}

Reviewer assessment:
Shared operation, armor and policy code is substantial, with generation-aware mutation authorities and leases. Native composition is incomplete: Q1 combat_context supplies only armor.alive, and the source search shows no native reserve/bind protection consumer. Q2 armor requires explicit source profile, so Q1 recipient with Q2 protection has no current attached owner route. Missing application composition is a concrete required integration gap; do not mistake the source-complete library owner's historical report for integrated behavior. Context and direct damage defects also affect Q1's use of shared services. No execution was performed or requested.

Snapshot selected SHA256 hashes:
6b1d901ba8b416faa382912b1b7296a276c7c4d9dbd8c1030e3960da4e5eaa5b  src/gameplay/q1/runtime.c
376d43df9d7812bd88873b2ecbe065902ee8bb24250f9e5bf37aa99a58d57d8d  src/gameplay/q1/queries.c
8c36feb2b149d7c98062f265351167d6c526a1f185a4813ecbb80ec1b73409b5  src/gameplay/q1/maps/runtime.c
eee0e959ad883ab18796a346de634b5d181333dbd3785688f365edaaa26dd17f  src/gameplay/policies.c
1a4be8c0b34459135d887f664dcb5ee5fe61f51d6793d132fb2f680032019517  src/gameplay/armor.c
33909f58875e89f0bfb43c1c851c26f0f3d87a086f169299dcc72777018d69cc  src/movement/common.c
c0b1c679af11906858a9e26211e7e3399cec31849fc29f1050c10f2af1272fc8  src/campaign/targets.c
1e48a455cf33dca65deefb3661ff1c8ae77c33f17b034302de64b63b97a874fa  src/campaign/q1/spawn.c
e5997203de73384986f6ffe9e3b6396651a6ba05cd09846cec5c8aee61c69f7f  src/main.c
915c4c50da8fcb60ed206f09f2d325eb7755f4c6f3f48b1c3d97d83d2eb98bae  docs/dependencies.json


## Source selection: cat src/gameplay/operation.c

```text
#include "qa/operation.h"

#include <stdlib.h>
#include <string.h>

typedef struct operation_entry {
    struct operation_entry *next;
    qa_operation_hook hook;
    uint64_t sequence;
    bool active;
} operation_entry;

typedef struct operation_frame {
    struct operation_frame *previous;
    void *request;
    qa_operation_canonical_fn canonical;
    void *context;
    uint64_t invocation;
    bool continuation_open, called, failed;
    qa_error failure;
} operation_frame;

struct qa_operation {
    operation_entry *entries;
    operation_frame *current, *spare;
    size_t request_size, result_size, active_count;
    uint64_t next_sequence, next_invocation;
    size_t depth;
};

static bool argument(qa_error *error, const char *message) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message); return false;
}

bool qa_operation_create(size_t request_size, size_t result_size, qa_operation **out, qa_error *error) {
    if (!out || !request_size) return argument(error,"operation requires a request layout and output");
    qa_operation *operation=calloc(1,sizeof(*operation));
    if (!operation) { qa_error_set(error,QA_ERROR_MEMORY,0,"allocating gameplay operation"); return false; }
    operation->request_size=request_size; operation->result_size=result_size;
    operation->next_sequence=1; operation->next_invocation=1;
    *out=operation; return true;
}

static void sweep(qa_operation *operation) {
    if (operation->depth) return;
    operation_entry **link=&operation->entries;
    while (*link) {
        operation_entry *entry=*link;
        if (!entry->active) { *link=entry->next; free(entry); }
        else link=&entry->next;
    }
}

bool qa_operation_destroy(qa_operation *operation, qa_error *error) {
    if (!operation) return true;
    if (operation->depth) return argument(error,"cannot destroy an operation during a callback");
    qa_operation_clear(operation);
    while (operation->spare) {
        operation_frame *frame=operation->spare;
        operation->spare=frame->previous;
        free(frame->request); free(frame);
    }
    free(operation); return true;
}

bool qa_operation_register(qa_operation *operation, const qa_operation_hook *hook,
                           qa_operation_registration *out, qa_error *error) {
    if (!operation || !hook || !out) return argument(error,"invalid operation registration");
    switch (hook->kind) {
    case QA_OPERATION_TRANSFORM: if (!hook->call.transform) return argument(error,"missing transform callback"); break;
    case QA_OPERATION_OBSERVE: if (!hook->call.observe) return argument(error,"missing observer callback"); break;
    case QA_OPERATION_REPLACE: if (!hook->call.replace) return argument(error,"missing replacement callback"); break;
    default: return argument(error,"invalid operation hook kind");
    }
    for (operation_entry *entry=operation->entries;entry;entry=entry->next) {
        if (!entry->active) continue;
        if (entry->hook.owner==hook->owner && entry->hook.name==hook->name)
            return argument(error,"duplicate operation registration");
        if (entry->hook.kind==QA_OPERATION_REPLACE && hook->kind==QA_OPERATION_REPLACE)
            return argument(error,"operation already has a replacement");
    }
    if (operation->next_sequence==UINT64_MAX) return argument(error,"operation registration identity exhausted");
    operation_entry *entry=malloc(sizeof(*entry));
    if (!entry) { qa_error_set(error,QA_ERROR_MEMORY,0,"allocating operation registration"); return false; }
    *entry=(operation_entry){.hook=*hook,.sequence=operation->next_sequence++,.active=true};
    operation_entry **link=&operation->entries;
    while (*link && ((*link)->hook.order<hook->order ||
           ((*link)->hook.order==hook->order && (*link)->sequence<entry->sequence))) link=&(*link)->next;
    entry->next=*link; *link=entry; ++operation->active_count;
    *out=entry->sequence; return true;
}

bool qa_operation_unregister(qa_operation *operation, qa_operation_registration registration) {
    if (!operation) return false;
    for (operation_entry *entry=operation->entries;entry;entry=entry->next) {
        if (entry->sequence!=registration || !entry->active) continue;
        entry->active=false; --operation->active_count; sweep(operation); return true;
    }
    return false;
}
void qa_operation_remove_owner(qa_operation *operation, qa_actor_owner owner) {
    if (!operation) return;
    for (operation_entry *entry=operation->entries;entry;entry=entry->next)
        if (entry->active && entry->hook.owner==owner) { entry->active=false; --operation->active_count; }
    sweep(operation);
}
void qa_operation_clear(qa_operation *operation) {
    if (!operation) return;
    for (operation_entry *entry=operation->entries;entry;entry=entry->next) entry->active=false;
    operation->active_count=0; sweep(operation);
}
bool qa_operation_active(const qa_operation *operation) { return operation && operation->active_count!=0; }

static bool continuation_failure(operation_frame *frame, qa_error *error, const char *message) {
    if (!frame->failed) {
        qa_error_set(&frame->failure,QA_ERROR_ARGUMENT,0,"%s",message); frame->failed=true;
    }
    if (error) *error=frame->failure;
    return false;
}
bool qa_operation_continue(qa_operation_next next, const void *request, void *result, qa_error *error) {
    if (!next.operation) return argument(error,"invalid operation continuation");
    operation_frame *frame=next.operation->current;
    while (frame && frame->invocation!=next.invocation) frame=frame->previous;
    if (!frame) return argument(error,"operation continuation is closed");
    if (!frame->continuation_open) return continuation_failure(frame,error,"operation continuation is closed");
    if (frame->called) return continuation_failure(frame,error,"operation continuation was already called");
    if (!request || (next.operation->result_size && !result)) return continuation_failure(frame,error,"invalid continuation request or result");
    frame->called=true;
    memmove(frame->request,request,next.operation->request_size);
    qa_error failure={0};
    if (!frame->canonical(frame->context,frame->request,result,&failure)) {
        if (!frame->failed) {
            frame->failure=failure;
            if (failure.code==QA_OK) qa_error_set(&frame->failure,QA_ERROR_ARGUMENT,0,"canonical gameplay operation failed");
            frame->failed=true;
        }
        if (error) *error=frame->failure;
        return false;
    }
    return true;
}

bool qa_operation_dispatch(qa_operation *operation, const void *request, void *result,
                           qa_operation_canonical_fn canonical, void *canonical_context,
                           qa_operation_committed_fn committed, void *committed_context, qa_error *error) {
    if (!operation || !request || !canonical || (operation->result_size && !result))
        return argument(error,"invalid gameplay operation dispatch");
    /* Hold lifetime during the direct path too: callbacks may register hooks or
     * try to retire their own owner, and destruction must remain forbidden. */
    if (!operation->active_count) {
        ++operation->depth;
        bool success=canonical(canonical_context,request,result,error);
        if (success && committed) success=committed(committed_context,error);
        --operation->depth; sweep(operation); return success;
    }
    if (operation->next_invocation==UINT64_MAX) return argument(error,"operation invocation identity exhausted");
    operation_frame *frame=operation->spare;
    if (frame) operation->spare=frame->previous;
    else {
        frame=calloc(1,sizeof(*frame));
        if (frame) frame->request=malloc(operation->request_size);
        if (!frame || !frame->request) {
            free(frame); qa_error_set(error,QA_ERROR_MEMORY,0,"allocating operation invocation"); return false;
        }
    }
    memcpy(frame->request,request,operation->request_size);
    frame->previous=operation->current; frame->canonical=canonical; frame->context=canonical_context;
    frame->invocation=operation->next_invocation++; frame->called=false; frame->failed=false; frame->continuation_open=false;
    operation->current=frame; ++operation->depth;
    uint64_t limit=operation->next_sequence;
    bool success=false;
    for (operation_entry *entry=operation->entries;entry;entry=entry->next) {
        if (entry->active && entry->sequence<limit && entry->hook.kind==QA_OPERATION_TRANSFORM &&
            !entry->hook.call.transform(entry->hook.context,frame->request,error)) goto finished;
    }
    operation_entry *replacement=NULL;
    for (operation_entry *entry=operation->entries;entry;entry=entry->next)
        if (entry->active && entry->sequence<limit && entry->hook.kind==QA_OPERATION_REPLACE) { replacement=entry; break; }
    if (replacement) {
        frame->continuation_open=true;
        success=replacement->hook.call.replace(replacement->hook.context,frame->request,
                    (qa_operation_next){operation,frame->invocation},result,error);
        frame->continuation_open=false;
        if (frame->failed) { if (error) *error=frame->failure; success=false; }
    } else success=canonical(canonical_context,frame->request,result,error);
    if (!success) goto finished;
    if (committed && !committed(committed_context,error)) { success=false; goto finished; }
    for (operation_entry *entry=operation->entries;entry;entry=entry->next) {
        if (entry->active && entry->sequence<limit && entry->hook.kind==QA_OPERATION_OBSERVE &&
            !entry->hook.call.observe(entry->hook.context,frame->request,result,error)) { success=false; break; }
    }
finished:
    operation->current=frame->previous;
    frame->previous=operation->spare; operation->spare=frame;
    --operation->depth; sweep(operation); return success;
}
```

## Source selection: cat src/gameplay/armor.c

```text
#include "qa/gameplay.h"

bool qa_regular_armor_equal(qa_regular_armor a, qa_regular_armor b) {
    if (a.kind != b.kind) return false;
    if (a.kind == QA_ARMOR_NONE) return true;
    if (a.points != b.points) return false;
    switch (a.kind) {
    case QA_ARMOR_Q1: return a.item == b.item && a.protection.q1_absorption == b.protection.q1_absorption;
    case QA_ARMOR_Q2: return a.item == b.item && a.protection.q2.normal == b.protection.q2.normal && a.protection.q2.energy == b.protection.q2.energy;
    case QA_ARMOR_Q3: return a.protection.q3_protection == b.protection.q3_protection;
    case QA_ARMOR_SOURCE: return a.item == b.item;
    case QA_ARMOR_NONE: return true;
    }
    return false;
}

bool qa_armor_equal(qa_armor a, qa_armor b) {
    return qa_regular_armor_equal(a.regular, b.regular) && a.powered.kind == b.powered.kind &&
        (a.powered.kind == QA_POWER_NONE || a.powered.cells == b.powered.cells);
}

bool qa_armor_validate(const qa_armor *armor, qa_error *error) {
    if (!armor || armor->regular.kind < QA_ARMOR_NONE || armor->regular.kind > QA_ARMOR_SOURCE ||
        armor->powered.kind < QA_POWER_NONE || armor->powered.kind > QA_POWER_SHIELD) goto invalid;
    if (armor->regular.kind != QA_ARMOR_NONE && !isfinite(armor->regular.points)) goto invalid;
    if (armor->powered.kind != QA_POWER_NONE && !isfinite(armor->powered.cells)) goto invalid;
    switch (armor->regular.kind) {
    case QA_ARMOR_Q1: if (!isfinite(armor->regular.protection.q1_absorption)) goto invalid; break;
    case QA_ARMOR_Q2: if (!isfinite(armor->regular.protection.q2.normal) || !isfinite(armor->regular.protection.q2.energy)) goto invalid; break;
    case QA_ARMOR_Q3: if (!isfinite(armor->regular.protection.q3_protection)) goto invalid; break;
    case QA_ARMOR_NONE: case QA_ARMOR_SOURCE: break;
    }
    return true;
invalid:
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid armor state"); return false;
}

qa_damage_flags qa_attack_flags(const qa_attack *attack) {
    qa_damage_flags flags = {.regular_scale = 1};
    if (!attack) return flags;
    uint32_t q2 = attack->cause.kind == QA_CAUSE_Q2 ? attack->cause.source.q2.flags : 0;
    uint32_t q3 = attack->cause.kind == QA_CAUSE_Q3 ? attack->cause.source.q3.flags : 0;
    flags.no_armor = ((q2 | q3) & 2u) != 0 || (attack->cause.kind == QA_CAUSE_Q1 && attack->cause.source.q1.armor == QA_Q1_ARMOR_BYPASS);
    flags.no_power_armor = (q2 & 0x100u) != 0;
    flags.no_regular_armor = (q2 & 0x80u) != 0;
    flags.energy = (q2 & 4u) != 0;
    if (attack->cause.kind == QA_CAUSE_Q1 && attack->cause.source.q1.armor == QA_Q1_ARMOR_HALF) flags.regular_scale = 0.5f;
    flags.no_knockback = (q2 & 8u) != 0 || (q3 & 4u) != 0;
    flags.no_protection = (q2 & 0x20u) != 0 || (q3 & 8u) != 0;
    flags.no_team_protection = (q3 & 0x10u) != 0;
    flags.destroy_armor = (q2 & 0x40u) != 0;
    return flags;
}

bool qa_armor_absorb(const qa_armor *armor, float damage, qa_damage_flags flags,
                     const qa_armor_context *context, const qa_protection_channel *stage,
                     qa_armor_result *out, qa_error *error) {
    if (!out || !context || !isfinite(damage) || !isfinite(context->screen_facing_dot) ||
        !isfinite(flags.regular_scale) || (stage && *stage != QA_PROTECTION_REGULAR && *stage != QA_PROTECTION_POWERED)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid armor stage"); return false;
    }
    if (!qa_armor_validate(armor, error)) return false;
    bool regular = !stage || *stage == QA_PROTECTION_REGULAR;
    bool power = !stage || *stage == QA_PROTECTION_POWERED;
    if (!context->q2_profile && ((regular && armor->regular.kind == QA_ARMOR_Q2) || (power && armor->powered.kind != QA_POWER_NONE))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 armor requires its source profile"); return false;
    }
    qa_armor_result result = {.armor = *armor};
    if (damage == 0 || flags.no_armor) { *out = result; return true; }
    qa_powered_armor *powered = &result.armor.powered;
    if (power && !flags.no_power_armor && (!context->rerelease || context->alive) &&
        powered->kind != QA_POWER_NONE && powered->cells > 0 &&
        (powered->kind != QA_POWER_SCREEN || context->screen_facing_dot > 0.3f)) {
        float damage_per_cell = powered->kind == QA_POWER_SCREEN || context->ctf ? 1.0f : 2.0f;
        float protected_damage = truncf(powered->kind == QA_POWER_SCREEN ? damage / 3 : 2 * damage / 3);
        bool doubled = context->rerelease ? flags.energy : flags.no_regular_armor;
        float available = powered->cells * damage_per_cell;
        if (doubled) available = truncf(available / 2);
        if (context->rerelease) { protected_damage = fmaxf(1, protected_damage); available = fmaxf(1, available); }
        result.power_saved = fminf(available, protected_damage);
        float used = truncf(result.power_saved / damage_per_cell) * (doubled ? 2 : 1);
        powered->cells = context->rerelease ? fmaxf(0, powered->cells - fmaxf(damage_per_cell, used)) : powered->cells - used;
    }
    qa_regular_armor *item = &result.armor.regular;
    if (regular && !flags.no_regular_armor && item->kind != QA_ARMOR_NONE) {
        float protection;
        switch (item->kind) {
        case QA_ARMOR_Q1: protection = item->protection.q1_absorption; break;
        case QA_ARMOR_Q2: protection = flags.energy ? item->protection.q2.energy : item->protection.q2.normal; break;
        case QA_ARMOR_Q3: protection = item->protection.q3_protection; break;
        case QA_ARMOR_SOURCE:
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "source armor requires an absorption owner"); return false;
        default: protection = 0; break;
        }
        result.regular_saved = fminf(item->points, ceilf(protection * flags.regular_scale * (damage - result.power_saved)));
        item->points -= result.regular_saved;
        if (item->kind == QA_ARMOR_Q1 && item->points <= 0) item->protection.q1_absorption = 0;
    }
    if (!isfinite(result.power_saved) || !isfinite(result.regular_saved) || !qa_armor_validate(&result.armor, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "armor arithmetic overflow"); return false;
    }
    *out = result;
    return true;
}
```

## Source selection: cat src/gameplay/policies.c

```text
#include "combat_internal.h"
#include <limits.h>

static bool self_damage(const qa_damage_request *request) {
    return request->attack.attacker.registry &&
           qa_actor_id_equal(request->target, request->attack.attacker);
}
static bool same_team(const qa_combat_state *target, const qa_combat_state *attacker) {
    return attacker && target->team && target->team == attacker->team;
}
static bool current(qa_combat *combat, const qa_damage_request *request, qa_combat_state *victim,
                    qa_combat_state *attacker, bool *has_attacker, qa_error *error) {
    if (!qa_combat_read(combat, request->target, victim, error))
        return false;
    *has_attacker = false;
    if (qa_combat_live(combat, request->attack.attacker)) {
        qa_error ignored = {0};
        if (qa_combat_read(combat, request->attack.attacker, attacker, &ignored))
            *has_attacker = true;
        else if (ignored.code != QA_ERROR_NOT_FOUND) {
            if (error)
                *error = ignored;
            return false;
        }
    }
    return true;
}
static bool describe(qa_combat *combat, const qa_combat_policy *policy,
                     const qa_damage_request *request, qa_combat_state *target,
                     qa_combat_state *attacker, bool *has_attacker, qa_combat_context *context,
                     qa_error *error) {
    if (!current(combat, request, target, attacker, has_attacker, error))
        return false;
    *context = (qa_combat_context){0};
    ++combat->active_calls;
    bool ok = policy->describe(policy->context, request, target, *has_attacker ? attacker : NULL,
                               context, error);
    --combat->active_calls;
    return ok;
}
static bool effect(qa_combat *combat, const qa_combat_policy *policy, qa_damage_effect_stage stage,
                   const qa_damage_request *request, qa_damage_effect *value, qa_error *error) {
    if (policy->effect) {
        ++combat->active_calls;
        bool ok = policy->effect(policy->context, combat, stage, request, value, error);
        --combat->active_calls;
        if (!ok)
            return false;
    }
    if (combat->hooks.effect && qa_combat_live(combat, request->target)) {
        ++combat->active_calls;
        bool ok = combat->hooks.effect(combat->hooks.context, combat, stage, request, value, error);
        --combat->active_calls;
        if (!ok)
            return false;
    }
    if (!isfinite(value->amount) || value->reaction < QA_REACTION_NONE ||
        value->reaction > QA_REACTION_DEATH)
        return qa_combat_argument(error, "source damage effect returned an invalid result");
    return true;
}
static bool integer(float value, int32_t *out, qa_error *error) {
    double truncated = trunc((double)value);
    if (truncated < INT32_MIN || truncated > INT32_MAX || !isfinite(truncated))
        return qa_combat_argument(error, "source damage exceeds its signed integer representation");
    *out = (int32_t)truncated;
    return true;
}
static int32_t signed_word(uint32_t value) {
    return value <= INT32_MAX ? (int32_t)value
                              : (int32_t)(value - UINT32_C(2147483648)) + INT32_MIN;
}
static int32_t multiply_integer(int32_t a, int32_t b) {
    return signed_word((uint32_t)a * (uint32_t)b);
}
/* Q2/Q3 weapons already apply their source power multiplier before damage.
 * They still expose the same ordered attachment boundaries as native Q1. */
static bool weapon_damage(qa_combat *combat, const qa_combat_policy *policy,
                          const qa_damage_request *request, qa_damage_effect *value,
                          qa_error *error) {
    *value = (qa_damage_effect){.amount = request->amount, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_BEFORE_QUAD, request, value, error))
        return false;
    if (!value->allowed || !qa_combat_live(combat, request->target)) {
        value->allowed = false;
        return true;
    }
    if (!effect(combat, policy, QA_DAMAGE_AFTER_QUAD, request, value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        value->allowed = false;
    return true;
}
static bool absorb(qa_combat *combat, const qa_combat_policy *policy,
                   const qa_damage_request *request, qa_protection_channel channel, float amount,
                   qa_damage_flags flags, float *saved, qa_error *error) {
    qa_combat_state target, attacker;
    qa_combat_context context;
    bool has_attacker;
    if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
        return false;
    if (!qa_combat_live(combat, request->target)) {
        *saved = 0;
        return true;
    }
    return qa_combat_absorb(combat, request, channel, NULL, amount, flags, &context.armor, saved,
                            error);
}

static bool q1_damage(qa_combat *combat, const qa_combat_policy *policy,
                      const qa_damage_request *request, qa_damage_result *result, qa_error *error) {
    qa_combat_state target, attacker;
    qa_combat_context context;
    bool has_attacker;
    if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
        return false;
    if (!qa_combat_live(combat, request->target) || !target.can_take_damage)
        return true;
    qa_damage_effect value = {.amount = request->amount, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_BEFORE_QUAD, request, &value, error))
        return false;
    if (!value.allowed || !qa_combat_live(combat, request->target))
        return true;
    if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    if (context.game.q1.quad && !request->attack.powerup_applied)
        value.amount *= 4;
    if (!effect(combat, policy, QA_DAMAGE_AFTER_QUAD, request, &value, error))
        return false;
    if (!value.allowed || !qa_combat_live(combat, request->target))
        return true;
    float damage = value.amount;
    if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
        return false;
    if (!qa_combat_live(combat, request->target) || !target.can_take_damage)
        return true;
    qa_q1_combat_context source = context.game.q1;
    value = (qa_damage_effect){.amount = damage, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_ARMOR_ALLOWED, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    bool armor_allowed = value.allowed;
    float power = 0, regular = 0;
    qa_damage_flags flags = qa_attack_flags(&request->attack);
    value = (qa_damage_effect){.amount = damage, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_POWER_ALLOWED, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    if (armor_allowed && value.allowed &&
        !absorb(combat, policy, request, QA_PROTECTION_POWERED, damage, flags, &power, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    value = (qa_damage_effect){.amount = damage - power, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_AFTER_POWER, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    float after_power = value.amount;
    if (armor_allowed && !absorb(combat, policy, request, QA_PROTECTION_REGULAR, after_power, flags,
                                 &regular, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    float take = ceilf(after_power - regular);
    if (!current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    if (source.walk && !target.no_knockback && source.has_momentum_direction &&
        !qa_combat_impulse(combat, request, source.momentum_direction, damage * 8, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    if (!current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    bool protected_health = target.invulnerable;
    if (protected_health) {
        value = (qa_damage_effect){.amount = damage, .allowed = true};
        if (!effect(combat, policy, QA_DAMAGE_PROTECTION_APPLIES, request, &value, error))
            return false;
        protected_health = value.allowed;
    }
    if (!qa_combat_live(combat, request->target))
        return true;
    if (!current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    if (protected_health || (!source.skip_base_team_health && source.teamplay == 1 &&
                             same_team(&target, has_attacker ? &attacker : NULL)))
        return true;
    value = (qa_damage_effect){.amount = damage, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_BEFORE_HEALTH, request, &value, error))
        return false;
    if (!value.allowed || !qa_combat_live(combat, request->target))
        return true;
    value = (qa_damage_effect){.amount = take, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_AFTER_ARMOR, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    take = value.amount;
    if (!current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    float health = fmaxf(-99, target.health - take);
    qa_reaction reaction = health <= 0 ? QA_REACTION_DEATH : QA_REACTION_PAIN;
    if (health <= 0) {
        value =
            (qa_damage_effect){.amount = health, .allowed = true, .reaction = QA_REACTION_DEATH};
        if (!effect(combat, policy, QA_DAMAGE_LETHAL_HEALTH, request, &value, error))
            return false;
        if (!qa_combat_live(combat, request->target))
            return true;
        health = value.amount;
        reaction = value.reaction;
    }
    if (!isfinite(health) || !isfinite(take))
        return qa_combat_argument(error, "Q1 damage arithmetic overflow");
    if (!qa_combat_set_health(combat, request->target, health, error))
        return false;
    *result = (qa_damage_result){.applied_damage = take, .reaction = reaction};
    return true;
}

static bool q2_damage(qa_combat *combat, const qa_combat_policy *policy,
                      const qa_damage_request *request, qa_damage_result *result, qa_error *error) {
    qa_combat_state target, attacker;
    qa_combat_context context;
    bool has_attacker;
    if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
        return false;
    if (!qa_combat_live(combat, request->target) || !target.can_take_damage)
        return true;
    qa_q2_combat_context source = context.game.q2;
    qa_damage_flags flags = qa_attack_flags(&request->attack);
    int32_t damage;
    qa_damage_effect input;
    if (!weapon_damage(combat, policy, request, &input, error))
        return false;
    if (!input.allowed)
        return true;
    if (policy->effect || combat->hooks.effect) {
        if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
            return false;
        if (!qa_combat_live(combat, request->target) || !target.can_take_damage)
            return true;
        source = context.game.q2;
    }
    if (!integer(input.amount, &damage, error))
        return false;
    if (!self_damage(request) && source.team_damage_enabled &&
        same_team(&target, has_attacker ? &attacker : NULL) && !source.friendly_fire &&
        !source.nuke)
        damage = 0;
    if (source.easy_skill && !source.deathmatch && source.player) {
        damage /= 2;
        if (damage < 1)
            damage = 1;
    }
    if (source.defender_sphere && source.player) {
        damage /= 2;
        if (damage < 1)
            damage = 1;
    }
    if (!request->radius && source.monster && source.attacker_player && !source.has_enemy &&
        target.health > 0)
        damage = multiply_integer(damage, 2);
    qa_damage_effect value = {.amount = (float)damage, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_BEFORE_MOMENTUM, request, &value, error) ||
        !integer(value.amount, &damage, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    if (!current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    int32_t knockback = 0;
    if (!source.no_knockback && !target.no_knockback &&
        !integer(request->knockback, &knockback, error))
        return false;
    if (!flags.no_knockback && source.movable) {
        float coefficient = source.player && self_damage(request) ? 1600 : 500;
        if (!qa_combat_impulse(combat, request, request->direction,
                               coefficient * (float)knockback / fmaxf(50, target.mass), error))
            return false;
    }
    if (!qa_combat_live(combat, request->target))
        return true;
    if (!current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    bool protected_health = target.invulnerable && !flags.no_protection;
    bool protection_bypassed = false;
    if (protected_health) {
        value = (qa_damage_effect){.amount = (float)damage, .allowed = true};
        if (!effect(combat, policy, QA_DAMAGE_PROTECTION_APPLIES, request, &value, error))
            return false;
        if (!qa_combat_live(combat, request->target))
            return true;
        protected_health = value.allowed;
        protection_bypassed = !value.allowed;
    }
    float protection_saved = protected_health ? (float)damage : 0;
    float amount = (float)damage - protection_saved, power = 0, regular = 0;
    value = (qa_damage_effect){.amount = amount, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_POWER_ALLOWED, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    if (value.allowed &&
        !absorb(combat, policy, request, QA_PROTECTION_POWERED, amount, flags, &power, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    value = (qa_damage_effect){.amount = amount - power, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_AFTER_POWER, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    int32_t after_power;
    if (!integer(value.amount, &after_power, error))
        return false;
    value = (qa_damage_effect){.amount = (float)after_power, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_ARMOR_ALLOWED, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    if (value.allowed && !absorb(combat, policy, request, QA_PROTECTION_REGULAR, (float)after_power,
                                 flags, &regular, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    value = (qa_damage_effect){.amount = (float)after_power - regular, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_AFTER_ARMOR, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    int32_t take;
    if (!integer(value.amount, &take, error) ||
        !current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    if (!flags.no_protection && source.reject_team_damage)
        return true;
    if (flags.destroy_armor && (!target.invulnerable || protection_bypassed) &&
        !flags.no_protection)
        take = damage;
    *result = (qa_damage_result){.has_feedback = true,
                                 .feedback_family = QA_GAME_Q2,
                                 .power_saved = power,
                                 .armor_saved = regular + protection_saved,
                                 .blood = (float)take,
                                 .knockback = (float)knockback};
    if (!protected_health) {
        value = (qa_damage_effect){.amount = (float)damage, .allowed = true};
        if (!effect(combat, policy, QA_DAMAGE_BEFORE_HEALTH, request, &value, error))
            return false;
        if (!qa_combat_live(combat, request->target)) {
            *result = (qa_damage_result){0};
            return true;
        }
        if (!value.allowed) {
            result->blood = 0;
            return true;
        }
        if (!current(combat, request, &target, &attacker, &has_attacker, error))
            return false;
    }
    if (!take)
        return true;
    float health = fmaxf(-999, truncf(target.health - (float)take));
    if (!qa_combat_set_health(combat, request->target, health, error))
        return false;
    result->applied_damage = (float)take;
    result->reaction = health <= 0            ? QA_REACTION_DEATH
                       : source.suppress_pain ? QA_REACTION_NONE
                                              : QA_REACTION_PAIN;
    if ((policy->effect || combat->hooks.effect) && qa_combat_live(combat, request->target)) {
        value = (qa_damage_effect){
            .amount = result->applied_damage, .allowed = true, .reaction = result->reaction};
        if (!effect(combat, policy, QA_DAMAGE_AFTER_HEALTH, request, &value, error))
            return false;
        if (!qa_combat_live(combat, request->target)) {
            result->reaction = QA_REACTION_NONE;
            return true;
        }
        if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
            return false;
        result->reaction = !qa_combat_live(combat, request->target) ? QA_REACTION_NONE
                           : target.health <= 0                     ? QA_REACTION_DEATH
                           : context.game.q2.suppress_pain          ? QA_REACTION_NONE
                                                                    : QA_REACTION_PAIN;
    }
    return true;
}

static bool q3_damage(qa_combat *combat, const qa_combat_policy *policy,
                      const qa_damage_request *request, qa_damage_result *result, qa_error *error) {
    qa_combat_state target, attacker;
    qa_combat_context context;
    bool has_attacker;
    if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
        return false;
    if (!qa_combat_live(combat, request->target) || !target.can_take_damage)
        return true;
    qa_q3_combat_context source = context.game.q3;
    if (source.intermission || source.noclip ||
        (source.missionpack_invulnerability && !source.juiced))
        return true;
    qa_damage_flags flags = qa_attack_flags(&request->attack);
    int32_t damage;
    qa_damage_effect input;
    if (!weapon_damage(combat, policy, request, &input, error))
        return false;
    if (!input.allowed)
        return true;
    if (policy->effect || combat->hooks.effect) {
        if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
            return false;
        if (!qa_combat_live(combat, request->target) || !target.can_take_damage)
            return true;
        source = context.game.q3;
        if (source.intermission || source.noclip ||
            (source.missionpack_invulnerability && !source.juiced))
            return true;
    }
    if (!isfinite(source.knockback_scale))
        return qa_combat_argument(error, "invalid Q3 knockback scale");
    if (!integer(input.amount, &damage, error))
        return false;
    if (source.attacker_player && !self_damage(request)) {
        int32_t maximum =
            source.attacker_guard ? source.attacker_max_health / 2 : source.attacker_max_health;
        damage = multiply_integer(damage, maximum) / 100;
    }
    int32_t knockback = source.no_knockback || target.no_knockback || flags.no_knockback ? 0
                        : damage < 200                                                   ? damage
                                                                                         : 200;
    *result = (qa_damage_result){
        .has_feedback = true, .feedback_family = QA_GAME_Q3, .knockback = (float)knockback};
    if (source.player && !source.no_knockback && !target.no_knockback && !flags.no_knockback &&
        !qa_combat_impulse(combat, request, request->direction,
                           source.knockback_scale * (float)knockback / 200, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    if (!current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    if (!flags.no_protection) {
        bool check_team = !source.missionpack || (!source.juiced && !flags.no_team_protection);
        if ((check_team && !self_damage(request) &&
             same_team(&target, has_attacker ? &attacker : NULL) && !source.friendly_fire) ||
            source.proximity_protected)
            return true;
        if (target.invulnerable) {
            qa_damage_effect protection = {.amount = (float)damage, .allowed = true};
            if (!effect(combat, policy, QA_DAMAGE_PROTECTION_APPLIES, request, &protection, error))
                return false;
            if (!qa_combat_live(combat, request->target)) {
                *result = (qa_damage_result){0};
                return true;
            }
            if (protection.allowed)
                return true;
        }
    }
    if (source.battlesuit) {
        result->battlesuit = true;
        if (request->radius || source.falling)
            return true;
        damage /= 2;
    }
    if (self_damage(request))
        damage /= 2;
    if (damage < 1)
        damage = 1;
    float power = 0, regular = 0;
    qa_damage_effect value = {.amount = (float)damage, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_POWER_ALLOWED, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target)) {
        *result = (qa_damage_result){0};
        return true;
    }
    if (value.allowed && !absorb(combat, policy, request, QA_PROTECTION_POWERED, (float)damage,
                                 flags, &power, error))
        return false;
    if (!qa_combat_live(combat, request->target)) {
        *result = (qa_damage_result){0};
        return true;
    }
    value = (qa_damage_effect){.amount = (float)damage - power, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_AFTER_POWER, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target)) {
        *result = (qa_damage_result){0};
        return true;
    }
    float after_power = value.amount;
    value = (qa_damage_effect){.amount = after_power, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_ARMOR_ALLOWED, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target)) {
        *result = (qa_damage_result){0};
        return true;
    }
    if (value.allowed && !absorb(combat, policy, request, QA_PROTECTION_REGULAR, after_power, flags,
                                 &regular, error))
        return false;
    if (!qa_combat_live(combat, request->target)) {
        *result = (qa_damage_result){0};
        return true;
    }
    value = (qa_damage_effect){.amount = after_power - regular, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_AFTER_ARMOR, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target)) {
        *result = (qa_damage_result){0};
        return true;
    }
    int32_t take;
    if (!integer(value.amount, &take, error))
        return false;
    result->power_saved = power;
    result->armor_saved = regular;
    result->blood = (float)take;
    value = (qa_damage_effect){.amount = (float)damage, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_BEFORE_HEALTH, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target)) {
        *result = (qa_damage_result){0};
        return true;
    }
    if (!value.allowed) {
        result->blood = 0;
        return true;
    }
    if (!take)
        return true;
    if (!current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    int32_t previous_health;
    if (!integer(target.health, &previous_health, error))
        return false;
    int32_t health = signed_word((uint32_t)previous_health - (uint32_t)take);
    if (health < -999)
        health = -999;
    if (!qa_combat_set_health(combat, request->target, (float)health, error))
        return false;
    result->applied_damage = (float)take;
    result->reaction = health <= 0 ? QA_REACTION_DEATH : QA_REACTION_PAIN;
    return true;
}
bool qa_combat_policy_execute(qa_combat *combat, const qa_combat_policy *policy,
                              const qa_damage_request *request, qa_damage_result *result,
                              qa_error *error) {
    *result = (qa_damage_result){0};
    switch (policy->family) {
    case QA_GAME_Q1:
        return q1_damage(combat, policy, request, result, error);
    case QA_GAME_Q2:
        return q2_damage(combat, policy, request, result, error);
    case QA_GAME_Q3:
        return q3_damage(combat, policy, request, result, error);
    }
    return qa_combat_argument(error, "unknown combat family");
}
```

## Source selection: sed -n '180,245p' src/gameplay/q1/runtime.c

```text
                              .code = code,
                              .time_ns = g->time_ns};
    return qa_builtin_emit(&g->services, &event, error);
}

static bool combat_context(void *context, const qa_damage_request *request,
                           const qa_combat_state *target, const qa_combat_state *attacker,
                           qa_combat_context *out, qa_error *error) {
    (void)attacker;
    (void)error;
    qa_q1_game *g = context;
    q1_player *player = q1_player_get(g, request->attack.attacker);
    qa_physics_properties physics;
    bool has_physics = g->services.physics && g->services.physics->services.read &&
                       g->services.physics->services.read(g->services.physics->services.context,
                                                          request->target, &physics);
    *out = (qa_combat_context){
        .armor.alive = target->health > 0,
        .game.q1 = {.quad = player && player->power_expires[QA_Q1_QUAD] > g->time,
                    .walk = has_physics && (physics.motion == QA_PHYSICS_STEP ||
                                            (physics.flags & QA_PHYSICS_PLAYER)),
                    .teamplay = g->options.teamplay}};
    return true;
}
static bool begin_frame(void *context, qa_session *session, const qa_source_frame *frame,
                        qa_error *error) {
    (void)session;
    (void)error;
    qa_q1_game *g = context;
    q1_map_frame_begin(g);
    while (g->retired_actors) {
        q1_actor *entity = g->retired_actors;
        g->retired_actors = entity->pool_next;
        entity->pool_next = g->spare_actors;
        g->spare_actors = entity;
    }
    while (g->retired_players) {
        q1_player *player = g->retired_players;
        g->retired_players = player->pool_next;
        player->pool_next = g->spare_players;
        g->spare_players = player;
    }
    g->time_ns = frame->time_ns;
    g->time = (double)frame->time_ns / 1000000000.0;
    g->elapsed = (double)frame->elapsed_ns / 1000000000.0;
    return true;
}
static bool actor_frame(void *context, qa_session *session, qa_actor_id actor,
                        const qa_source_frame *frame, qa_error *error) {
    (void)session;
    qa_q1_game *g = context;
    q1_actor *entity = q1_entity(g, actor);
    if (!entity || !entity->native || !g->services.physics)
        return true;
    if (entity->kind == Q1_PROJECTILE) {
        bool changed;
        if (!qa_builtin_step_projectile(&g->services, actor, frame->time_ns, &changed, error))
            return false;
        if (!q1_alive(g, actor))
            return true;
        if (changed) {
            if (entity->state.projectile.kind == Q1_HIP_LASER) {
                qa_body_state body;
                if (!qa_world_body_read(g->services.world, actor, &body, error))
                    return false;
                entity->state.projectile.movedir = body.velocity;
```

## Source selection: rg -n 'qa_combat_(reserve_protection|bind_protection)|qa_q1_game_create|qa_q1_level_create|qa_q1_spawn_select' src

```text
src/campaign/q1/level.c:38:qa_q1_level *qa_q1_level_create(const qa_q1_level_options *options, qa_error *error) {
src/campaign/q1/spawn.c:5:struct qa_q1_spawn_selector {
src/campaign/q1/spawn.c:16:static bool live(const qa_q1_spawn_selector *selector, qa_actor_id actor) {
src/campaign/q1/spawn.c:19:qa_q1_spawn_selector *qa_q1_spawn_selector_create(const qa_q1_spawn_options *options,
src/campaign/q1/spawn.c:41:    qa_q1_spawn_selector *selector = calloc(1, sizeof(*selector));
src/campaign/q1/spawn.c:60:void qa_q1_spawn_selector_destroy(qa_q1_spawn_selector *selector) {
src/campaign/q1/spawn.c:68:qa_actor_id qa_q1_spawn_last(const qa_q1_spawn_selector *selector) { return selector->last; }
src/campaign/q1/spawn.c:69:bool qa_q1_spawn_restore_last(qa_q1_spawn_selector *selector, qa_actor_id last, qa_error *error) {
src/campaign/q1/spawn.c:75:static bool player_body(qa_q1_spawn_selector *selector, qa_actor_id actor, bool living,
src/campaign/q1/spawn.c:104:static bool nearby(qa_q1_spawn_selector *selector, qa_actor_id point, float radius, bool living,
src/campaign/q1/spawn.c:126:static bool visible(qa_q1_spawn_selector *selector, qa_actor_id point, bool *out, qa_error *error) {
src/campaign/q1/spawn.c:155:static qa_actor_id first(const qa_q1_spawn_selector *selector, const qa_q1_spawn_point *points,
src/campaign/q1/spawn.c:162:static bool random_choice(qa_q1_spawn_selector *selector, qa_actor_id *out, qa_error *error) {
src/campaign/q1/spawn.c:172:static bool select_point(qa_q1_spawn_selector *selector, const qa_q1_spawn_point *points,
src/campaign/q1/spawn.c:285:bool qa_q1_spawn_select(qa_q1_spawn_selector *selector, const qa_q1_spawn_point *points,
src/gameplay/combat.c:459:bool qa_combat_reserve_protection(qa_combat *combat, qa_actor_id actor, qa_protection_channel channel,
src/gameplay/combat.c:495:bool qa_combat_bind_protection(qa_combat *combat, qa_protection_lease lease, const qa_protection_binding *binding, qa_error *error) {
src/gameplay/q1/runtime.c:262:bool qa_q1_game_create(const qa_builtin_services *services, const qa_q1_options *options,
```

## Source selection: sed -n '120,290p' src/gameplay/combat.c

```text
    if (entry->power_inventory && state.armor.powered.kind != QA_POWER_NONE) {
        qa_inventory_entry fuel;
        if (!fuel_entry(combat, entry, &fuel, error)) return false;
        state.armor.powered.cells = (float)fuel.count;
    }
    if (!state_valid(&state, error)) return false;
    *out = state; *local = !entry->external; return true;
}
bool qa_combat_create(qa_actor_registry *actors, const qa_combat_hooks *hooks, qa_combat **out, qa_error *error) {
    if (!actors || !out) return qa_combat_argument(error, "combat requires an actor registry and output");
    size_t count = qa_actors_capacity(actors);
    if (count > SIZE_MAX / sizeof(qa_combat_record)) return memory_error(error);
    qa_combat *combat = calloc(1, sizeof(*combat));
    if (!combat) return memory_error(error);
    combat->records = calloc(count, sizeof(*combat->records));
    if (!combat->records || !qa_operation_create(sizeof(qa_damage_request), sizeof(qa_damage_outcome), &combat->damage, error)) {
        bool allocation_failed = !combat->records;
        free(combat->records); free(combat);
        return allocation_failed ? memory_error(error) : false;
    }
    combat->actors = actors; combat->next_serial = 1;
    if (hooks) combat->hooks = *hooks;
    *out = combat; return true;
}
bool qa_combat_idle(const qa_combat *combat) { return combat && !combat->active_calls && !combat->active_hits; }
bool qa_combat_destroy(qa_combat *combat, qa_error *error) {
    if (!combat) return true;
    if (!qa_combat_idle(combat)) return qa_combat_argument(error, "cannot destroy active combat");
    if (!qa_operation_destroy(combat->damage, error)) return false;
    free(combat->records); free(combat->policies); free(combat); return true;
}
void qa_combat_actor_released(qa_combat *combat, qa_actor_record released) {
    if (!combat || released.id.slot >= qa_actors_capacity(combat->actors)) return;
    qa_combat_record *entry = &combat->records[released.id.slot];
    if (entry->active && qa_actor_id_equal(entry->actor, released.id)) memset(entry, 0, sizeof(*entry));
}
static bool next_serial(qa_combat *combat, uint64_t *out, qa_error *error) {
    if (combat->next_serial == UINT64_MAX) return qa_combat_argument(error, "combat ownership identity exhausted");
    *out = combat->next_serial++; return true;
}
bool qa_combat_register_policy(qa_combat *combat, const qa_combat_policy *policy, qa_error *error) {
    if (!combat || !policy || !policy->describe || policy->family < QA_GAME_Q1 || policy->family > QA_GAME_Q3)
        return qa_combat_argument(error, "invalid combat policy");
    if (!qa_combat_idle(combat)) return qa_combat_argument(error, "cannot change policies during combat");
    for (size_t i = 0; i < combat->policy_count; ++i) if (combat->policies[i].provider == policy->provider)
        return qa_combat_argument(error, "combat policy provider already registered");
    if (combat->policy_count == combat->policy_capacity) {
        size_t capacity = combat->policy_capacity ? combat->policy_capacity * 2 : 8;
        if (capacity < combat->policy_capacity || capacity > SIZE_MAX / sizeof(*combat->policies)) return memory_error(error);
        qa_combat_policy *policies = realloc(combat->policies, capacity * sizeof(*policies));
        if (!policies) return memory_error(error);
        combat->policies = policies; combat->policy_capacity = capacity;
    }
    combat->policies[combat->policy_count++] = *policy; return true;
}
bool qa_combat_unregister_policy(qa_combat *combat, qa_actor_owner provider, qa_error *error) {
    if (!qa_combat_idle(combat)) return qa_combat_argument(error, "cannot change policies during combat");
    for (size_t i = 0; i < combat->policy_count; ++i) if (combat->policies[i].provider == provider) {
        memmove(combat->policies + i, combat->policies + i + 1, (combat->policy_count - i - 1) * sizeof(*combat->policies));
        --combat->policy_count; return true;
    }
    qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "combat policy not registered"); return false;
}
bool qa_combat_create_actor(qa_combat *combat, qa_actor_id actor, const qa_combat_state *state, qa_error *error) {
    if (!qa_combat_live(combat, actor) || record(combat, actor)) return qa_combat_argument(error, "actor is stale or already owns combat state");
    if (!state_valid(state, error) || state->armor.regular.kind == QA_ARMOR_SOURCE)
        return qa_combat_argument(error, "local combat requires native armor storage");
    uint64_t serial; if (!next_serial(combat, &serial, error)) return false;
    qa_combat_record *entry = &combat->records[actor.slot];
    *entry = (qa_combat_record){.actor = actor, .serial = serial, .active = true, .state = *state};
    return true;
}
bool qa_combat_bind(qa_combat *combat, qa_actor_id actor, const qa_combat_binding *binding, bool replace, qa_error *error) {
    if (!qa_combat_live(combat, actor) || !binding || !binding->read || !binding->write_health || !binding->write_armor)
        return qa_combat_argument(error, "invalid combat binding");
    qa_combat_record *entry = &combat->records[actor.slot];
    if (cursor_for(combat, actor)) return qa_combat_argument(error, "cannot replace a live damage binding");
    if (entry->power_admitting) return qa_combat_argument(error, "cannot replace combat during power fuel admission");
    if (record(combat, actor) && !replace) return qa_combat_argument(error, "actor already has combat storage");
    if (record(combat, actor) && (entry->protection[0].reserved || entry->protection[1].reserved))
        return qa_combat_argument(error, "close protection leases before replacing combat storage");
    uint64_t previous_serial = entry->serial;
    bool previous_active = entry->active;
    qa_combat_state initial;
    ++combat->active_calls; bool ok = binding->read(binding->context, &initial, error); --combat->active_calls;
    if (!ok || !state_valid(&initial, error)) return false;
    if (!qa_combat_live(combat, actor) || entry->serial != previous_serial || entry->active != previous_active)
        return qa_combat_argument(error, "combat actor or storage changed during admission");
    uint64_t serial; if (!next_serial(combat, &serial, error)) return false;
    qa_inventory *power_inventory = previous_active ? entry->power_inventory : NULL;
    qa_item_id power_item = previous_active ? entry->power_item : 0;
    *entry = (qa_combat_record){.actor = actor, .serial = serial, .active = true, .external = true,
        .binding = *binding, .power_inventory = power_inventory, .power_item = power_item};
    return true;
}
bool qa_combat_bind_power_inventory(qa_combat *combat, qa_actor_id actor, qa_inventory *inventory,
                                    qa_item_id item, qa_error *error) {
    qa_combat_record *entry;
    if (!inventory || !item || !combat)
        return qa_combat_argument(error, "power cells require an inventory and item");
    if (!require_record(combat, actor, &entry, error)) return false;
    if (entry->power_inventory)
        return (entry->power_inventory == inventory && entry->power_item == item) ||
               qa_combat_argument(error, "actor already has a different power cell reservoir");
    if (entry->power_admitting || cursor_for(combat, actor))
        return qa_combat_argument(error, "power fuel admission requires an inactive combat binding");
    uint64_t serial = entry->serial;
    qa_inventory_entry fuel;
    entry->power_admitting = true;
    ++combat->active_calls;
    bool ok = qa_inventory_entry_read(inventory, actor, item, &fuel, error);
    --combat->active_calls;
    bool current_owner = record(combat, actor) == entry && entry->serial == serial;
    if (current_owner) entry->power_admitting = false;
    if (!ok) return false;
    if (!isfinite((float)fuel.count) || !current_owner)
        return qa_combat_argument(error, "invalid or retired power fuel binding");
    entry->power_inventory = inventory; entry->power_item = item; return true;
}
bool qa_combat_power_inventory(qa_combat *combat, qa_actor_id actor, qa_inventory **inventory,
                                qa_item_id *item) {
    qa_combat_record *entry = record(combat, actor);
    if (inventory) *inventory = entry ? entry->power_inventory : NULL;
    if (item) *item = entry ? entry->power_item : 0;
    return entry && entry->power_inventory;
}
static qa_combat_cursor *cursor_for(qa_combat *combat, qa_actor_id actor) {
    for (qa_combat_cursor *cursor = combat->current; cursor; cursor = cursor->previous)
        if (cursor->active && qa_actor_id_equal(cursor->outcome->request.target, actor)) return cursor;
    return NULL;
}
static bool journal_reserve(qa_combat_cursor *cursor, qa_error *error) {
    if (cursor->outcome->mutation_count < cursor->journal_capacity) return true;
    size_t capacity = cursor->journal_capacity ? cursor->journal_capacity * 2 : 8;
    if (capacity < cursor->journal_capacity || capacity > SIZE_MAX / sizeof(qa_damage_mutation)) return memory_error(error);
    qa_damage_mutation *mutations = realloc(cursor->outcome->mutations, capacity * sizeof(*mutations));
    if (!mutations) return memory_error(error);
    cursor->outcome->mutations = mutations; cursor->journal_capacity = capacity; return true;
}
static void advance_cursors(qa_combat *combat, qa_actor_id actor, const qa_damage_mutation *mutation) {
    for (qa_combat_cursor *cursor = combat->current; cursor; cursor = cursor->previous) {
        if (!cursor->active || !qa_actor_id_equal(cursor->outcome->request.target, actor)) continue;
        switch (mutation->kind) {
        case QA_MUTATION_HEALTH: cursor->observed.health = mutation->value.health.after; break;
        case QA_MUTATION_ARMOR: cursor->observed.armor = mutation->value.armor.after; break;
        case QA_MUTATION_SOURCE_VELOCITY: cursor->velocity = mutation->value.velocity.after; cursor->has_velocity = true; break;
        case QA_MUTATION_IMPULSE: cursor->has_velocity = false; break;
        }
    }
}
static bool vector_equal(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static bool observe_store(qa_combat *combat, qa_combat_cursor *cursor, const qa_damage_mutation *mutation, qa_error *error) {
    qa_combat_record *entry = record(combat, cursor->outcome->request.target);
    if (!cursor->active || !entry || entry->serial != cursor->binding_serial || cursor->reaction_seen)
        return qa_combat_argument(error, "damage observation is closed or its owner retired");
    qa_combat_state actual;
    if (!read_state(combat, entry, &actual, error)) return false;
    switch (mutation->kind) {
    case QA_MUTATION_HEALTH:
        if (!isfinite(mutation->value.health.after) || mutation->value.health.before != cursor->observed.health || actual.health != mutation->value.health.after)
            return qa_combat_argument(error, "observed health differs from authoritative store");
        break;
    case QA_MUTATION_ARMOR:
        if (!qa_armor_equal(mutation->value.armor.before, cursor->observed.armor) || !qa_armor_equal(mutation->value.armor.after, actual.armor))
            return qa_combat_argument(error, "observed armor differs from authoritative store");
        if (qa_armor_equal(mutation->value.armor.before, mutation->value.armor.after)) return true;
        break;
    case QA_MUTATION_SOURCE_VELOCITY:
        if (!qa_vec_finite(mutation->value.velocity.before) || !qa_vec_finite(mutation->value.velocity.after) ||
            (cursor->has_velocity && !vector_equal(cursor->velocity, mutation->value.velocity.before)))
            return qa_combat_argument(error, "invalid observed source velocity");
```

