# Application source audit: B32 and B33

These tasks remain incomplete. This packet records missing source, not a completion claim or a request to stop implementation. The graph is revision 6. Reviewed entry/build snapshot: `849a3b2159c5888ec965708894386f82150e2fda`; subsequent native/Q2 fixes do not supply the application owners described here.

## B32

Goal: Implement native menus, per-seat HUD, source/guest UIs, content selection, independent mod controls, and all settings.

Criterion: Every required menu/HUD/service workflow is connected to real engine implementations.

`src/main.c:7-14,90-107` supplies help/version and archive/BSP inspection. `CMakeLists.txt:559-561` links that entry only to `qa_content`. The source/header inventory has shared input, text, fonts, settings, console and render primitives, but no native menu controller, per-seat HUD owner, source UI host, or mod-control workflow. These prerequisites do not fulfill B32.

The donor's `src/ui/mods/menu.ts:5-80` demonstrates a concrete required workflow: individual mod rows, selection/search, enable/disable, unavailable reasons, refresh and close. The donor's `src/ui/hud/` and `src/ui/settings/` inventories include native Q1/Q2 and rerelease HUD layouts, weapon wheels, gameplay/input/accessibility/server/lobby/ranking settings. No corresponding C application consumer was found. The outstanding implementation must keep individual mod controls and all of the original graph scope; it cannot substitute one global game preset.

## B33

Goal: Implement camera/tools/diagnostics/capture and existing LLM provider, cancellation, and command integration.

Criterion: Required public tools and assistance have native application consumers; no invented external service.

`include/qa/display.h:69-76` exposes retained CPU pixels and capture; image encoders and GL readback primitives are supporting capabilities. There is no camera path/controller, capture command owner, shared diagnostic-tool controller, or LLM request/provider/command owner in the C source inventory or executable. This is missing baseline work, not an approved extension point.

The donor contains `src/camera/application.ts`, `src/camera/spline.ts`, `src/capture/index.ts`, `src/debug/world.ts`, and `src/debug/shapes.ts`. Its `src/llm/api.ts` implements the existing streaming provider adapters, while `src/llm/request.ts:3-58` defines cancellation, HTTP request cleanup and stream ownership. The C implementation needs equivalent user behavior and cancellation/cleanup through native services; no TypeScript promises or browser stream wrappers are required. Source inventory and the inspected LLM request bodies establish missing scope, not full audit coverage of those donor subsystems.

## Limits

No C application behavior has been run. No configuration, compiler, tests, generator, sanitizer or benchmark ran for this packet. B32/B33 have not previously been reported complete. Their dependencies and required implementation remain unchanged. Source completion and later P01 qualification are separate requirements.
