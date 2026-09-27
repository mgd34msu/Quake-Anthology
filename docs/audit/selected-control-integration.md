# Selected player control integration

The application is implementing one control record per canonical actor generation,
containing selected movement state and cutscene pose. Native Q2 observes that
owner through a read-only callback. Client tick, after-movement, end-frame and
weapon tick clear input latches and skip native advancement while controlled.
The clearing operation has no callback or allocation and preserves cooked
grenades and reserved ammunition.

Root read the complete bounded Q2 change and affected entry contexts against the
donor's retained cutscene ownership. No additional defect was confirmed in that
packet. This does not qualify the unfinished application control, camera,
provider-phase or save integration. No engine execution ran.

Native Q3 now retains character-only cinematic presentation and input suppression
in its typed player continuation. Spawn and explicit clear remove that state.
The application still owns the independently selected movement family, shared
body, combat and linking. An independent reviewer accepted the final two-file
packet; root read that complete diff and checked the handed-off file hashes.
The same change reacquires the actor generation after observing combat traits.
