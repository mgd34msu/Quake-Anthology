THE-2859 Q2 weapon-input cvar tail

Owned source: src/app/application/native_q2_console.c. The .h is unchanged. Four references are stored on the existing application_native_q2_console owner, resolved after successful console creation and after successful cvar restore. The per-frame helper reads current integer values through the same owner.cvars. The arbitrary-name source_integer function remains unchanged for cold/external calls. No scalar copy, new allocation, validation return, per-frame fallback, or wire change is introduced.

Actual caller coverage
- arsenal.c application_q2_weapon_input is the only source_weapon_input caller; it admits APPLICATION_PROVIDER_Q2 and reads the built-in source's live controls first.
- The rerelease dialect projects the four settings; classic retains its input flags exactly as before.
- guest_native_q2.c prepare_owner creates a GAME view for APPLICATION_PROVIDER_NATIVE, whose original DLL/.so imports read that view. It does not call this built-in helper, and is not rerouted through built-in handles. Hosted module runtime behavior was not executed in this component.
- The generated catalog resolves all four on a fresh GAME view before source declarations. Quick switch remains 1; the other three remain 0. References survive later declarations, live edits, prepared publication, and restore adoption. A new independent ENGINE and GAME view is bound again, with user declarations reversed to test actual replacement-store lifetime.

qsrc semantics
- quake2-rerelease-dll/rerelease/g_main.cpp:289-290 registers quick switch=1 and instant switch=0 with CVAR_LATCH; :339 registers infinite ammo=0 with CVAR_LATCH; :343 registers no-stack-double=0.
- p_weapon.cpp:30 reads infinite ammo; :46 reads no-stack-double; :319/:374 read instant switch; :459 reads quick switch. Gameplay rules and tick conditions are untouched.

Proof
- Component translation units contain exact extracted before/after private declarations, dialect(), registry(), source_integer(), source_weapon_input(), definitions(), and console_restore() bodies. The full owned source also passes GCC and Clang strict C17 syntax with -Werror.
- Real canonical cvar create/view/catalog/read/set/declare/latch/prepared-publication/save-capture/save-prepare/save-commit APIs are linked from frozen copied production or sanitized libraries.
- Only application_fail, console-idle and observer-registration boundaries are stubbed. This is not a full console construction, hosted-module, live game, or audio proof. The source constructor's actual cold bind placement is shown in owned.patch.
- Bare defaults; declarations; zero/positive/negative/fractional/out-of-range/nonfinite integer conversion; case-insensitive common-store edits; real fov/cg_fov alias edits; prepared values before and after publish; pending vs applied latch; arbitrary-name cold source_integer; independent reordered replacement store; actual saved-value adoption and subsequent edits; and existing hosted-provider exclusion are checked.
- The four target names have no aliases in the canonical table; no synthetic aliases were added. The actual fov/cg_fov alias mutation verifies shared-store alias changes do not invalidate weapon handles.
- All eight complete before/after input byte streams equal: 5,616 bytes, 78 sampled input records. GCC and Clang ordinary and ASan/UBSan runs all exit 0. Sanitizer environment overrides are removed; no sanitizer report is emitted. Partial extraction excludes unrelated functions and Clang's unneeded-internal-declaration warning is disabled only for the unused original tables in the component. The full owned file has no warning suppression.

Warm measured mechanism
- Each dialect runs 600 frames, two source_weapon_input calls per frame. Classic performs no setting reads; rerelease performs four reads per call.
- BEFORE named qa_cvars_find calls: 4,800. AFTER: 0.
- Warm qa_cvars_resolve calls: 0 in both. Wrapped malloc/calloc/realloc/non-null free calls: 0 in both.
- These are component call/allocation counts, not wall-clock frame-time or gameplay speed claims. Root owns full build and real frame-counter validation.

Frozen artifacts
- before/ and after/: owned-file copies.
- owned.patch: exact source delta.
- proof-before.c, proof-after.c, driver.inc: source and behavioral component.
- commands.json and run_fixture.py: all compiler/link/run commands and return codes.
- gcc-*/clang-* .log/.bin: outputs and full state byte streams.
- receipt.json: frozen source and bounded result summary.
