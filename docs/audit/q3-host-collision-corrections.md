# Q3 source collision and native host ownership

The shared collision kernel now represents model selection independently of
the Q3 transformed-trace path. The ordinary public geometry trace keeps its
existing behavior. Source host adapters can choose an untransformed real
submodel or a transformed trace explicitly; temporary box and capsule handles
reuse the same shape implementation.

The capsule swap preserves the donor's original moving-box bounds and point
classification while substituting the target capsule radius and offset. It
resolves real Q3 model 255 when present. The host remains responsible for source
handle admission, no-node early return, and temporary-box state. An independent
source review compared these paths with the donor Q3 collision model/world
implementation and found no further defect in this bounded change.

Fixed-signature native Q3 imports now pass their authoritative signature to the
concrete host so pointer width and floating-point arguments remain typed.
The unsupported automatic binding of source slot zero as the Q3 world is removed.
Artifact-specific entity projection must establish the actual source identity.

Reverse actor-release notification clears matching native owned/borrowed slots
after canonical actor invalidation. Owned release bookkeeping runs after the
slot is cleared, preventing duplicate bookkeeping during nested shared release.
Host-origin release follows the same order. Host destruction rejects active
callbacks so release bookkeeping cannot free the host being traversed. Root
read the slot binding/storage and release implementation before integration.

B23/B24 remain open: the concrete game/cgame/UI adapters and their application
bindings are still being written. No engine code was compiled or executed.
