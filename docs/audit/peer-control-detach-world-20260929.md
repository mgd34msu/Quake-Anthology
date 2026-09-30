# Control detach peer review, 2026-09-29

Source-only bounded review of `application_control_detach`, `application_guest_input_actor_idle`, retained source scope ownership and the guest drop-queue admission guard. No compiler, test, parser, program or runtime was run. No confirmed defect found in these inspected helpers.

Matched SHA256 identities: `control.c` `a7dda1f23129b2825965f492f33c69ad9955a118c33d074f9443c848787c2832`, `arsenal_guest.c` `4984b020395250465bdfc365787cd5bb8180038e4bc2ad063d878a9c81f0e817`, and `guest_input_private.h` `179d9955579755bf1429fd07594f5fc056a1037ad68a53bac958d062422e4778`, all under `src/app/application`.

Detach checks the exact full actor identity and the actual live provider input owners. Active command actor, applying actor or any retained same-actor source scope prevents detach. Native moving/retired controls likewise prevent freeing retained movement completion. An absent or reused-generation record is an idempotent success. Accepted detach frees the retained movement result and clears that matching control only; it writes no canonical body, source state or actor ownership.

The guest retirement queue independently checks this same idle admission before attempting dynamic player participation removal. This does not accept the whole new guest queue or dynamic roster lifecycle, which still needs its actual roster/routing consumers. Entire guest locomotion/completion, source-state projection, compiled behavior and baseline functionality remain outside this narrow review.
