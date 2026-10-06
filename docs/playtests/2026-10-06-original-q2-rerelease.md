# Original Q2 rerelease startup, 2026-10-06

The installed `41c128d0` executable and sibling native runtime received a
bounded stock `base1` check with the installed original game DLL. It did not
reach playable frames within the 20-second observation: the window stayed
black and the DLL save-descriptor initialization flag remained zero.

The last actual observation, at 19.916 seconds, counted 1,052 pointer entries,
1,052 name entries, 1,014 tag entries, 3,253 mappings and 474 registered imports.
This establishes progress during initialization, not startup completion or a
measured speedup. No public frame, weapon-fire or normal-quit acceptance is
claimed. The bounded run was stopped and all owned processes were removed.

The retained mapping inventory in `54709aec` has strict compiler and actual
mapping/scope component proof. This shipped check does not qualify its complete
original-DLL execution path. No debugger or profiler was attached. The child
packet-send failure appeared during cleanup and is not established as the
startup cause.

Evidence: `original-rr-retained-inventory-53dhs8rf/summary.json` and its actual
startup photograph and progress samples. The built-in Q2 rerelease gameplay
path is a separate checked mode; this result concerns original-module startup.
