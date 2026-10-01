# Frontend observer and frame timing source review

The bounded six-file unit consists of `src/console/cvars.c`,
`src/console/cvars_private.h`, `src/console/cvars_save.c`,
`include/qa/console_cvar_observer.h`, and
`src/app/frontend/frame_time.c` and `.h`.

Every public cvar mutator enters and leaves the genuine registry mutation
boundary. Nested mutations publish through the retained FIFO after the complete
outer mutation, including metadata and legacy binding effects. Equal writes do
not invent observer events. Capture, preparation, validation, and metadata
restoration reject an undrained registry. Restore binding validation retains the
registry's notification guard.

The frame helper reads the actual source registry. It retains donor dialect
behavior for fixed time, time scale, Q1/QW frame overrides, and Q3 numeric
conversion boundaries. It does not retain a shadow numeric clock.

The movement peer read the complete six-file packet, donor mirror and frame
helper, and actual Q3 cvar/save callers on 2026-10-01. The review reported no
confirmed findings. A possible observer detach during legacy notification was
not accepted as a finding because no concrete current production retirement
path through that callback was established.

This is a source review result only. No compilation, test, runtime, or generated
verification was performed. Role-time admission, full frontend continuation,
and the remaining B00–B34 acceptance work remain open outside this packet.
