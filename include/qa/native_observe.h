#ifndef QA_NATIVE_OBSERVE_H
#define QA_NATIVE_OBSERVE_H

#include "qa/native.h"

typedef struct qa_native_entry_observer qa_native_entry_observer;
typedef struct qa_native_write_observer qa_native_write_observer;
typedef struct qa_native_write_scope qa_native_write_scope;
typedef struct qa_native_call_scope qa_native_call_scope;

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
 * change write subscriptions. Source execution requires an exact stopped-write
 * scope from this event; an ordinary invocation cannot use the publication. */
typedef bool (*qa_native_write_observer_fn)(void *context, qa_native_instance *instance,
                                          const qa_native_write_event *event,
                                          qa_error *error);

bool qa_native_observe_entry(qa_native_instance *instance, qa_native_address entry,
                             const qa_native_signature *signature,
                             qa_native_entry_observer_fn callback, void *context,
                             qa_native_entry_observer **out, qa_error *error);
/* Cold execution requires all saved Source entries to be claimed by their
 * actual upper callback owners. */
bool qa_native_observers_restore_ready(const qa_native_instance *, qa_error *);
bool qa_native_unobserve_entry(qa_native_entry_observer *binding, qa_error *error);
bool qa_native_invoke_original(qa_native_entry_observer *binding,
                               const qa_native_value *arguments, size_t count,
                               qa_native_value *result, qa_error *error);
typedef bool (*qa_native_entry_cancel_fn)(void *, const qa_error *);
/* Exact emulated VOID original invocation. accepts runs only on a failed
 * application callback while CPU/journal/ABI ownership is still sound. An
 * accepted failure restores the complete enclosing CPU, retaining Source RAM. */
bool qa_native_invoke_original_cancellable(qa_native_entry_observer *,
    const qa_native_value *,size_t,qa_native_value *,qa_native_entry_cancel_fn,
    void *,bool *cancelled,qa_error *);
/* Capture the actual emulated processor before lowering a declared Source call.
 * Resolve its execution before finally cleanup. Only the exact accepted
 * application error can restore this CPU; Source RAM remains committed.
 * Cancellation supplies no Source return value (the caller's donor uses zero).
 * Close after cleanup, including failed calls. Nested scopes unwind in order. */
bool qa_native_call_scope_open(qa_native_instance *,qa_native_entry_cancel_fn,
    void *,qa_native_call_scope **,qa_error *);
bool qa_native_call_scope_resolve(qa_native_call_scope *,bool completed,
    bool *cancelled,qa_error *);
/* Cleanup-only retirement after all native/guest callbacks have returned.
 * An unresolved failed call becomes terminal; no late CPU/result recovery. */
bool qa_native_call_scope_abandon(qa_native_call_scope *,qa_error *);
bool qa_native_call_scope_close(qa_native_call_scope **,qa_error *);
bool qa_native_observe_writes(qa_native_instance *instance, qa_native_address address,
                              size_t bytes, qa_native_write_observer_fn callback, void *context,
                              qa_native_write_observer **out, qa_error *error);
bool qa_native_unobserve_writes(qa_native_write_observer *binding, qa_error *error);
/* Borrow the actual paused emulated CPU for synchronous Source reactions.
 * The scope cannot outlive this exact callback. Successful nested ABI calls
 * restore the enclosing CPU and retain their genuine RAM effects. */
bool qa_native_write_scope_open(qa_native_instance *, const qa_native_write_event *,
    qa_native_write_scope **, qa_error *);
bool qa_native_write_scope_close(qa_native_write_scope **, qa_error *);

#endif
