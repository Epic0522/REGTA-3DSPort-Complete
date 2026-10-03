#!/bin/bash
set -e

shlist=$1
vsh_obj=$2

name=$(basename ${vsh_obj%.shbin.o})
vsh_bin=${vsh_obj%.shbin.o}.shbin
vsh_hdr=${vsh_obj%.shbin.o}_shbin.h
shaders_src=$(for sh in $(cat $shlist); do echo $(dirname $shlist)/$sh; done)
shaders_hdr=$(for sh in $(cat $shlist); do echo $(dirname $shlist)/$(dirname $sh)/header.v.pica; done)
shaders_tmp=$(for sh in $(cat $shlist); do echo $(dirname $vsh_bin)/$sh; done)

echo compiling shader \"$name\"

echo preprocessing
for sh in $(< $shlist)
do
    tmp=$(dirname $vsh_bin)/$sh
    mkdir -p $(dirname $tmp)
    cat $(dirname $shlist)/$(dirname $sh)/header.v.pica \
	$(dirname $shlist)/$sh > $tmp
done

echo generating deps
echo $vsh_obj: ${shaders_hdr} $shaders_src > ${vsh_obj}.d

echo removing old shader
if [ -f $vsh_obj ]; then rm -f $vsh_obj; fi
if [ -f $vsh_bin ]; then rm -f $vsh_bin; fi
if [ -f $vsh_hdr ]; then rm -f $vsh_hdr; fi

echo calling picasso
picasso $shaders_tmp -o $vsh_bin -h $vsh_hdr
#rm $shaders_tmp

echo generating header
echo >> $vsh_hdr
cnt=0
for prg in $(< $shlist)
do
    # Helper files do not create a DVLE; do not consume a program index.
    if grep -q '^[[:space:]]*\.nodvle' "$(dirname "$shlist")/$prg"; then continue; fi
    prg_name=$(basename $prg .v.pica | tr [a-z] [A-Z])
    echo "#define VSH_PRG_${prg_name} $cnt" >> $vsh_hdr
    cnt=$((cnt+1))
done

echo >> $vsh_hdr
echo "extern const u8 ${name}_shbin[];" >> $vsh_hdr
echo "extern const u8 ${name}_shbin_end[];" >> $vsh_hdr
echo "extern const u32 ${name}_shbin_size;" >> $vsh_hdr

echo linking
bin2s $vsh_bin | $AS -o $vsh_obj;
