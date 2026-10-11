/* SPDX-License-Identifier: Apache-2.0
 * Deterministic real X11 window motion and color stress. Test desktop only.
 * Absolute pacing drops missed animation ticks instead of building a queue.
 * Draw count is NOT the number of encoded or client-presented frames.
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static volatile sig_atomic_t stop;
static void stopped(int sig) { (void)sig; stop=1; }
static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec+t.tv_nsec/1e9;
}
static void paint(Display *d, Pixmap p, GC gc, int width, int height, int tick) {
    int r,c; char text[160];
    unsigned long bg=0x172b46, accent=0x29a6b8;
    if(tick>=0) {
        unsigned int k=(unsigned int)tick;
        bg=(((k*3)%112+32)<<16)|(((k*5)%112+32)<<8)|((k*7)%112+32);
        accent=0xffffff^bg;
    }
    XSetForeground(d,gc,bg); XFillRectangle(d,p,gc,0,0,width,height);
    XSetForeground(d,gc,accent); XFillRectangle(d,p,gc,0,0,width,68);
    XSetForeground(d,gc,0xffffff);
    snprintf(text,sizeof(text),"XRDP RK3576 / window motion + color / synthetic office / tick %d",tick);
    XDrawString(d,p,gc,24,40,text,(int)strlen(text));
    for(r=0;r<(height-140)/26;r++) {
        int y=90+r*26;
        XSetForeground(d,gc,(r&1)?0xf2f5f8:0xe0e7ee);
        XFillRectangle(d,p,gc,20,y,width-40,25);
        for(c=0;c<5;c++) {
            XSetForeground(d,gc,c==3?0x186b8c:0x263748);
            snprintf(text,sizeof(text),"Item %03d   %d.%02d",r+1,c*137,r*3);
            XDrawString(d,p,gc,30+c*(width-50)/5,y+18,text,(int)strlen(text));
        }
    }
    XSetForeground(d,gc,accent);
    XFillRectangle(d,p,gc,20,height-35,width-40,12);
}
int main(int argc,char **argv) {
    int duration=argc>1?atoi(argv[1]):60, fps=60, phase=-1;
    const char *mode=argc>2?argv[2]:"cycle";
    Display *d; Window w,root; Pixmap back; GC gc;
    int sw,sh,width,height,count=0,skipped=0;
    double start,finish;
    if(duration<1||duration>7200||
       (strcmp(mode,"cycle")&&strcmp(mode,"drag")&&strcmp(mode,"color")&&strcmp(mode,"mixed"))) return 2;
    d=XOpenDisplay(NULL); if(!d) return 3;
    sw=DisplayWidth(d,DefaultScreen(d));sh=DisplayHeight(d,DefaultScreen(d));
    width=sw*3/5; height=sh*2/3;
    if(width<400||height<240) { XCloseDisplay(d);return 4; }
    root=DefaultRootWindow(d);
    w=XCreateSimpleWindow(d,root,80,80,width,height,0,0,0x172b46);
    XStoreName(d,w,"XRDP CodecBridge - deterministic motion benchmark");
    XSelectInput(d,w,ExposureMask|StructureNotifyMask);
    Atom del=XInternAtom(d,"WM_DELETE_WINDOW",False);XSetWMProtocols(d,w,&del,1);
    XSizeHints hints={0};hints.flags=PMinSize|PMaxSize;
    hints.min_width=hints.max_width=width;hints.min_height=hints.max_height=height;
    XSetWMNormalHints(d,w,&hints);
    gc=XCreateGC(d,w,0,NULL);
    back=XCreatePixmap(d,root,width,height,DefaultDepth(d,DefaultScreen(d)));
    paint(d,back,gc,width,height,-1);XMapRaised(d,w);XSync(d,False);
    signal(SIGINT,stopped);signal(SIGTERM,stopped);
    start=now();finish=start+duration;
    printf("ANIMATION start=%.6f seconds=%d target_fps=%d screen=%dx%d window=%dx%d mode=%s\n",start,duration,fps,sw,sh,width,height,mode);fflush(stdout);
    while(!stop&&now()<finish) {
        double elapsed=now()-start;
        int tick=(int)(elapsed*fps), current;
        if(!strcmp(mode,"cycle")) current=((int)elapsed/20)%3;
        else current=!strcmp(mode,"drag")?0:!strcmp(mode,"color")?1:2;
        if(current!=phase) {
            phase=current;
            printf("PHASE mono=%.6f mode=%s\n",now(),phase==0?"drag":phase==1?"color":"mixed");fflush(stdout);
            if(phase==0) paint(d,back,gc,width,height,-1);
        }
        while(XPending(d)) {
            XEvent e;XNextEvent(d,&e);
            if(e.type==ClientMessage&&(Atom)e.xclient.data.l[0]==del)stop=1;
        }
        if(phase!=0)paint(d,back,gc,width,height,tick);
        if(phase!=1) {
            int x=20+(int)((sw-width-50)*(0.5+0.47*sin(elapsed*2.3)));
            int y=45+(int)((sh-height-100)*(0.5+0.47*sin(elapsed*3.1)));
            XMoveWindow(d,w,x,y);
        }
        XCopyArea(d,back,w,gc,0,0,width,height,0,0);
        XSync(d,False);count++;
        int next=(int)((now()-start)*fps)+1;
        if(next>tick+1)skipped+=next-tick-1;
        double target=start+(double)next/fps;
        struct timespec deadline={(time_t)target,(long)((target-floor(target))*1e9)};
        clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&deadline,NULL);
    }
    printf("ANIMATION done mono=%.6f draws=%d skipped_ticks=%d elapsed=%.3f\n",now(),count,skipped,now()-start);
    XFreePixmap(d,back);XFreeGC(d,gc);XDestroyWindow(d,w);XCloseDisplay(d);
    return 0;
}
