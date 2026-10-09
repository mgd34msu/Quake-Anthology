# THE-2873 canonical collision checkpoint

Main's last verified commit is `e8077e3c`. This checkpoint contains the complete canonical contents/surface-type migration and the selective live-field decoder optimization. Integration builds, public geometry proof and pinned timings are pending; it is not an installed build.

Contents and flags are decoded at BSP load or the live native-module boundary. Queries and shared snapshots carry canonical bits. Original module/protocol writers export their exact native words. The old pairwise converter and 32-bit mask scan are deleted.

Component packets are under `/tmp/qa-the2873-*-20261009`: `canonical-domain`, `q1-canonical`, `q2-canonical`, `q3-canonical`, `movement-canonical`, `compat-canonical`, `network-canonical`, `q2-gameplay`, `canonical-gameplay`, `frontend-bits`, `application-canonical`, `persistence-bits`, and `selective-decode`. These are component checks, not installed gameplay evidence.

Next: synchronize the staged tree with `/tmp/qa-sync-frozen-sdk-20261008.py`; build production and ASan; run the seven core checks on each. Then run the prepared `public-canonical` and `selective-decode` fixtures and sequential pinned timings. Keep THE-2873 In Progress and the previous qa-c installed until the whole slice qualifies.

The 05:24 installation remains the owner's executable. No desktop game or audible test is authorized. Keep the older account-switch WIP branch until all its remaining drafts have an explicit disposition.
