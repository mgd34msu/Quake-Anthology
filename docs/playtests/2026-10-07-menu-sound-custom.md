# THE-185 / MIKE-12: Sound and current Custom comparison

The shared Sound menu now displays integral sample rates and recognizes
numerically equivalent saved values. The disabled hook no longer adds an
irrelevant style row to Custom or Equipment. Local lobby remains a separate,
confirmed Custom gap under THE-856; this report does not claim full menu parity.

## Cause and change

`settings_menu.c` compared the projected `s_outputRate` text with four literal
integers. A saved `22050.000000` therefore became a duplicate fifth choice,
whose label retained all six decimals. The menu now matches numeric values,
retains the original current or latched value for the selected choice, and
formats a custom rate as integral Hz. It does not change the sample rate,
archive representation, audio engine, or cvar aliases.

`startup_selection.c` enumerated hook styles even with Hook Off. The shared
metadata adapter now returns no style choices in that state. The existing
Custom summary and Equipment factory both omit the inactive field; enabled
hook selection still uses the existing mechanism and choices.

Neither change adds a failure return, validation context, hash, or duplicate
implementation.

## Current reference

The TypeScript reference uses the actual working tree at
`5d6c6e311db6749c0b8f8ffca53ebe2bae69b24e`, not an unstamped historical image.
Its full Git status was captured before and after the run and remained equal:
four modified tracked files and two untracked entries. The capture truthfully
records that dirty tree; it is not a clean-source parity claim.

Bundle: `/tmp/qa-private-av-emjokx16/user/THE185-ts-reference`.
The public input route captured Main, enabled-audio Sound, and Custom at
960x600, then closed normally. All 34 copied owner settings remained unchanged
at their original source. The actual SDL stream used private Pulse and its
null sink. Recorded owned processes were absent after cleanup.

The current TypeScript Sound page displays 48000 Hz; the copied C profile
selects 22050 Hz. Effects and music volumes also differ. These are observed
state differences, not grounds for changing the owner's settings to match
a screenshot. Both pages expose the Output device row and share the authored
panel, title, font, controls, and row layout.

The current Custom comparison confirms the same authored layout and summary
font design. Selected game, map, providers, and focus differ. C additionally
showed Hook style while Hook was Off; the metadata change removes that row.
TypeScript includes a Local lobby row. C has no frontend consumer of its
compiled lobby/session service, so setting an otherwise empty menu ID would
create a dead destination. THE-856 tracks the complete reachable adapter.

## Qualification

The strict candidate build and all six configured CTests pass. The candidate
source check compares all 3,054 C/header/CMake inputs directly: only the two
listed menu files differ from the committed base. Existing unrelated work is
excluded from this candidate.

Earlier rate-only CPU and GPU0 GL bundles `qa-private-av-p40hkb3_` and
`qa-private-av-yvhmbsny` show exactly four choices, selected `22050 Hz`, the
enabled device row, actual native Q3 menu entry into q3dm0, and public quit0.
Root inspected Sound, Custom and gameplay PNGs. Candidate/helper pins and
the original profile stayed unchanged; all 21 CPU and 22 GL owned process
tokens were absent. These runs precede the disabled-hook metadata change
and are not qualification receipts for the later candidate.

These are functional checks with read-only debugger observations and private
captured output, not performance measurements or individual menu-sound
fidelity proof.

Final rate-plus-hook candidate built at 19:55:33 CDT. Its exact GPU0 GL bundle
`/tmp/qa-private-av-rvt6rihl` and CPU bundle `/tmp/qa-private-av-ptgfmq8w`
both pass the copied-profile enabled Sound/Custom route, native Q3 menu entry
into q3dm0, completed gameplay, and public quit0. Each rate control contains
four choices and selects `22050 Hz`; neither Custom contains Hook style.
Root inspected the complete final Sound/Custom comparison and gameplay PNGs.
All 34 original settings, all three candidate pins, and helper pins remain
unchanged; all 22 GL and 21 CPU owned process tokens are absent.

Final side-by-sides and the observed control data are retained in
`/tmp/qa-the185-current-menu-comparison-20261007`, using `final-` filenames.
The GPU0 GL installer-shaped receipt qualifies this exact candidate. The
coordinator records the resulting commit and installed file equality in
`installed-m0-menu-rate-hook-20261007.json` under the recovery cache.
THE-185 remains In Progress for the reachable Local lobby adapter (THE-856).
