#!/usr/bin/env bash
# Lists source comments and messages admitting that behavior was left unported.
# Token counts cannot see these: a file can pass depth_audit with whole branches
# falling into a no-op. Each hit is a lead. Clear it by porting the behavior, or
# by rewording it if the text describes something legitimate, such as a guest ABI stub.
# Usage: tools/admission_audit.sh [path...]
RX='unresolved|not (yet )?(constructed|ported|implemented|modeled|modelled|composed|wired|landed)|until .{0,30}lands|once .{0,30}lands|no landed|no concrete|deferred to|left for|see unresolved|slice report|parent reconciliation|out of (this )?slice|in this slice|for now|simplif(y|ied)|stub|placeholder|TODO|FIXME'
grep -rnIEi "$RX" "${@:-src}" ${1:+} --include='*.c' --include='*.h' | grep -vE 'QA_CATALOG_UNRESOLVED|"[^"]*unresolved[^"]*"|catalog/'
