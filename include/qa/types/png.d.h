/* Port of src/types/png.d.ts: ambient declaration only.
 *
 * The TypeScript file declares that importing a "*.png" asset with
 * Bun yields a default-exported path string:
 *   declare module "*.png" { const path: string; export default path; }
 * It has no runtime code, and C has no asset-import declaration to
 * mirror, so there is no C counterpart and no src/types/png.c.
 * Callers pass PNG paths as ordinary C strings. */
#ifndef QA_TYPES_PNG_H
#define QA_TYPES_PNG_H

/* Intentionally empty: see above. */

#endif
