# Shared command-space conversion

THE-869 keeps `qa_input_usercmd_build` as the human/bot builder and moves application command-space projections into one `qa_input_command_convert` in `src/input/commands.c`. The module normalization scale also lives there. Caller data supplies angle basis and original quantization rules; clocks, button meanings and guest command custody retain their existing game rules.

The seven application/input implementations no longer carry their own scaling and angle conversion blocks. The conversion preserves raw Q3 -128 axes, relative angle words, rerelease fractional movement, Q1 jump/up provenance, changed guest command writeback and the existing classic 16-byte/rerelease 28-byte native Q2 command layouts. Original protocol codecs and field widths are unchanged.

The isolated staged source `9c24b3f0f44ef9b514ad0974794d7972c750ed32`, based on `2aadaea7`, passed full production and ASan builds and all seven configured core checks. GCC/Clang strict checks and the retained physical human/bot fixture passed. Extracted actual old/new numeric paths matched across all five source/target rules, extrema, rounding ties and native Q2 command bytes, including ASan/UBSan/float-cast-overflow runs. Numeric conversion made zero wrapped heap calls. No failure path was added in the eight changed files.

Private source snapshots, exact commands and receipts are under `/tmp/qa-the869-command-space-20261009`; full build/source receipts are under `/tmp/qa-the869-isolated-build-20261009`.

This bounds proof to those components and full-build integration. It does not claim a frame speedup or complete live combined-module parity. The existing KEX angle precision gap remains: the shared Q2 command stores shorts while that protocol transmits floats. The installed owner executable is unchanged by this slice.
