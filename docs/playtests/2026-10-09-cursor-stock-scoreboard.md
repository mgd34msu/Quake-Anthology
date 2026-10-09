# THE-3167: cursor capture and stock Q1 scoreboard

The generated canonical cvar defaults inverted `_windowed_mouse` into `in_nograb=1` for Q1/QW/Q2. Correct the CSV defaults so ordinary gameplay captures the mouse; explicit saved overrides remain respected. The single SDL pointer-state implementation now hides the OS pointer while the window is focused, releases capture in windowed UI, and releases/shows it when focus is lost.

The generic Q1 event HUD drew Monsters/Secrets bars continuously. Stock Q1 HUD selection now uses its existing solo scoreboard instead. Combined configurations selecting a non-Q1 HUD keep the generic tallies. Remove the duplicate frontend score flag and special ENGINE-only scores callback: both command aliases and direct bindings use the single held QA_INPUT_SCORES action in every HUD consumer. Stock numbers and strings use the original bitmap art.

The same stock HUD consumer also prepares and draws the original intermission pictures and tally from the completed-level event. Component checks verify submissions and positions against WinQuake/sbar.c:1269-1304; a real exit-trigger intermission screenshot is still pending. THE-3167 is not fully closed by this step.

Production and ASan/UBSan builds passed, including seven existing core checks each. The new behavior check in the existing core test protects default capture, explicit overrides, physical key press/repeat/release, and two menu round trips for both score aliases. No new test target or coverage requirement was added. The generated cvar catalog matches its CSV.

The private exact-candidate classic CPU run logs focus GAME/UI/GAME/UI/GAME, mouse availability, and actual SDL relative/grab/cursor states. Gameplay is relative=1, grab=1, cursor=0; windowed menu is relative=0, grab=0, cursor=0; loss of focus shows/releases the OS cursor. TAB holds the stock solo scoreboard and release removes it. Real rerelease CPU screenshots show fresh gameplay without the generic panel and the stock held scoreboard. All completed runs quit normally, preserve the original owner profile and leave no recorded owned processes.

The owner's copied historical TAB binding is `inven`. Only the private test copy was bound to +showscores to exercise the symptom; the original binding is retained. A classic GL held-scoreboard screenshot also passes, but its first loading/menu captures were premature and are not fresh-gameplay proof. No speed or real audio-output claim is made.

Evidence: `/tmp/qa-the3167-private-20261009/held-classic-debug.json.log`; `/tmp/qa-private-wayland-pjs_p8dv`; `/tmp/qa-private-wayland-x7xw426z`; `/tmp/qa-private-wayland-barq1j92/user/tab-scoreboard.png`; `/tmp/qa-the3167-verified-build-20261009`; `/tmp/qa-the3167-core-regression-20261009`; `/tmp/qa-the3167-q1-hud-20261009`.

Additional failed assisted exit routes are recorded separately; none proves intermission. Further non-fatal playtest work is paused by the owner's core-first directive.
