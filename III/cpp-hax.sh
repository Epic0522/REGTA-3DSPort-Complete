#!/bin/bash

# fuck the c preprocessor
# wasn't even a good macro language in the 70s 
# but at least its dumb enough to "parse"
# output files work nicely with emacs (M-x grep-mode <CR>)

find ./src -type f -iregex '.*\(cpp\|h\)' -exec grep -EHn '#if|#elif|#ifdef|#ifndef' '{}' ';' > cpp-conditionals
find ./src -type f -iregex '.*\(cpp\|h\)' -exec grep -EHn '#define' '{}' ';' > cpp-macros
sed -E -n 's/.*#define[[:space:]]+([_a-zA-Z][_a-zA-Z0-9]*).*/\1/p' cpp-macros | sort | less | uniq > cpp-words

(for x in $(< cpp-words)
 do
     echo $x;
     find ./src -type f -iregex '.*\(cpp\|h\)' -exec grep -Hn "\<${x}\>" '{}' ';' | sed -E 's/(.*)/\t\1/';
 done) > cpp-words-xref
