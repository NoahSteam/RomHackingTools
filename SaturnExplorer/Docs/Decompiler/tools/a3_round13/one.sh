#!/bin/sh
# one.sh file.out "name prefix" : print one fixture's block without the 'ended' lines
awk -v pat="====  $2" 'index($0,pat)==1{f=1;print;next} /^====  /{f=0} f' $1 | grep -v "ended:" | cut -c1-170
