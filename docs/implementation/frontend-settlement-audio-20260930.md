# Q3 settlement sound ownership

The round reset in `frontend/round.c` clears the engine's listener seats before the four source settlement frames. `frontend_event_sound()` previously sent settlement one-shot sounds to `qa_audio_engine_play()` immediately. Its actual implementation iterates the active seats, so a request with zero seats produced no voice.

The frontend event owner now retains the resolved audio asset, owned diagnostic name, original actor generation, source owner, spatial request, and admitted play time. Play and channel-stop projections retain their original order. Footstep selection and its random state advance when the source effect is consumed, and the selected asset is retained; publishing the sound does not repeat that selection.

The real presentation pass installs listeners before `frontend_event_audio()` publishes these requests. An active round or a listener inventory with no real mixer keeps them pending. Publishing removes each request before its callback, preventing a partially failed output from replaying that request. New round and world retirement free pending references. Persistent loop and static-sound holders retain their existing ownership.

The event asset inventory includes pending assets. QAPE version 3 records the actual deferred flag and ordered projections, including asset ordinal, digest and source identity, full actor provenance, owned name, sound policy, and admitted time. Import resolves the exact already restored asset inventory without registering or acquiring content. It requires the captured actor and audio ID to match the already restored identity table, including retired provenance, and cannot mint a new ID. Existing loop and last-footstep asset holders use that same pure inventory resolver. Failed decode uses the existing event-owner cleanup.

This is a source change. No build, parser, test, compiler, game, or audio-device execution has run. Full frontend candidate construction and publication remain separate unfinished work.
