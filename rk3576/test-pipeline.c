/* SPDX-License-Identifier: Apache-2.0
 * Guard-page checks for truncated/untrusted command batches and framing rules.
 */
#define _GNU_SOURCE
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define XRDP_MPP_PARSER_ONLY
#define g_getenv getenv
#define g_strcmp strcmp
#define XR_RDPGFX_CMDID_STARTFRAME 0x000b
#define XR_RDPGFX_CMDID_WIRETOSURFACE_1 0x0001
#define XR_RDPGFX_CMDID_ENDFRAME 0x000c
typedef struct { union { struct { char *cmd; int cmd_bytes; } gfx; } u; } XRDP_ENC_DATA;
#include "xrdp_mpp_pipeline.inc"
static void le32(unsigned char *p,unsigned int v) {int i;for(i=0;i<4;i++)p[i]=v>>(i*8);}
static unsigned int rng=0x87654321;
static unsigned int random32(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void) {
    unsigned char valid[45]={0};int n,checks=0;
    long pagesize=sysconf(_SC_PAGESIZE);
    unsigned char *guard=mmap(NULL,pagesize*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    XRDP_ENC_DATA enc;
    if(guard==MAP_FAILED||mprotect(guard+pagesize,pagesize,PROT_NONE))return 3;
    valid[0]=11;le32(valid+4,16);le32(valid+8,77);
    valid[16]=1;le32(valid+20,17);valid[26]=11;
    valid[33]=12;le32(valid+37,12);le32(valid+41,77);
    setenv("XRDP_MPP_PIPELINE","1",1);
    for(n=0;n<=45;n++) {
        enc.u.gfx.cmd=(char*)guard+pagesize-n;enc.u.gfx.cmd_bytes=n;
        memcpy(enc.u.gfx.cmd,valid,n);
        if(mpp_release_frame_id(&enc)!=(n==45?77:0))return 1;
        checks++;
    }
    for(n=0;n<100000;n++) {
        unsigned char buf[45];int offset=n%45;
        memcpy(buf,valid,45);buf[offset]^=(unsigned char)(random32()|1);
        enc.u.gfx.cmd=(char*)guard+pagesize-45;enc.u.gfx.cmd_bytes=n%3?45:-1;
        memcpy(enc.u.gfx.cmd,buf,45);
        (void)mpp_release_frame_id(&enc);checks++;
    }
    enc.u.gfx.cmd=(char*)valid;enc.u.gfx.cmd_bytes=45;
    valid[29]=1;if(mpp_release_frame_id(&enc))return 1;valid[29]=0;checks++;
    le32(valid+41,78);if(mpp_release_frame_id(&enc))return 1;le32(valid+41,77);checks++;
    setenv("XRDP_MPP_PIPELINE","0",1);if(mpp_release_frame_id(&enc))return 1;checks++;
    munmap(guard,pagesize*2);
    printf("PASS pipeline parser %d guarded framing/length/flag cases\n",checks);
    return 0;
}
