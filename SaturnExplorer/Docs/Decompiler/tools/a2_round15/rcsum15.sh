#!/bin/bash
# usage: rcsum14.sh <out dir of famrc14.sh>   -> one line per rule: applied, checked, bad, unmatched, skipped, maskskip, uncompared (applications with no root compared), deadchg, dropped
cat "$1"/fam*.log | grep '^RULECHECK ' | awk '{a[$2]+=$4; c[$2]+=$6; b[$2]+=$8; u[$2]+=$10; s[$2]+=$12; m[$2]+=$14; n[$2]+=$16; d[$2]+=$18; r[$2]+=$20} END{for(k in a) printf "%-26s applied %9d checked %10d bad %4d unmatched %7d skipped %8d maskskip %6d uncompared %7d deadchg %7d dropped %7d\n", k, a[k], c[k], b[k], u[k], s[k], m[k], n[k], d[k], r[k]}' | sort -k3 -n -r
echo "maskwide: $(cat "$1"/fam*.log | grep '^RULECHECK-MASKWIDE' | awk '{s+=$2} END{print s+0}')"
echo "--- whole-function check (wholefn14.hh): per rule applied changed compared bad unsupBefore unsupAfter limit undef vacuous"
cat "$1"/fam*.log | grep '^WHOLEFN ' | awk '{a[$2]+=$4; c[$2]+=$6; p[$2]+=$8; b[$2]+=$10; ub[$2]+=$12; ua[$2]+=$14; l[$2]+=$16; u[$2]+=$18; v[$2]+=$20} END{for(k in a) printf "%-26s applied %9d changed %9d compared %10d bad %4d unsupBefore %8d unsupAfter %7d limit %6d undef %6d vacuous %8d\n", k, a[k], c[k], p[k], b[k], ub[k], ua[k], l[k], u[k], v[k]}' | sort -k3 -n -r
echo "--- why the whole-function check could not judge:"
cat "$1"/fam*.log | grep '^WHOLEFN-WHY ' | awk '{n=$2; $1=""; $2=""; w[$0]+=n} END{for(k in w) printf "%9d %s\n", w[k], k}' | sort -n -r | head -20
