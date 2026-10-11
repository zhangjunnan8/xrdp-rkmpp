#!/usr/bin/env python3
"""Patch fixed xorgxrdp 0.10.5; keep an independent upstream scalar oracle."""
import pathlib,sys,shutil
src=pathlib.Path(sys.argv[1]);here=pathlib.Path(__file__).resolve().parent
p=src/'module/rdpCapture.c';s=p.read_text()
name='a8r8g8b8_to_nv12_709fr_box'
assert s.count('\n'+name+'(')==1
start=s.index('\n'+name+'(')+1;body=s.index('{',start);depth=1;end=body+1
while depth:
    if s[end]=='{':depth+=1
    if s[end]=='}':depth-=1
    end+=1
reference=s[:s.index('*/')+2]+'\n'
reference+='#include <stdint.h>\n#define RDPCLAMP(x,l,h) ((x)<(l)?(l):(x)>(h)?(h):(x))\nint\n'
reference+=s[start:end].replace(name,'upstream_nv12',1)+'\n'
(src/'upstream-nv12-reference.c').write_text(reference)
s=s[:body]+'''{
    return rk3576_nv12_709fr(s8, src_stride, d8_y, dst_stride_y,
                            d8_uv, dst_stride_uv, width, height);
}'''+s[end:]
s=s.replace('#include "wyhash.h"','#include "wyhash.h"\n#include "rk3576-nv12-neon.c"')
assert s.count('#include "rk3576-nv12-neon.c"')==1
p.write_text(s)
shutil.copyfile(here/'nv12-neon.c',src/'module/rk3576-nv12-neon.c')
print('Prepared bit-exact NEON capture:',src)
