# Cinematic audio audience

Q3 material cinematics retain the viewing seat's audio audience. The shared
cinematic kernel previously derived all material audio as world audio. Options
now carry an explicit target-derived, seat or world audience; the default keeps
existing callers' behavior. Initial stream publication and restored streams use
one resolver. Checkpoints retain the override and reject a different routing
identity during restoration.

Root read the complete bounded header/kernel diff, capture and restore callers,
and the donor Q3 cinematic creation path, which supplies seat audio independently
of its image target. This is source review only. The Q3 frontend and actual
application consumers remain under implementation; no engine code was run.
