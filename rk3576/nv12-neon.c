/* SPDX-License-Identifier: Apache-2.0
 * Conversion semantics derived from xorgxrdp module/rdpCapture.c:
 * Copyright 2014 Laxmikant Rashinkar; Copyright 2014-2017 Jay Sorg.
 * Original X11 permission notice is preserved in LICENSES/xorgxrdp.txt.
 * Bit-exact xorgxrdp BT.709 full-range conversion for little-endian AArch64.
 * Preserve its per-pixel floor and rounded 2x2 chroma average. No format change.
 * Input/output rectangles are even; unaligned addresses and padded rows work.
 */
#include <stdint.h>
#include <stddef.h>
#include <arm_neon.h>
static uint8x16_t rk_y(uint8x16x4_t p) {
    uint16x8_t lo=vmull_u8(vget_low_u8(p.val[2]),vdup_n_u8(54));
    uint16x8_t hi=vmull_u8(vget_high_u8(p.val[2]),vdup_n_u8(54));
    lo=vmlal_u8(lo,vget_low_u8(p.val[1]),vdup_n_u8(183));
    hi=vmlal_u8(hi,vget_high_u8(p.val[1]),vdup_n_u8(183));
    lo=vmlal_u8(lo,vget_low_u8(p.val[0]),vdup_n_u8(18));
    hi=vmlal_u8(hi,vget_high_u8(p.val[0]),vdup_n_u8(18));
    return vcombine_u8(vshrn_n_u16(lo,8),vshrn_n_u16(hi,8));
}
static uint8x16_t rk_uv(uint8x16x4_t p,int v) {
    uint8x16_t positive=p.val[v?2:0],negative=p.val[v?0:2];
    uint8x16_t g=p.val[1];
    uint16x8_t lo=vaddq_u16(vdupq_n_u16(32768),vshll_n_u8(vget_low_u8(positive),7));
    uint16x8_t hi=vaddq_u16(vdupq_n_u16(32768),vshll_n_u8(vget_high_u8(positive),7));
    lo=vsubq_u16(lo,vmull_u8(vget_low_u8(negative),vdup_n_u8(v?12:29)));
    hi=vsubq_u16(hi,vmull_u8(vget_high_u8(negative),vdup_n_u8(v?12:29)));
    lo=vsubq_u16(lo,vmull_u8(vget_low_u8(g),vdup_n_u8(v?116:99)));
    hi=vsubq_u16(hi,vmull_u8(vget_high_u8(g),vdup_n_u8(v?116:99)));
    return vcombine_u8(vshrn_n_u16(lo,8),vshrn_n_u16(hi,8));
}
int rk3576_nv12_709fr(const uint8_t *src,int ss,uint8_t *y,int ys,
                    uint8_t *uv,int uvs,int width,int height) {
    int row,x;
    if(width<0||height<0||(width&1)||(height&1))return 1;
    for(row=0;row<height;row+=2) {
        const uint8_t *a=src+(size_t)row*ss,*b=a+ss;
        uint8_t *ya=y+(size_t)row*ys,*yb=ya+ys,*u=uv+(size_t)(row/2)*uvs;
        for(x=0;x+16<=width;x+=16) {
            uint8x16x4_t pa=vld4q_u8(a+4*x),pb=vld4q_u8(b+4*x);
            uint16x8_t su=vaddq_u16(vpaddlq_u8(rk_uv(pa,0)),vpaddlq_u8(rk_uv(pb,0)));
            uint16x8_t sv=vaddq_u16(vpaddlq_u8(rk_uv(pa,1)),vpaddlq_u8(rk_uv(pb,1)));
            uint8x8x2_t out;
            vst1q_u8(ya+x,rk_y(pa));vst1q_u8(yb+x,rk_y(pb));
            out.val[0]=vrshrn_n_u16(su,2);out.val[1]=vrshrn_n_u16(sv,2);
            vst2_u8(u+x,out);
        }
        for(;x<width;x+=2) {
            int j,k,us=0,vs=0;
            for(j=0;j<2;j++)for(k=0;k<2;k++) {
                const uint8_t *p=(j?b:a)+4*(x+k);
                int r=p[2],g=p[1],bl=p[0];
                (j?yb:ya)[x+k]=(54*r+183*g+18*bl)>>8;
                us+=(32768-29*r-99*g+128*bl)>>8;
                vs+=(32768+128*r-116*g-12*bl)>>8;
            }
            u[x]=(us+2)/4;u[x+1]=(vs+2)/4;
        }
    }
    return 0;
}
