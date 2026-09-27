# Shared source-script continuation

This source integration serves B23/B26 host and bot consumers. It does not close
either task or qualify runtime behavior.

The lexer and preprocessor publish the current token on EOF and partial failure.
Recognized source-language failure is distinguished from resource failure and an
unsupported source profile. Adjacent-string lookahead retains source ordering
without recursive native stack growth. Retained read frames replace the former
processed-token shortcut. Checkpoints preserve the raw token and failure state.

Quoted tokens now borrow the immutable input until transformation is required.
The first escape or lexer concatenation promotes the prefix to stable arena
storage. Root identified a movable scratch pointer that could survive an
allocation failure; this construction removes the final copy/allocation and
keeps partial output valid on failure.

The version-two checkpoint codec uses one field traversal for encoding and
decoding. It writes explicit little-endian values, preserves numeric bit
patterns and nullable strings, and encodes the absent-index sentinel independent
of native pointer width. Decode owns a copy of the payload and bounds array
allocation by the remaining encoded bytes. It rejects invalid versions, malformed
booleans/strings, truncated fields, invalid continuation structure, and trailing
bytes before publishing output. Root read the complete new codec and the
continuation/quoted-token changes before integration.

Native host adapters still need their concrete parser services and handle
lifetimes integrated. Complete save/application consumers remain required.
No engine parser, compiler, generator, tests, or executable was run.
