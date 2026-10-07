# Project instructions

Work from the Linear Quake-Anthology project in milestone order, with owner playtest defects first. Keep one implementation per shared capability. A cvar lookup reads the common table by canonical name or alias; do not add context or validation layers to make a valid lookup succeed. When a check rejects valid engine state, remove or narrow it unless the original engine has the equivalent check.

Every installation into `qfiles/qa-c` must use `tools/install_qualified_build.py`. First launch the exact candidate privately with a fresh copy of the owner's saved settings/profile, reach gameplay, and quit normally. Read the original profile without modifying it. A fresh empty profile alone does not qualify an installation. Pass the copied-profile qualification receipt to the installer before replacing the executable or posting a Slack retest note.

Game windows and audio must stay on private displays and audio servers, away from the owner's desktop and speakers. Clean up only recorded owned process IDs. Sound proof uses actual captured output; timing runs use no debugger. The coordinator owns SDK changes, builds and installations while workers capture a fixed installed artifact.

Put the Linear issue ID and relevant MIKE tag in commit subjects. After a fix is committed, installed and proved, move its issue to In Review with the commits, build time and bounded proof. Never mark issues Done; the supervisor verifies them and Mike retests owner defects. Post to Slack only when a new qualified `qa-c` is installed.
