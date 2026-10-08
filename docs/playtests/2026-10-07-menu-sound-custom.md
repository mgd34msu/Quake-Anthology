# THE-185 / MIKE-12: Sound and current Custom comparison

The shared Sound menu now displays integral sample rates and recognizes
numerically equivalent saved values. The disabled hook no longer adds an
irrelevant style row to Custom or Equipment. Local lobby is now reachable and
its host/ready/two-match/end/close route works on CPU and NVIDIA GL. Its labels
use the shared menu style. Peer joining remains outside the proven host route;
this report does not claim full menu parity.

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
TypeScript includes a Local lobby row. At this rate-plus-hook comparison, C
had no frontend consumer of its compiled lobby/session service. THE-856
tracked that missing adapter; the later installed route below fills it.

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
These rate-plus-hook receipts precede the lobby adapter and remain stamped
to that earlier artifact.

## Installed lobby route and shared styling

Commit `938ff929` connects Custom to the existing typed lobby/session service
through one frontend adapter. The actual selected launch draft is copied into
the room, and the published transport supplies its real bound endpoint.
Host, Ready and Start use the existing startup queue. End game returns the
same room to OPEN and clears readiness; starting the next match preserves
its selection. Close removes the room and membership.

`1e4e4098` removes the separate small-text helper: phase, member and status
labels use ordinary disabled buttons, as the current TypeScript menu does.
`eb32916f` uses its existing 92 + row*28 geometry and 28-unit height. The shared
renderer supplies font scale, disabled color, padding and row backgrounds.
No separate font or widget implementation was added.

The current TypeScript attempt `/tmp/qa-private-av-z8e3wja3` captures its actual
empty lobby, Offline rejection and Hosting controls. The empty-lobby PNG and
`ui/common/layout.ts` confirm the row coordinates; `ui/settings/local-lobby.ts`
defines phase/member/status as disabled buttons. Its room-creation step timed
out, and the process monitor then encountered an exiting descendant. The
packet is retained as incomplete. Its overlapping title is visible in the
reference; C draws one title. There is no accepted TypeScript hosted-room
comparison from that attempt.

The exact candidate built 2026-10-07 21:29:45 CDT from `eb32916f` includes
these changes plus the shared menu sound dispatch in `e9d3ec54` (THE-316).
All 3,056 C/header/CMake inputs directly match the committed source. Unrelated
existing work is excluded. CPU `/tmp/qa-private-av-hakps395` and NVIDIA GPU0
GL `/tmp/qa-private-av-7hkpnf79` each use a fresh owner34 copy and real input:
Custom → Local lobby → Offline rejection → Native Hosting Apply → room name
and capacity → Host → Ready → Start → completed gameplay → End game.
Both then start and end a second match, close the room and public quit0.
Actual match generations are 1 and 2, and the bound transport agrees with
the published endpoint. Root inspected the room and return PNGs on both
renderers. Original settings and candidate/helper files stayed unchanged;
all 27 CPU and 28 GL owned process tokens were independently absent.

`tools/install_qualified_build.py` installed this exact candidate into
`qfiles/qa-c` at 21:34:20 CDT using the copied-profile GL qualification.
Receipt `installed-m0-menu-lobby-sound-20261007.json` in the recovery cache is
PASS; the installed executable and both companions directly equal the
qualified files. The Slack install note is
<https://adhdinc.slack.com/archives/C0C69RVFPLL/p1791427053262149>.

The earlier host-route CPU/GL packets `2rrxmxzf` and `mhddy1pz` remain valid
for `938ff929`, before the label/layout changes. Second-consumer joining,
remote handshake and lobby retention through full save restore remain
unqualified under THE-856. THE-185 and THE-856 stay In Progress for those
remaining scopes. Functional debugger observations make no timing or
individual sound-fidelity claim.
