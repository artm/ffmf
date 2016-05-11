#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include <glib.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include <X11/Xlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>

#include <avcodec.h>

#include "xinemf.h"

#define EDGE_WIDTH 16
#define MKTAG(a,b,c,d) (a | (b << 8) | (c << 16) | (d << 24))
#define ALIGN(x, a) (((x)+(a)-1)&~((a)-1))

static Display * dpy = NULL;
static int xv_port = -1;
  
static Drawable main_win_drawable;
static GC main_win_gc;
static GTimer * frame_timer;

static int main_win_width, main_win_height;
static GdkRectangle output_area;

typedef struct jam_frame_t {
  int width, height;
  int fmt;
  XvImage * xvimage;
  XShmSegmentInfo shminfo;
} jam_frame_t;

static jam_frame_t theFrame;

static void jam_dispose_xvimage(jam_frame_t * frame);
/* couldn't find the header ;) */
void avcodec_align_dimensions(AVCodecContext *s, int *width, int *height);
double round(double x);

void jam_init_displayer(GdkWindow * main_window,int outw,int outh)
{
  GdkGC * gdk_gc = NULL;
  /* find out the Xv stuff */
  dpy = XOpenDisplay(NULL);
  if (dpy) {
    unsigned int xv_version, xv_release;
    unsigned int xv_request_base, xv_event_base, xv_error_base;

    if (XvQueryExtension(dpy, &xv_version, &xv_release, 
	  &xv_request_base, &xv_event_base, 
	  &xv_error_base) != Success) {
      RMR_error("No Xv extension found!\n");
      exit(1);
    }
    RMR_debug("Xv extension v%d.%d found\n",xv_version,xv_release);

    if (XShmQueryExtension(dpy) != True) {
      RMR_error("No XSmh extension found!\n");
      exit(1);
    }
    RMR_debug("XShm extension found\n");
  } else {
    RMR_error("Can't open display\n");
  }
  main_win_drawable = gdk_x11_drawable_get_xid(GDK_DRAWABLE(main_window));
  gdk_gc = gdk_gc_new(GDK_DRAWABLE(main_window));
  main_win_gc = gdk_x11_gc_get_xgc(gdk_gc);
  gdk_drawable_get_size(GDK_DRAWABLE(main_window),&main_win_width,&main_win_height);
  /* squeeze the output area into the window */
  {
    double xr = (double)main_win_width/(double)outw, yr = (double)main_win_height/(double)outh;
    double ratio = MIN(xr,yr);
    output_area.width = ratio*outw;
    output_area.height = ratio*outh;
    output_area.x = (main_win_width - output_area.width) / 2;
    output_area.y = (main_win_height - output_area.height) / 2;
    RMR_debug("Window sizes: %ix%i\n", main_win_width, main_win_height);
    RMR_debug("Requested output size: %ix%i\n", outw, outh);
    RMR_debug("Calculated output area: %ix%i+%i+%i\n", output_area.width, output_area.height, output_area.x, output_area.y);
  }

  /* find our Xv adaptor / port */
  {
    int num_adaptors,i;
    XvAdaptorInfo * adaptor_infos;
    
    XvQueryAdaptors(dpy,main_win_drawable,&num_adaptors,&adaptor_infos);
    
    for(i=0;i<num_adaptors;i++) {
      if (strstr(adaptor_infos[i].name,"Blitter")) {
	int p;
	for (p=0;p<adaptor_infos[i].num_ports;p++) {
	  if (XvGrabPort(dpy,adaptor_infos[i].base_id+p,CurrentTime) == Success) {
	    xv_port = adaptor_infos[i].base_id+p;
	    break;
	  }
	}
      }
    }
    XvFreeAdaptorInfo(adaptor_infos);
    assert(xv_port>0);
    RMR_debug("Selected xv port: %i\n",xv_port);
  }

  frame_timer = g_timer_new();
}

void update_xvimage(AVFrame * av_frame,jam_frame_t * frame)
{
  AVPicture dst, *src = (AVPicture*)av_frame;
  int dst_pix_fmt = PIX_FMT_YUV422, src_pix_fmt = frame->fmt;
  
  dst.data[0] = frame->xvimage->data;
  dst.linesize[0] = frame->xvimage->pitches[0];
  img_convert(&dst,dst_pix_fmt,src,src_pix_fmt,frame->width,frame->height);
}

