# Local actor damage admission

Q3 shootable movers consume a hit through their source use callback. Routing this
through an ordinary health/death reaction exposed a false death-class outcome to
shared observers. The external combat binding already supported early admission;
locally stored actors now have the same callback stage without replacing their
health, armor or protection owners.

`qa_combat_set_admission` binds or clears a local callback. It rejects external
storage, malformed callbacks and changes during that actor's damage, admission or
power-fuel publication. Storage serials detect changed ownership. The dispatch
copies callback/context, tracks nested admissions and unwinds the count only if
the original record and serial survive the callback. Actor retirement clears the
record. Explicit source damage executors retain their existing admission owner.

The application owner independently reviewed the header, storage and dispatch
diff and found no defect in that bounded source review. Provider restoration must
rebind callbacks, and provider teardown must clear them or retire their actors
before destroying callback context. The Q3 owner is integrating this contract and
removing the death workaround. No compilation or runtime checks were performed;
complete gameplay/composition acceptance remains open.
