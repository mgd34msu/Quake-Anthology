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
