# Project instructions

Work from the owner's current goal and feature comparison. Do not access Linear. Keep one implementation per shared capability, built on the common primitives and event system, and delete replaced paths as callers migrate. A cvar lookup reads the common table by canonical name or alias; do not add context or validation layers to make a valid lookup succeed. When a check rejects valid engine state, remove or narrow it unless the original engine has the equivalent check.

Quake TypeScript is the feature baseline and the reference when Anthology behavior is unclear. Original Quake sources supply the original game rules. Preserve the fullest functionality that adds value to the game; do not remove capabilities to reduce code or token use. Command history, journals, recording and replay are wanted. History may exceed the legacy prediction window while original protocol fields and module interfaces remain compatible. Player bounds, view height and scaling are existing TypeScript functionality to port, not an unresolved owner decision.

Do not attribute planning, audits or restrictions to the owner without their direct instruction. A supervisor document is a supplied report, not proof that the owner personally planned or audited the work. Do not invent pending-owner approval gates.

Every installation into `qfiles/qa-c` must use `tools/install_qualified_build.py`. First launch the exact candidate privately with a fresh copy of the owner's saved settings/profile, reach gameplay, and quit normally. Read the original profile without modifying it. A fresh empty profile alone does not qualify an installation. Pass the copied-profile qualification receipt to the installer before replacing the executable or posting a Slack retest note.

Game windows and audio must stay on private displays and audio servers, away from the owner's desktop and speakers. Clean up only recorded owned process IDs. Sound proof uses actual captured output; timing runs use no debugger. The coordinator owns SDK changes, builds and installations while workers capture a fixed installed artifact.

Commit and push each verified slice with its existing issue ID and relevant MIKE tag. Report what the code, build, tests and live checks each establish; do not claim runtime proof from compilation. Post to Slack only when a new qualified `qa-c` is installed.
