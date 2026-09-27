# Jev launcher and local marketplace repair

## Scope and snapshots

This record covers the local `vibecheck-jev` installation repair on
2026-09-27. It does not establish automatic Codex hook delivery. The repair
started from commit `bef608534cd272266a9aaabb1bb4094a9a47270f`; the repaired
source, including the regenerated runtime bundle, is committed locally at
`bfdac9d74b7f86701fbe96678671993bf0cda99f`. Nothing was pushed or published.

The repair preserves the exact prior Codex configuration at:

```text
/home/buzzkill/.codex/config.toml.vibecheck-jev-before-local-20260927.bak
```

The backup has mode `0600` and SHA-256
`9a413381a480d179dd54c090630f21a96f774b32b868db9df888a0b63ff0f1d1`.
The current configuration has SHA-256
`bfbbebaf499f7ec3f065c37d485ce98049af4f8577914a9d78d48bf1cccd2f74`.
Its only difference from the backup changes the `vibecheck-jev` marketplace
from the Git URL at tag `v1.0.0` to the local source checkout:

```toml
[marketplaces.vibecheck-jev]
source_type = "local"
source = "/home/buzzkill/Projects/vibecheck-jev"
```

All three existing `vibecheck-jev` hook trust hashes remained unchanged. The
repair did not disable a hook, alter a trust decision, or add a fail-open
exception.

## Cause and correction

The old marketplace entry owned the installed cache. A refresh from its pinned
Git tag could therefore replace local launcher changes. A prior manual overlay
updated selected files but left the cache launcher without commit `bfe2bbe`'s
shell-key loader.

The local Codex CLI reported `marketplace remove`, `marketplace add`, and
`plugin add` as the supported operations. The repair used those commands to
remove the pinned marketplace, register the source checkout, and reinstall the
plugin cache. `codex plugin list --json` then reported both the plugin source
and the marketplace source as local
`/home/buzzkill/Projects/vibecheck-jev`.

The first reinstall exposed a second stale generated file. Commit `bef6085`
changed `src/jev/hooks/pretool.ts` to normalize `collaboration.*` and
`functions.collaboration.*` tool names. Its committed
`runtime/vibecheck-jev.mjs` still accepted only bare tool names. The hook
matcher invoked the launcher for a namespaced tool, but the stale runtime
allowed the call before requesting a judgment.

`bun run build:release` regenerated the runtime and release archives. The
source checkout's `bun run check:bundle` passed. The first
`bun run verify:release --native` attempt stopped because the default Node.js
was `v22.23.1`, below the launcher's Node.js 24 minimum. The verifier passed
after its `PATH` selected the already installed Node.js `v26.1.0`. The passing
run covered release checksums, bundled bytes, hook files, source and extracted
plugin MCP lifecycles, and hooks on Bun and Node.js. The plugin cache was then
reinstalled from the local marketplace source.

## Installed bytes

The source checkout and installed cache have identical bytes for these files:

| File | SHA-256 |
|---|---|
| `scripts/vibecheck-jev.sh` | `0e4ad85d4f3f3077f7ba1d4716f6a255ab6ccb5a2a1f1c013e883331dfac0418` |
| `hooks/codex-hooks.json` | `50cd57a02b5f8a7c93d9aa9fb5d4f6fbfacd04367ad73959277ceabf024c8414` |
| `runtime/vibecheck-jev.mjs` | `23b91c359af8148803bbe2b31ec728e8a6d163b401fb116510757139c2004bbf` |

The regenerated `artifacts/SHA256SUMS` has SHA-256
`1041c582225fd5f2c65417baa7579f360292566dcb0961a03bd35ff7069583f7`.
The installed cache was populated before the bundle-only source commit, so its
metadata still records source commit
`bef608534cd272266a9aaabb1bb4094a9a47270f`. Its installed runtime SHA-256 is
identical to commit `bfdac9d74b7f86701fbe96678671993bf0cda99f`; the stale
metadata does not indicate different installed bytes.

## Explicit judgment evidence

No key value was printed or stored in this record. An explicit installed-
launcher probe ran with `TYPESAFE_API_KEY` removed from its environment. The
launcher loaded the existing interactive-shell setting and reached hosted Jev.
The `sources --source typesafe` probe reported model `jev-1.13.0`, a `0.97`
reading, and `179 ms` latency.

After the bundle rebuild and reinstall, a second environment-stripped explicit
probe invoked `hook pretool --client codex` with tool name
`collaboration.send_message`. It produced ledger entry
`v_38f3312fa2384dcca2adf77e45860fa3` at sequence 2551. Hosted
`jev-1.13.0` allowed the brief with `narrows=0.35`, `defers=0.24`, and `169 ms`
latency. This proves the installed launcher, shell-key loader, namespace
normalization, hosted judgment, and ledger write path when invoked explicitly.

## Automatic delivery remains unverified

Normal `send_message` calls from this running Codex client did not add a
`hook:pretool` ledger record. Entries at sequences 2529 and 2530 were explicit
CLI probes from the coordinator, not automatic hook invocations. No automatic
invocation is therefore claimed for this client session. A fresh client must
show a new `hook:pretool` record tied to a normal tool call before automatic
delivery is accepted.