void jam_frame_display(AVFrame * av_frame, double fdt, GdkRectangle * crop)
{
  jam_frame_t * frame = (jam_frame_t*)av_frame->opaque;
  static double jam_displayer_fdt = -1;
  
  if (frame && frame->xvimage) {

    if (jam_displayer_fdt > 0.0) {
      static double compensate = 0.0;
      double wait_for = jam_displayer_fdt - g_timer_elapsed(frame_timer,NULL) - compensate;
      
      if (wait_for > 0.0) {
	unsigned long usec = (unsigned long)round(wait_for * 1e6);
	g_usleep(usec);
	compensate = g_timer_elapsed(frame_timer,NULL) - wait_for;
      }
    }
  
    if (av_frame->type != FF_BUFFER_TYPE_USER) 
      update_xvimage(av_frame,frame);
    
    /* actually display the frame */
    XvShmPutImage(dpy,xv_port,main_win_drawable,main_win_gc,
	frame->xvimage,
	crop->x,crop->y,crop->width,crop->height,
	output_area.x, output_area.y,
	output_area.width, output_area.height,
	False);
    XSync(dpy,False); 

    /* restart timer and reset frame display time */
    jam_displayer_fdt = fdt;
    g_timer_start(frame_timer);    
  }
}

void jam_displayer_configure_cb(int w,int h)
{
  main_win_width = w;
  main_win_height = h;
}

int jam_frame_get_buffer(AVCodecContext * c, AVFrame * av_frame)
{
  jam_frame_t * frame = &theFrame;
  int w = c->width, h = c->height;
  int fmt = c->pix_fmt;
  int res = 0;
  
  if (frame->width != w || frame->height != h || frame->fmt != fmt) {
    int xv_fmt;
    
    if (frame->xvimage) 
      jam_dispose_xvimage(frame);

    switch(fmt) {
      case PIX_FMT_YUV420P:
	xv_fmt = MKTAG('Y','V','1','2');
	break;
      case PIX_FMT_YUV422P:
      case PIX_FMT_YUV422:
	xv_fmt = MKTAG('Y','U','Y','2');
	break;
      default:
	RMR_error("unsupported format\n");
	abort();
    }
    
    avcodec_align_dimensions(c,&w,&h);
    
    frame->xvimage = XvShmCreateImage(dpy,xv_port,xv_fmt, 0, w, h, &frame->shminfo);
    frame->shminfo.shmid = shmget(IPC_PRIVATE, frame->xvimage->data_size, IPC_CREAT | 0777);
    assert(frame->shminfo.shmid != -1);
    frame->shminfo.shmaddr = shmat(frame->shminfo.shmid,0,0);
    assert(frame->shminfo.shmaddr!=NULL && frame->shminfo.shmaddr!=(void*)-1);
    frame->shminfo.readOnly = False;
    frame->xvimage->data = frame->shminfo.shmaddr;
    XShmAttach(dpy,&frame->shminfo);
    XSync(dpy,False);
    /* FIXME i detach twice, like xine, but i'm not sure that's correct */
    shmctl(frame->shminfo.shmid,IPC_RMID, 0);
    shmctl(frame->shminfo.shmid,IPC_RMID, 0);
    frame->shminfo.shmid = -1;
    frame->width = c->width;
    frame->height = c->height;
    frame->fmt = fmt;
  }

 
  switch (frame->fmt) {
    case PIX_FMT_YUV420P:
      av_frame->data[0] = av_frame->base[0] = frame->xvimage->data + frame->xvimage->offsets[0];
      av_frame->data[2] = av_frame->base[1] = frame->xvimage->data + frame->xvimage->offsets[1];
      av_frame->data[1] = av_frame->base[2] = frame->xvimage->data + frame->xvimage->offsets[2];
      av_frame->linesize[0] = frame->xvimage->pitches[0];
      av_frame->linesize[2] = frame->xvimage->pitches[1];
      av_frame->linesize[1] = frame->xvimage->pitches[2];
      av_frame->type = FF_BUFFER_TYPE_USER;
      break;
    case PIX_FMT_YUV422:
      av_frame->data[0] = av_frame->base[0] = frame->xvimage->data + frame->xvimage->offsets[0];
      av_frame->linesize[0] = frame->xvimage->pitches[0];
      av_frame->type = FF_BUFFER_TYPE_USER;
      break;
    case PIX_FMT_YUV422P:
      res = avcodec_default_get_buffer(c,av_frame);
  }
  av_frame->opaque = frame;
 
  return res;
}

void jam_frame_release_buffer(AVCodecContext * c, AVFrame * av_frame)
{
  if (av_frame->type == FF_BUFFER_TYPE_USER) {
    av_frame->data[0] = av_frame->data[1] = av_frame->data[2] = 0;
    av_frame->base[0] = av_frame->base[1] = av_frame->base[2] = 0;
    av_frame->opaque = NULL;
  } else 
    avcodec_default_release_buffer(c,av_frame);
}

static void jam_dispose_xvimage(jam_frame_t * frame)
{
  if (frame->xvimage) {
    XShmDetach(dpy,&frame->shminfo);
    XFree(frame->xvimage);
    shmdt(frame->shminfo.shmaddr);
    if (frame->shminfo.shmid >= 0) {
      shmctl(frame->shminfo.shmid, IPC_RMID, NULL);
    }
    frame->shminfo.shmid = -1;
    frame->xvimage = NULL;
    frame->width = 0;
    frame->height = 0;
  }
}

