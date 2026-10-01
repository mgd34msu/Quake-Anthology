# QuakeWorld source origin

The QW movement origin now owns three binary64 components. QC source input
adds its authored float origin, authored float `mins`, and the QW center
offsets in that storage. A spectator return before integration preserves the
center; body publication subtracts the current authored source `mins` before
storing the source float words.

QW vector add and advance evaluate binary64 expressions and then store float
components, matching the TypeScript `MovementMath` vector stores. Nudge uses
the unsnapped binary64 base, including its zero-offset attempt. Recursive
command slices retain their literal binary64 `milliseconds * 0.001` origin
integration interval. The shared fly path preserves the existing NetQuake
float interval operations while carrying QW remaining time in binary64.

The actual QW movement-field codec stores x, y, and z as little-endian f64
fields. Its enclosing application controls owner must use version 3. The
generic movement-origin accessor projects to a float body without replacing
the authoritative QW origin. QW input validation checks the authoritative
components before projection.

Source references: `quake-typescript` QC `readQuakeWorldState` and
`writeQuakeWorldState`, QW `nudge` and `spectatorMove`, `MovementMath`, and
`createMutableVectorMath`; original QW `pmove.c` and `sv_user.c`.

This is a source-only change. No executable checks have run. The float scene
query still narrows unrounded QW start coordinates in fixed-pose/body-expansion
traces. The wider QW scalar arithmetic, collision numeric policy, and full
movement baseline remain open; this packet does not establish their parity.
