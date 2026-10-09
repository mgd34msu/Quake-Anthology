THE-2859 workload qualification, offline recommendation

Use the existing private input helper and console on one separate qualification launch per retained artifact/product. Do not add a queued wait script or production instrumentation. Use the exact copied 42-file profile, retail content and measured-launch prefix. After gameplay is active, type the following, then close the console:

  gfxinfo; frameinfo
  r_mode; r_customwidth; r_customheight; r_fullscreen
  com_maxfps; cl_maxfps; r_maxfps; r_swapInterval
  fov; cg_fov; host_framerate; fixedtime; timescale
  skill; deathmatch; coop; sv_fps
  screenshotPNG qualification

For Q3 also use viewpos. Existing frameinfo reports map, renderer, clients, configuration, received source_ms and input_ack. gfxinfo reports the actual drawable and driver. The screenshot is separate visual evidence; its dimensions also confirm output size. Read the source console replies, not just the requested argv values. If dimensions differ, correct them through the existing r_mode/r_customwidth/r_customheight/fullscreen and vid_restart path on the private profile and qualify again; don't relabel the requested resolution as actual.

Specific Q3 evidence: /tmp/qa-private-av-vm3nur58/timing-result.json reached its diagnostic deadline. Its log has latched-setting notices and no actual gfxinfo/frameinfo/camera reports. Thus neither the claimed 320x200 nor effective caps are established. The copied profile has settings/images.cfg custom size 475x517; Q3 source/client archives have r_mode 3 and com_maxfps 85, with fov 90 versus 120. options.c:95-109 converts CLI width/height to leading startup cvar sets; it does not by itself prove the final compositor drawable. No cause of the diagnostic deadline is asserted here.

Controlled comparison: use the original games' existing timedemo path with ONE fixed demo stream per product, retained unchanged by direct bytes/stat pins. Record once via the existing record/stop commands if no suitable retail demo exists. Replay the identical bytes before and after. Qualify active map, dimensions, effective FOV/caps and the demo's camera/source timeline first. Then use the same existing passive present observer, timers OFF, pinned cores0-7,12-19, >=600 warm presents and600 consecutive measured presents. No queued waits, console probes or captures during that measured window. Any separate timers-ON stage pass may receive its public report commands after startup, with the console closed during the sample.

Why timedemo: qsrc Q3 cl_cgame.c:1005-1014 advances serverTime by50ms for every demo frame, independent of machine speed. The C port implements that at network/q3/clock.c:58-63 and consumes fixed snapshots at frontend/network.c:8290-8314. qsrc NQ cl_demo.c:98-109 reads one recorded message per timedemo frame and restores recorded view angles; C network_q1_client.c:864-869 does the same admission. C Q2 network_q2_client.c:777-781 advances by the demo server frame and original server FPS (100ms classic or recorded rerelease cadence). This controls the server-state sequence and camera input without re-running gameplay RNG at a speed-dependent cadence. Do not claim a live qualified demo path until both artifacts actually reach it.

Claim boundary: this is a CLIENT frame/presentation benchmark. It does not benchmark authoritative native server ticks or prove gameplay RNG equivalence. Do not disable monsters, pause simulation, replace native gameplay or lower visual settings to manufacture a speedup. A stationary native map run can still be reported as an observational native-frame comparison if dimensions/caps/camera are actually established and repeated A/B ordering is quiet, but it is not an exact state/RNG replay.

Current public observability does not supply a generic gameplay RNG-state query. Q1 seeds derive from provider.owner (providers.c:423); Q2 seed uses provider.owner at providers.c:825; Q3 seeds likewise derive from owners and Q3 world creation can include publication_generation (map.c:2815-2818). The existing frameinfo gives configuration/source time, not these seed states. Public save is game state evidence, but vanilla legacy saves do not universally expose or preserve the C engine's complete RNG state. Do not pretend save/argv/idle camera proves it.

Fixedtime alone is insufficient to prove all native games matched: Q2/Q3 have original fixedtime controls (Q2 qcommon/common.c:1523-1524, Q3 common.c:2590-2591; C console/frame_time.c:113-164), but Q1 host_framerate still goes through a72Hz real elapsed-time admission (C console/frame_time.c:197-211 / providers.c:1399-1403). Faster render frames can therefore yield different numbers of Q1 game ticks in the same1800 rendered frames. cl_avidemo would add capture file I/O and must not be used for ordinary timing.

No new tools, game runs, debugger, SDK changes, production hooks or timing results were produced for this recommendation. Existing compact controller remains held until actual workload evidence and the final production pair are nominated.

Source last read: e81f359f0c2adcdf2487b61aecc7c74702de7804 plus concurrent working-tree frame-time caller migration; function names remain the reference if source lines move.
