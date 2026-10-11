/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
int upstream_nv12(const uint8_t*,int,uint8_t*,int,uint8_t*,int,int,int);
int rk3576_nv12_709fr(const uint8_t*,int,uint8_t*,int,uint8_t*,int,int,int);
static uint32_t seed=0x12345678;
static uint32_t random32(void) { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed; }
static double now(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void test(int w,int h,int pad,int cube) {
    int ss=w*4+pad*4,ys=w+pad,uvs=w+pad+3;
    size_t n=(size_t)ss*h,ny=(size_t)ys*h+64,nu=(size_t)uvs*(h/2)+64,i;
    uint8_t *src=malloc(n+64),*a=malloc(ny+nu),*b=malloc(ny+nu);
    if(!src||!a||!b)exit(3);
    for(i=0;i<n+64;i++)src[i]=(uint8_t)random32();
    if(cube)for(i=0;i<(size_t)w*h;i++) {
        src[4*i]=i;src[4*i+1]=i>>8;src[4*i+2]=i>>16;src[4*i+3]=0xff;
    }
    memset(a,0xa5,ny+nu);memset(b,0xa5,ny+nu);
    upstream_nv12(src,ss,a+17,ys,a+ny+11,uvs,w,h);
    if(rk3576_nv12_709fr(src,ss,b+17,ys,b+ny+11,uvs,w,h)||memcmp(a,b,ny+nu)) {
        for(i=0;i<ny+nu&&a[i]==b[i];i++);
        fprintf(stderr,"Mismatch %dx%d pad=%d byte=%zu ref=%u neon=%u\n",w,h,pad,i,a[i],b[i]);exit(1);
    }
    free(src);free(a);free(b);
}
int main(void) {
    int i;
    for(i=0;i<600;i++)test(2+2*(random32()%257),2+2*(random32()%65),random32()%33,0);
    test(318,242,7,0);test(1920,1080,0,0);test(2560,1440,0,0);test(3840,2160,0,0);
    test(4096,4096,0,1); /* All 16,777,216 RGB colors. */
    {
        int w=2560,h=1440,frames=60;
        uint8_t *src=malloc((size_t)w*h*4),*dst=malloc((size_t)w*h*3/2);
        double start;
        if(!src||!dst)return 3;
        for(i=0;i<w*h*4;i++)src[i]=(uint8_t)random32();
        start=now();for(i=0;i<frames;i++)upstream_nv12(src,w*4,dst,w,dst+w*h,w,w,h);
        printf("SCALAR 1440p mean_ms=%.3f\n",(now()-start)*1000/frames);
        start=now();for(i=0;i<frames;i++)rk3576_nv12_709fr(src,w*4,dst,w,dst+w*h,w,w,h);
        printf("NEON 1440p mean_ms=%.3f\n",(now()-start)*1000/frames);
        free(src);free(dst);
    }
    puts("PASS 605 cases, all RGB colors, strides, even tails and write canaries; byte-identical to upstream");
    return 0;
}
