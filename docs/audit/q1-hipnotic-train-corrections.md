# Hipnotic trains and brushes

The native map provider now implements func_train2, func_bobbingwater and
func_pushable from the TypeScript Hipnotic world module. The root review read
the donor hipnotic-train.ts, common.ts brush/targetEvent helpers, and the changed
C dispatch, mover, train and brush paths. Source scheduling retains pusher
local time. Train departure uses the previous corner's speed and event before
installing the new goal; teleport, wait, stop and use continuations remain typed
native state. Foreign authored targets use the shared target service.

Pushable proxies use the shared body and physics services. The source proxy
origin and bounds formulas are preserved, including their lack of an added
brush origin. Full actor generations are reacquired after callback boundaries.
The worker's final reread added proxy release on owner retirement or failed
owner linking during spawn. Root reread that cleanup before integration.

Root review also compared sound emission with the donor entity service.
Sound positions now use body center; missing emitter bodies propagate failure
instead of producing a sound at a fabricated origin.

The new source is registered in CMake. This is a bounded source comparison,
not game execution or acceptance of all B10/B13 criteria. Authored application
adapters and complete native checkpoint assembly remain open. No engine build
or execution was performed.
