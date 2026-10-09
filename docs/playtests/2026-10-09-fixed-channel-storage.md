# THE-873 / THE-882 fixed channel storage

The existing Unified channel now reserves its record, bitmap, fragment and payload storage at construction through the shared `qa_arena` and `qa_pool` primitives. Its sealed backing replaces per-message and per-assembly malloc/calloc/free. Packet fields, quotas, ordering, ACK behavior and checkpoint bytes remain unchanged. Payload equality reads the same storage directly, including session continuation checks.

Frames use two contiguous slots per direction, avoiding an extra gather copy. Reliable future messages use bounded pages; the receive head owns the channel's existing contiguous delivery scratch as a pool lease. A future message promotes once when it becomes head, including partial messages. Declined admission retains the same lease for retry. One existing payload operation handles both ownership forms; no second reader or channel is added.

Default cold reservation is 38901551 bytes per channel. It is 15 bytes larger than the first contiguous-frame candidate, replacing its existing 4 MiB delivery scratch. This is an explicit memory cost, not a whole-engine zero-allocation claim. Runtime peer construction, owning checkpoint output and session document/frame leases still allocate outside the channel's hot storage path.

## Correctness

GCC/Clang strict and sanitizer before/after components preserve 16869981 transcript bytes; the original 16865235-byte anchor remains unchanged. Cases include fragmentation, loss/reordering, retries, ACKs, quotas, frame replacement, blocked admission, callback queueing/reentry, partial-head checkpoint restore and close. Watched hot heap allocation/free calls are zero, versus 1032/1032 before. Owning checkpoint output and cold construction/destruction are excluded from that count.

Exact integrated source `ec2138a1` passes full production/ASan builds and both seven configured core suites. The actual production-linked session receipt fixture passes 66 checks in each build, including multi-page corruption, acknowledged shortcuts and restored continuation state, with zero watched equality heap calls. These are engine/codec checks, not a live multiplayer or installed-game claim. Legacy transport adapters are unchanged.

## Pinned component timings

The first paged design regressed large receives, and was replaced before commit. The final ABBA is original, candidate, candidate, original, with 120 warm-up and 600 samples per case, affinity `0-7,12-19`, no debugger or profiler, and no other owned heavy job. Values are median / p99 microseconds per 1 MiB operation, 914 fragments:

| Operation | A1 | B1 | B2 | A2 |
| --- | ---: | ---: | ---: | ---: |
| Frame transmit | 85.945 / 134.460 | 90.020 / 142.320 | 89.646 / 97.980 | 91.570 / 99.380 |
| Frame receive | 86.135 / 133.120 | 87.670 / 98.520 | 89.040 / 103.680 | 92.665 / 109.371 |
| Reliable receive | 90.305 / 140.780 | 94.210 / 104.140 | 95.850 / 105.120 | 96.000 / 110.900 |

The revised receive-head layout recovers most of the first candidate's roughly 142 us reliable-receive cost. The final phases overlap baseline variation; no consistent latency speedup or whole-frame gain is claimed. Normal in-order reliable delivery adds zero gather copies. Out-of-order promotion still copies once. The retained benchmark includes all 15 size/operation cases, not only this large-message table.

Evidence: `/tmp/qa-the873-882-unified-channel-fixed-20261009`, `/tmp/qa-the873-882-unified-channel-reliable-head-20261009` (including `pinned-abba`) and `/tmp/qa-the873-882-channel-final-build-20261009`. One initial receipt incorrectly compared access time in whole stat objects; that run is excluded and retained separately. Valid ABBA executable pins use device/inode/size/mtime, all exits are zero and owned PIDs are absent. No install was made.
