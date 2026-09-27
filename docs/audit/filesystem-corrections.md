# Portable filesystem source corrections

## Scope and snapshot

This record covers the source corrections for FND-03 and FND-06b under B03 and
B15. The snapshot was taken at anthology commit
`4a649f2fe3c7c08150909897cd6414ca6bb52c6d`. The filesystem backends, public
header, and catalog discovery file were untracked at this snapshot;
`src/content/vfs.c` had working-tree changes. The hashes below identify the
reviewed bytes.

The review used text inspection only. The engine was not configured, compiled,
tested, or run. CMake registration and P01 Linux and Windows verification
remain required, so B03 and B15 remain open.

## Corrected defects

### Linux conditional declaration

`open_contained` declared `descriptor` inside the Linux `openat2` block and
again in the fallback path. Both declarations occupied the same function
scope on Linux. `src/platform/filesystem_posix.c:231` now declares one
function-scope descriptor and reuses it in both paths.

### Windows path re-resolution

The first Windows writable traversal validated a directory, closed its handle,
and retained only its path. `MoveFileExW`, `DeleteFileW`, and stream open or
truncate later resolved that path again. A concurrent rename or reparse-point
replacement could redirect the mutation after validation.

`writable_path` now retains the canonical root handle and every intermediate
directory handle. `lock_writable_directory` opens each directory with
`FILE_FLAG_OPEN_REPARSE_POINT`, verifies that it is a directory, and rejects a
reparse point. `writable_parent` verifies the locked root's volume, file index,
and creation time against the retained root. It also verifies each opened path
against the canonical root. `writable_path_close` releases the handle chain
only after replacement, removal, or stream publication completes.

### Windows write-share race

The retained handles originally allowed `FILE_SHARE_WRITE`. Microsoft documents
that [`FILE_SHARE_WRITE`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)
permits another open to request write access. Microsoft also documents that
[`FSCTL_SET_REPARSE_POINT`](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_set_reparse_point)
sets or changes a reparse point through a file or directory handle. The file
system algorithm requires `FILE_WRITE_DATA` or `FILE_WRITE_ATTRIBUTES` for
that operation. [`FSCTL_DELETE_REPARSE_POINT`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/fsctl-delete-reparse-point)
also requires write access.

`lock_writable_directory` now uses only `FILE_SHARE_READ`. Share checks are
symmetric: lock acquisition fails if an existing handle has write or delete
access, and later opens cannot request either access while the retained lock
exists. This closes in-place reparse mutation as well as directory replacement
during the publication window.

## Resulting boundary

The shared `qa/filesystem.h` boundary now owns platform traversal for both VFS
and catalog discovery. Its common layer validates normalized relative paths and
provides exact or folded deterministic component resolution. The POSIX backend
uses `openat2` when available and validates the final retained descriptor in its
fallback. The Windows backend validates final handle identity and path.

`src/content/vfs.c` retains roots and files across clones. Archive and loose-
file snapshots validate the retained identity before publishing bytes.
Writable replace, remove, and resumable stream operations use the same
filesystem boundary. `src/content/catalog/discovery.c` uses the boundary for
listing, status, case resolution, and path admission instead of duplicating a
platform traversal.

## Reviewed hashes

| File | SHA-256 |
|---|---|
| `include/qa/filesystem.h` | `ac10119980b008cd56e26b21cceeb534a97c57a73e5da39cd2b1a65327400486` |
| `src/platform/filesystem_internal.h` | `9ceaf66edd675c500d2442d59581284a46edecf9f6fb8787cf7db6f9f4a5138f` |
| `src/platform/filesystem.c` | `0c2ec2e4c8797927cc210a27c645ae8baa91cbd98192e500a2b1d82b0759be2b` |
| `src/platform/filesystem_posix.c` | `af55483987ee5361bebfd0269ff367f5427524f24ccc8c13672faed4f9c07dbe` |
| `src/platform/filesystem_windows.c` | `dac72e7af56050b2d313242f4251379611886d8ae83817f1bc2d8a34ef3e63eb` |
| `src/content/vfs.c` | `a2d672d0a67a51b7ce4279e8d3ed4a1aec44179803e0b7d525b0116802b99937` |
| `src/content/catalog/discovery.c` | `d3a579eaf6e2a60c1db687112594fe8dc8f165e684a9ec4e93f60960112a7c44` |

These files are frozen for coordinator review and CMake integration. P01 must
still compile both backends and run containment, reparse or symlink admission,
archive replacement, clone rollback, append or checkpoint, and catalog
discovery checks on the relevant platforms.
