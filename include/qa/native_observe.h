#ifndef QA_NATIVE_OBSERVE_H
#define QA_NATIVE_OBSERVE_H

#include "qa/native.h"

typedef struct qa_native_entry_observer qa_native_entry_observer;
typedef struct qa_native_write_observer qa_native_write_observer;

/* Bindings belong to the instance. Callbacks and contexts remain valid through
 * successful removal or instance destruction. Failed removal retains them.
 * Calls and subscription changes are confined to the owning simulation thread. */

/* Arguments and aggregate result storage are borrowed for this callback. The
 * callback replaces this invocation. invoke_original executes exactly one
 * bypassed invocation; nested invocations are still intercepted. Application
 * callbacks may synchronously invoke the same instance. */
typedef bool (*qa_native_entry_observer_fn)(void *context, qa_native_instance *instance,
                                          qa_native_entry_observer *binding,
                                          const qa_native_value *arguments, size_t count,
                                          qa_native_value *result, qa_error *error);

typedef struct qa_native_write_event {
    qa_native_address instruction;
    qa_native_address address;
    size_t offset, size;
    qa_bytes before, after;
} qa_native_write_event;

/* Delivered after the actual source instruction commits, before the next
 * application instruction (including a reaction entry). before/after contain
 * the complete watched range; offset/size identify its written intersection.
 * Unchanged stores are delivered. A write callback may read/repair memory and
 * change write subscriptions, but cannot execute application code. */
typedef bool (*qa_native_write_observer_fn)(void *context, qa_native_instance *instance,
                                          const qa_native_write_event *event,
                                          qa_error *error);

bool qa_native_observe_entry(qa_native_instance *instance, qa_native_address entry,
                             const qa_native_signature *signature,
                             qa_native_entry_observer_fn callback, void *context,
                             qa_native_entry_observer **out, qa_error *error);
bool qa_native_unobserve_entry(qa_native_entry_observer *binding, qa_error *error);
bool qa_native_invoke_original(qa_native_entry_observer *binding,
                               const qa_native_value *arguments, size_t count,
                               qa_native_value *result, qa_error *error);
bool qa_native_observe_writes(qa_native_instance *instance, qa_native_address address,
                              size_t bytes, qa_native_write_observer_fn callback, void *context,
                              qa_native_write_observer **out, qa_error *error);
bool qa_native_unobserve_writes(qa_native_write_observer *binding, qa_error *error);

#endif
