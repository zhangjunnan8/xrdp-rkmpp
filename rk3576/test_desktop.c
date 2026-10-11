/* SPDX-License-Identifier: Apache-2.0
 * Synthetic office-like X11 scene and headless FreeRDP window resize utility.
 * Run only on the isolated test display, never the user's active desktop.
 */
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double seconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec+t.tv_nsec/1e9;
}
int main(int argc, char **argv)
{
    Display *d;
    Window root, w;
    GC gc;
    int frame=0, duration=30;
    double start;
    if(argc>1 && (!strcmp(argv[1],"resize-client") || !strcmp(argv[1],"close-client")))
    {
        Window parent,*children;
        unsigned int count,i;
        int resize=!strcmp(argv[1],"resize-client");
        if(argc!=(resize?4:2)) return 2;
        d=XOpenDisplay(NULL); if(!d) return 3;
        root=DefaultRootWindow(d);
        if(!XQueryTree(d,root,&root,&parent,&children,&count)) return 4;
        for(i=0;i<count;i++)
        {
            char *name=NULL;
            XFetchName(d,children[i],&name);
            if(name && strstr(name,"FreeRDP"))
            {
                if(resize) XResizeWindow(d,children[i],atoi(argv[2]),atoi(argv[3]));
                else
                {
                    XEvent event;
                    memset(&event,0,sizeof(event));
                    event.xclient.type=ClientMessage;
                    event.xclient.window=children[i];
                    event.xclient.message_type=XInternAtom(d,"WM_PROTOCOLS",False);
                    event.xclient.format=32;
                    event.xclient.data.l[0]=XInternAtom(d,"WM_DELETE_WINDOW",False);
                    event.xclient.data.l[1]=CurrentTime;
                    XSendEvent(d,children[i],False,NoEventMask,&event);
                }
                XSync(d,False); printf("%s %s\n",resize?"Resized":"Closed",name);
                XFree(name); XFree(children); XCloseDisplay(d); return 0;
            }
            if(name) XFree(name);
        }
        XFree(children); XCloseDisplay(d); return 5;
    }
    if(argc>1) duration=atoi(argv[1]);
    d=XOpenDisplay(NULL); if(!d||duration<1||duration>3600) return 2;
    root=DefaultRootWindow(d);
    w=XCreateSimpleWindow(d,root,30,30,1200,780,0,0,0xffffff);
    XStoreName(d,w,"RK3576 MPP synthetic office test");
    XSelectInput(d,w,ExposureMask|StructureNotifyMask);
    gc=XCreateGC(d,w,0,NULL);
    XMapRaised(d,w);
    start=seconds();
    while(seconds()-start<duration)
    {
        int row,col;
        char text[160];
        struct timespec wait={0,16666667};
        while(XPending(d)) { XEvent event; XNextEvent(d,&event); }
        XSetForeground(d,gc,0xf6f7fa); XFillRectangle(d,w,gc,0,0,1200,780);
        XSetForeground(d,gc,0x183b60); XFillRectangle(d,w,gc,0,0,1200,70);
        XSetForeground(d,gc,0xffffff);
        snprintf(text,sizeof(text),"RK3576 / direct MPP / office scrolling test / frame %d",frame);
        XDrawString(d,w,gc,25,35,text,strlen(text));
        for(row=0;row<25;row++)
        {
            int y=95+row*25-(frame%25);
            XSetForeground(d,gc,(row&1)?0xffffff:0xe5eaf0);
            XFillRectangle(d,w,gc,20,y,1150,24);
            for(col=0;col<5;col++)
            {
                XSetForeground(d,gc,col==3?0x145cad:0x152331);
                snprintf(text,sizeof(text),"Row %04d   Value %d.%02d",row+frame/25,col*137,row*3);
                XDrawString(d,w,gc,30+col*225,y+17,text,strlen(text));
            }
        }
        XSetForeground(d,gc,0xe44848); XFillRectangle(d,w,gc,(frame*7)%1100,725,70,25);
        XSetForeground(d,gc,0x008844);
        strcpy(text,"Small text: ABC xyz 0123456789 | thin lines / colors / scrolling");
        XDrawString(d,w,gc,20,705,text,strlen(text));
        XFlush(d); frame++; nanosleep(&wait,NULL);
    }
    printf("DESKTOP frames=%d elapsed_seconds=%.3f\n",frame,seconds()-start);
    XFreeGC(d,gc); XDestroyWindow(d,w); XCloseDisplay(d); return 0;
}
