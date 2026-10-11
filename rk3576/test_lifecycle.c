/* SPDX-License-Identifier: Apache-2.0 */
#include <config_ac.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "xrdp_encoder_mpp.h"
static int checks;
#define CHECK(expr) do { checks++; if(!(expr)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr); exit(1); } } while(0)
static int fd_count(void)
{
    DIR *d=opendir("/proc/self/fd");
    int n=0;
    CHECK(d!=NULL);
    while(readdir(d)) n++;
    closedir(d);
    return n;
}
static int rss_kb(void)
{
    FILE *f=fopen("/proc/self/status","r");
    char line[256]; int rss=0;
    CHECK(f!=NULL);
    while(fgets(line,sizeof(line),f)) if(sscanf(line,"VmRSS: %d kB",&rss)==1) break;
    fclose(f); return rss;
}
static int has_idr(const unsigned char *p, int length)
{
    int i;
    for(i=0;i+4<length;i++)
        if(!p[i]&&!p[i+1]&&p[i+2]==1&&(p[i+3]&31)==5) return 1;
    return 0;
}
int main(void)
{
    unsigned char input[336*256*3/2], output[1024*1024];
    int cycle, f, flags, bytes, baseline_fd=0, baseline_rss=0;
    struct log_config *log=log_config_init_for_console(LOG_LEVEL_WARNING,NULL);
    log_start_from_param(log); log_config_free(log);
    memset(input,128,sizeof(input));
    setenv("XRDP_MPP_STRICT","1",1);
    for(cycle=0;cycle<80;cycle++)
    {
        void *e=xrdp_encoder_mpp_create();
        CHECK(e!=NULL);
        for(f=0;f<8;f++)
        {
            int w=f<4?320:318, h=f<4?240:242;
            short rect[4]={0,0,(short)w,(short)h};
            input[f]=160;
            bytes=sizeof(output);
            CHECK(xrdp_encoder_mpp_encode(e,0,0,0,w,h,336,256,0,(const char *)input,
                rect,1,(char *)output,&bytes,6,&flags)==0);
            CHECK(bytes>0);
            CHECK((flags&1)==((f==0||f==4)?1:0));
            if(f==2)
            {
                rect[0]=-1; bytes=sizeof(output);
                CHECK(xrdp_encoder_mpp_encode(e,0,0,0,w,h,336,256,0,(const char *)input,
                    rect,1,(char *)output,&bytes,6,NULL)!=0);
            }
        }
        /* Too-small destination poisons hardware references; strict mode must
         * fail, and a geometry change must be able to start a clean stream. */
        {
            short rect[4]={0,0,318,242};
            bytes=1;
            CHECK(xrdp_encoder_mpp_encode(e,0,0,0,318,242,336,256,0,(const char *)input,
                rect,1,(char *)output,&bytes,6,NULL)!=0);
            bytes=sizeof(output);
            CHECK(xrdp_encoder_mpp_encode(e,0,0,0,318,242,336,256,0,(const char *)input,
                rect,1,(char *)output,&bytes,6,NULL)!=0);
            rect[2]=320; rect[3]=240; bytes=sizeof(output);
            CHECK(xrdp_encoder_mpp_encode(e,0,0,0,320,240,336,256,0,(const char *)input,
                rect,1,(char *)output,&bytes,6,NULL)==0);
        }
        CHECK(xrdp_encoder_mpp_delete(e)==0);
        if(cycle==9) { baseline_fd=fd_count(); baseline_rss=rss_kb(); }
        if(cycle>9) CHECK(fd_count()==baseline_fd);
    }
    printf("LIFECYCLE checks=%d cycles=80 fd_baseline=%d fd_final=%d rss_baseline_kb=%d rss_final_kb=%d\n",
           checks,baseline_fd,fd_count(),baseline_rss,rss_kb());
    CHECK(rss_kb()-baseline_rss<16384);
    /* Real MPP -> real x264 transition after an output capacity failure. */
    {
        void *e;
        short rect[4]={0,0,320,240};
        setenv("XRDP_MPP_STRICT","0",1);
        e=xrdp_encoder_mpp_create(); CHECK(e!=NULL);
        bytes=sizeof(output);
        CHECK(xrdp_encoder_mpp_encode(e,0,0,0,320,240,336,256,0,(const char *)input,
            rect,1,(char *)output,&bytes,6,NULL)==0);
        bytes=1;
        CHECK(xrdp_encoder_mpp_encode(e,0,0,0,320,240,336,256,0,(const char *)input,
            rect,1,(char *)output,&bytes,6,NULL)!=0);
        bytes=sizeof(output);
        CHECK(xrdp_encoder_mpp_encode(e,0,0,0,320,240,336,256,0,(const char *)input,
            rect,1,(char *)output,&bytes,6,NULL)==0);
        CHECK(has_idr(output,bytes));
        CHECK(xrdp_encoder_mpp_delete(e)==0);
        printf("FALLBACK lost-output recovery starts with IDR; total_checks=%d\n",checks);
    }
    log_end(); return 0;
}
