# Retained navigation crossings

The fixed-capacity trace API and retained collector use the same AAS traversal
and foreign-navigation intersection routines. The collector grows according to
actual crossings, bounded by the requested maximum, rather than allocating the
guest's maximum up front. Repeated AAS leaves remain in source order; foreign
intersections keep their stable fraction/ordinal ordering. Results belong to
the caller and survive reuse of navigation query scratch; failures clear count.

Root read the complete six-file change and the factored traversal bodies. No
additional defect was confirmed. Guest codecs are being migrated to retained
per-call storage so callbacks cannot invalidate an outer result. The new API
does not close the bot/application scopes. No engine execution ran.
