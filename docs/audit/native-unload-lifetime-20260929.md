# Native shutdown and loader teardown ownership, 2026-09-29

This is a source repair. No compiler, parser, test, executable or benchmark was run. The parent granted narrow ownership of the native instance/memory/import-call boundaries, runner handoff/child consumption, host teardown and guest role consumers. Exact source hashes are frozen in `/tmp/qa-native-unload-freeze.txt` for independent review by the Q2 worker. The earlier committed native activation repair was independently accepted by the Q2 and world workers; this teardown packet supersedes its changed guest-role files and requires separate review.

## Confirmed failures

Direct destruction unbound libffi import closures and tables before calling `dlclose` or `FreeLibrary`. A library destructor calling a retained fixed import could therefore enter freed closure storage. Tagged source allocations and the application's Q3 bridge also retired before the loader finished. Variadic imports lacked their owning thread's active instance during unload. A failed source shutdown left its role initialized and could repeat a partially performed callback on cleanup retry. A counted null argument list in `qa_native_call` could be read by Q3 entry admission before generic FFI validation.

## Repairs and real consumers

`qa_native_destroy` rejects initial busy admission without consuming ownership. After admission it consumes either backend, preserving the first shutdown, loader-callback or transport error. Import closures, tables, tagged allocations and callback context remain live through loader teardown, then release. Direct loading failure follows the same unload-before-unbind ordering. Host bootstrap record allocations also release on failed construction.

An explicit owning-thread unload scope retains TLS and active depth through direct unload and runner DESTROY callback pumping. Memory and tag operations use this actual scope; ordinary exports, invocation, checkpoint, new destruction and canonical source-slot bindings reject teardown. The Q3 host remains alive until its native executor unloads. A new Q3 actor projection cannot be admitted during unload because its source record storage cannot survive the module.

`qa_native_host_destroy_ready` exposes actual callback/executor admission. Admitted host destruction consumes both backends even when reporting a cleanup fault. The guest role consumer clears that pointer immediately, keeps the shared bridge through unload, and retains a descriptor for a later close retry when reporting an error. Root owns migration of its remaining provider host consumer. The runner child clears a consumed instance before responding with a cleanup error.

Shutdown lifecycle consumption occurs only immediately before a validated direct `ffi_call`, or after runner request encoding and before handing it to the transport. Invalid argument/result admission leaves native lifecycle unchanged. A failed transport handoff cannot safely authorize replay of a potentially executed remote callback. Guest role shutdown checks source/world activity before its attempt, consumes its initialized state before calling source code, closes idle resources after failure and preserves the first error. Later retries do not reenter that source shutdown.

OS library constructors and destructors execute source code during committed loading and owned unloading. This repair preserves the callback lifetimes; it does not isolate arbitrary library code. Full native mixed-region interoperability, frontend/snapshot/bot integration, portable continuations and runtime behavior remain open.

## Source checks and acceptance

Read actual `native_profile_unbind`, fixed libffi import dispatch, variadic TLS dispatch, native instance lifecycle, runner callback pumping/child destroy, tagged memory, host memory/dispatch/cleanup and guest role shutdown/close paths. `git diff --check` passed for the affected files before freeze.

The Q2 worker independently matched all 14 current frozen hashes and accepted the bounded instance/import/FFI/memory/runner teardown, native host lifetime and Q3 role shutdown/destroy/retry changes. It traced direct loader/profile unbind, runner callbacks, borrowed/owned Q3 binding and portal cleanup dependencies and found no additional confirmed defect. Broader checkpoint/cvar changes and runtime behavior were excluded. The author separately reread the parent's migrated `providers.c` native host consumer at SHA256 `1079bf7f4b0d049c1331734ed116752e85fe237f8f117b69b278e48a2048a976`: rejected readiness retains the owner; admitted destruction clears it before reporting a fault, leaving the remaining Q3 host for a later close retry. The Q2 reviewer was asked to include this superseding consumer change in its own evidence.
