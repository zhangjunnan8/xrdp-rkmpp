/* SPDX-License-Identifier: Apache-2.0
 * Calls the actual XRDP callback and actual MPP/x264 objects. Synthetic input only.
 */
#include <config_ac.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include "log.h"
#include "xrdp_encoder_mpp.h"
#include "xrdp_encoder_x264.h"

static double milliseconds(clockid_t clock)
{
    struct timespec t;
    clock_gettime(clock, &t);
    return t.tv_sec*1000.0+t.tv_nsec/1000000.0;
}
static int compare(const void *a, const void *b)
{
    double x=*(const double *)a, y=*(const double *)b;
    return (x>y)-(x<y);
}
struct poison_source { unsigned char *data; size_t bytes; int calls; };
static void poison_after_copy(void *arg)
{
    struct poison_source *p = arg;
    memset(p->data, 0xe3, p->bytes); p->calls++;
}
int main(int argc, char **argv)
{
    int w, h, frames, f, y, x, stride, rows, bytes, flags, software, rv, full, recovery_calls=0;
    unsigned char *input, *reference, *packet;
    FILE *encoded, *raw;
    void *encoder;
    double *times, begin, cpu, sum=0, encode_cpu=0, frame_cpu;
    uint64_t total=0;
    struct log_config *log;
    short rect[4];
    struct poison_source poison;
    if (argc!=7) { fprintf(stderr,"usage: test-direct mpp|x264 width height frames output.h264 reference.nv12\n"); return 2; }
    software=!strcmp(argv[1],"x264");
    full=getenv("XRDP_TEST_FULL_FRAME")!=NULL;
    w=atoi(argv[2]); h=atoi(argv[3]); frames=atoi(argv[4]);
    if(w<16||w>4096||h<16||h>4096||frames<1||frames>100000||(w&1)||(h&1)) return 2;
    stride=(w+31)&~15; rows=(h+15)&~15; /* deliberate source padding */
    input=malloc((size_t)stride*rows*3/2);
    reference=malloc((size_t)stride*rows*3/2);
    packet=malloc(16*1024*1024); times=calloc(frames,sizeof(double));
    encoded=fopen(argv[5],"wbx"); raw=fopen(argv[6],"wbx");
    if(!input||!reference||!packet||!times||!encoded||!raw) return 2;
    memset(reference,128,(size_t)stride*rows*3/2);
    log=log_config_init_for_console(LOG_LEVEL_INFO,NULL);
    log_start_from_param(log); log_config_free(log);
    encoder=software?xrdp_encoder_x264_create():xrdp_encoder_mpp_create();
    if(!encoder) return 3;
    poison.data=input; poison.bytes=(size_t)stride*rows*3/2; poison.calls=0;
    begin=milliseconds(CLOCK_MONOTONIC); cpu=milliseconds(CLOCK_PROCESS_CPUTIME_ID);
    for(f=0;f<frames;f++)
    {
        /* Full first frame then update just one rectangle. Poison all other
         * source pixels so a backend ignoring damage cannot pass decoding QA. */
        rect[0]=(f==0||full)?0:(short)((f*14)%(w-14)&~1);
        rect[1]=(f==0||full)?0:(short)((f*10)%(h-14)&~1);
        rect[2]=(f==0||full)?(short)w:(short)((w-rect[0]<128)?w-rect[0]:128);
        rect[3]=(f==0||full)?(short)h:(short)((h-rect[1]<96)?h-rect[1]:96);
        memset(input,0xe3,(size_t)stride*rows*3/2);
        for(y=rect[1];y<rect[1]+rect[3];y++) for(x=rect[0];x<rect[0]+rect[2];x++)
            input[y*stride+x]=reference[y*stride+x]=(unsigned char)(32+(x/8+y/8+f*3)%192);
        for(y=rect[1]/2;y<(rect[1]+rect[3])/2;y++) for(x=rect[0];x<rect[0]+rect[2];x+=2)
        {
            input[stride*rows+y*stride+x]=reference[stride*rows+y*stride+x]=(unsigned char)(96+(x/16+f)%64);
            input[stride*rows+y*stride+x+1]=reference[stride*rows+y*stride+x+1]=(unsigned char)(96+(y/8+f)%64);
        }
        bytes=16*1024*1024;
        if(getenv("XRDP_TEST_IDLE_AT") && f==atoi(getenv("XRDP_TEST_IDLE_AT")))
        {
            struct timespec idle={1,100000000};
            nanosleep(&idle,NULL);
        }
        times[f]=milliseconds(CLOCK_MONOTONIC);
        frame_cpu=milliseconds(CLOCK_PROCESS_CPUTIME_ID);
        if(!software && getenv("XRDP_TEST_RECOVERY_AT") &&
                f==atoi(getenv("XRDP_TEST_RECOVERY_AT")))
        {
            int tiny=16;
            if(getenv("XRDP_TEST_POISON_ON_READY"))
                xrdp_encoder_mpp_set_input_ready(encoder,poison_after_copy,&poison);
            rv=xrdp_encoder_mpp_encode(encoder,0,0,0,w,h,stride,rows,0,
                (const char*)input,rect,1,(char*)packet,&tiny,6,&flags);
            if(rv==0) { fprintf(stderr,"Expected output-capacity failure\n");return 7; }
            /* Model a producer reusing its source immediately after release. */
            memcpy(input,reference,(size_t)stride*rows*3/2);recovery_calls++;
        }
        if(!software && getenv("XRDP_TEST_POISON_ON_READY"))
            xrdp_encoder_mpp_set_input_ready(encoder, poison_after_copy, &poison);
        rv=(software?xrdp_encoder_x264_encode:xrdp_encoder_mpp_encode)
            (encoder,0,0,0,w,h,stride,rows,0,(const char *)input,rect,1,
             (char *)packet,&bytes,6,&flags);
        times[f]=milliseconds(CLOCK_MONOTONIC)-times[f]; sum+=times[f];
        encode_cpu+=milliseconds(CLOCK_PROCESS_CPUTIME_ID)-frame_cpu;
        if(rv||bytes<=0) { fprintf(stderr,"encode failed frame=%d rv=%d\n",f,rv); return 4; }
        if(fwrite(packet,1,bytes,encoded)!=(size_t)bytes) return 5;
        total+=bytes;
        for(y=0;y<h;y++) if(fwrite(reference+y*stride,1,w,raw)!=(size_t)w) return 5;
        for(y=0;y<h/2;y++) if(fwrite(reference+stride*rows+y*stride,1,w,raw)!=(size_t)w) return 5;
    }
    cpu=milliseconds(CLOCK_PROCESS_CPUTIME_ID)-cpu; begin=milliseconds(CLOCK_MONOTONIC)-begin;
    qsort(times,frames,sizeof(double),compare);
    printf("RESULT backend=%s width=%d height=%d frames=%d bytes=%" PRIu64
           " mean_ms=%.3f p50_ms=%.3f p95_ms=%.3f max_ms=%.3f wall_ms=%.3f cpu_ms=%.3f encode_cpu_ms=%.3f\n",
           argv[1],w,h,frames,total,sum/frames,times[(frames-1)/2],times[(frames*95+99)/100-1],times[frames-1],begin,cpu,encode_cpu);
    if(software) xrdp_encoder_x264_delete(encoder); else xrdp_encoder_mpp_delete(encoder);
    if(!software && getenv("XRDP_TEST_POISON_ON_READY") && poison.calls!=frames+recovery_calls)
    { fprintf(stderr,"Input-ready notification count %d != %d\n",poison.calls,frames+recovery_calls); return 6; }
    fclose(encoded); fclose(raw); free(input); free(reference); free(packet); free(times);
    log_end();
    return 0;
}
