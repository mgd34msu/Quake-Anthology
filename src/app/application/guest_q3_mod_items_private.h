#ifndef QA_APPLICATION_GUEST_Q3_MOD_ITEMS_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_MOD_ITEMS_PRIVATE_H
#include "guest_q3_mod_items.h"
#include "guest_q3_mod_private.h"
typedef struct item_field { size_t record; uint32_t offset; } item_field;
typedef struct item_override { uint32_t address,instruction; int32_t value; bool unequal; } item_override;
typedef struct item_capacity {
    enum { ITEM_CAPACITY_CONSTANT,ITEM_CAPACITY_FIELD,ITEM_CAPACITY_SOURCE } kind;
    int32_t constant;
    item_field field;
    uint32_t instruction;
    item_override *overrides; size_t count;
} item_capacity;
typedef struct item_bit { qa_item_id item; uint32_t mask; } item_bit;
typedef struct item_storage {
    bool bits; item_field field; qa_item_id item; item_capacity capacity;
    uint32_t private_mask; item_bit *items; size_t count;
} item_storage;
typedef struct item_definition {
    qa_item_admission admission;
    application_q3_mod_call *actions[2];
    qa_buffer icon,held;
} item_definition;
typedef struct item_test { item_field field; bool masked,at_most; uint32_t mask; int32_t value; } item_test;
typedef struct item_predicate { uint32_t instruction; bool unselected; } item_predicate;
typedef struct item_actor_pointer { size_t record; mod_pointer pointer; } item_actor_pointer;
typedef struct item_selection { int32_t value; qa_item_id item; } item_selection;
typedef struct item_continued_call { uint32_t instruction; application_q3_mod_call *call; } item_continued_call;
typedef struct item_stage {
    uint32_t input_entry,dispatch_entry,request_entry,request_argument,continue_entry,continue_branch;
    bool original_taken;
    item_actor_pointer dispatcher,continuation;
    mod_pointer movement;
    uint32_t movement_length,minimum,maximum;
    item_field clock,selection,view_height,ground;
    item_selection *values; size_t value_count;
    item_test *settled,*accepted,*when; size_t settled_count,accepted_count,when_count;
    item_predicate *predicates,*continue_predicates; size_t predicate_count,continue_predicate_count;
    item_continued_call *calls; size_t call_count;
} item_stage;
struct application_q3_mod_items_profile {
    application_q3_mod_profile *source;
    item_definition *definitions; size_t definition_count;
    item_storage *storage; size_t storage_count;
    item_stage *stage;
};
typedef struct item_record_address { size_t record; uint32_t address; } item_record_address;
typedef struct item_pending {
    struct item_pending *next;
    uint64_t sequence;
    qa_inventory_change *changes; size_t count;
} item_pending;
typedef struct item_actor {
    struct item_actor *next;
    application_q3_mod_items *owner;
    qa_actor_id actor;
    item_record_address *addresses; size_t address_count;
    qa_inventory_lease lease;
    qa_qvm_binding watch;
    item_pending *pending;
    application_q3_item_request request;
    application_q3_item_request_status status;
    unsigned references;
    bool attempted,weapon_bound,releasing,admitting;
} item_actor;
struct application_q3_mod_items_application {
    application_q3_mod_items *owner;
    struct application_q3_mod_items_application *previous;
    application_q3_mod_application *source;
    qa_actor_id actor;
    bool applied;
};
struct application_q3_mod_items_entry {
    application_q3_mod_items *owner;
    struct application_q3_mod_items_entry *previous;
    qa_qvm_call call;
    qa_actor_id actor;
    bool dispatcher,request,continuation,accepted,continued;
    int32_t requested;
    uint32_t movement;
};
struct application_q3_mod_items {
    application_q3_mod_items_profile *profile;
    application_q3_mod *mod;
    qa_inventory *inventory;
    application_q3_mod_items_services services;
    item_actor *actors;
    application_q3_mod_items_application *application;
    application_q3_mod_items_entry *entries;
    uint64_t next_request;
    unsigned calls;
};
bool q3items_current(item_actor *,qa_error *);
item_actor *q3items_actor(application_q3_mod_items *,qa_actor_id);
bool q3items_address(item_actor *,item_field,uint32_t *,qa_error *);
bool q3items_scalar(item_actor *,item_field,const qa_qvm_committed_write *,int32_t *,qa_error *);
bool q3items_capacity(item_actor *,const item_capacity *,const qa_qvm_committed_write *,int32_t *,qa_error *);
bool q3items_read(item_actor *,const item_storage *,const qa_qvm_committed_write *,
    qa_inventory_entry *,size_t,qa_error *);
bool q3items_stage_parse(application_q3_mod_items_profile *,const qa_json_document *,qa_json_id,qa_strings *,qa_error *);
bool q3items_field_parse(application_q3_mod_items_profile *,const qa_json_document *,qa_json_id,item_field *,bool storage,bool capacity,qa_error *);
bool q3items_call_parse(application_q3_mod_items_profile *,const qa_json_document *,qa_json_id,application_q3_mod_call **,qa_error *);
bool q3items_active(item_actor *,qa_item_id *,qa_error *);
bool q3items_tests(item_actor *,const item_test *,size_t,bool *,qa_error *);
#endif
