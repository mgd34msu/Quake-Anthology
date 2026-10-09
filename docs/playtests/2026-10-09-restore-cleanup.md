# Failed restore cleanup — THE-3166

A rejected saved candidate must leave the running game intact. Pending restored
mode data is not an executing callback, and an unpublished restored bot roster
must not receive the active world's shutdown callback. Actual callback, borrow,
and objective-reservation checks remain in place; candidate destruction still
releases its allocated owners.

Frozen build input `30f8a27dc0400611b26105750c3f8bec86f5e038` passed production
and ASan builds and the seven core checks in each. The exact production candidate
was launched privately with fresh copies of the owner's profile on headless
Wayland, with isolated display/audio and no debugger. Q2 rerelease and Q3 each
continued rendering the original map after rejecting an old saved candidate,
then exited normally through the compositor close event with code 0. Both source
profile comparisons were unchanged; neither run left an owned process alive.
The final world screenshots were inspected.

This proves failed-candidate cleanup, not successful restoration. Q2 rerelease
still rejects inventory-group reconstruction; Q3 still rejects the old persistent
event envelope. The diagnostic candidate was not installed into `qfiles/qa-c`.

Evidence: `/tmp/qa-the3166-failed-candidate-cleanup-20261009`,
`/tmp/qa-the3166-cleanup-restores-20261009.json`, and the case roots recorded there.
The earlier bounded diagnostic in `/tmp/qa-the3166-bot-shutdown-entry-result.json`
identified an unpublished roster with `restoring=1`, `calls=0`, and no admitted
source shutdown. That debugger run supplies state evidence only.
