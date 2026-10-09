# THE-344 common rule ID

Movement, console and tick roles now use the same `qa_ruleset_id` type. Each role still holds its own selection; a Q1 world does not overwrite a Q3 player's movement or a Q2 module's tick policy. Values remain NetQuake 0, QuakeWorld 1, Q2 classic 2, Q2 rerelease 3 and Q3 4.

The three former enum definitions, four identity-conversion helpers and their 47 calls are deleted, including `game_domains.h`. One immutable descriptor in `src/core/ruleset.c` supplies settings and console names. Five local settings-name tables and the tool-name table are deleted. Settings keys, console spellings, module ABI records, collision families, button timing and protocol vocabularies retain their distinct meanings.

The source migration preserves numeric literals, field widths and non-include string/character literals. The cvar generator and its generated header change together. No compatibility typedef, second lookup table, checker rule, version choice or validation context was added. The descriptor consumers' direct failure-return count is unchanged.

## Verification

Full production and ASan/UBSan builds and all seven configured core checks pass for frozen source `fe0e74e6`. The earlier type-only source `ce9d04c9` also passed those checks.

GCC, Clang and GCC sanitizer before/after components each produce 315 identical files, 146,854 bytes per pair. Actual save codecs cross-read both directions across all 125 independent client/movement/tick combinations. Shared prediction records retain independent movement and tick IDs. Native Q2 command records remain exactly 16/28 bytes. The retained legacy-command fixture preserves NQ 15/666/999, QW 28, Q2 34/KEX and Q3 keyed/unkeyed fields. These are bounded codec comparisons, not a live server interoperability or full saved-session restore claim. The existing KEX angle-precision gap remains unchanged.

Six actual descriptor-consumer translation units pass strict compilation. Production and sanitizer components preserve all five settings names, five console names, existing invalid-ID behavior and 125 independent owner-key compositions.

Evidence: `/tmp/qa-the344-rule-id-codemod-20261009/authorized-preview`, `/tmp/qa-the344-rule-metadata-20261009`, `/tmp/qa-the344-rule-id-bytes-20261009` and `/tmp/qa-the344-rule-final-build-20261009`. The migration adds no frame allocator or new gameplay rule. No performance gain, live combined-game acceptance or installation is claimed by this type slice.
