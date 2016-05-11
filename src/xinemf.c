#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include <glib.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>

#include <avformat.h>
#include <avcodec.h>

#include "xinemf.h"
#include "playlist.h"

enum {
  RMR_NONE,
  RMR_GO,
  RMR_BRIGHTNESS,
  RMR_X,
  RMR_SPEED,
};

typedef struct {
  int type;
  union {
    int go;
    double brightness;
    double x;
    double speed;
  };
} RMR_received_t;

static GAsyncQueue * RMR_events_queue = NULL;

/*
 * main module's API
 * should just post to async queue!
 */
void RMR_go_i() 
{
  RMR_received_t * rec = g_new0(RMR_received_t,1);
  rec->type = RMR_GO;
  rec->go = 'i';
  g_async_queue_push(RMR_events_queue,rec);
}
void RMR_go_x() 
{
  RMR_received_t * rec = g_new0(RMR_received_t,1);
  rec->type = RMR_GO;
  rec->go = 'x';
  g_async_queue_push(RMR_events_queue,rec);
}
  
void RMR_go_1() 
{
  RMR_received_t * rec = g_new0(RMR_received_t,1);
  rec->type = RMR_GO;
  rec->go = '1';
  g_async_queue_push(RMR_events_queue,rec);
}

void RMR_set_brightness(float brightness) 
{
  RMR_received_t * rec = g_new0(RMR_received_t,1);
  rec->type = RMR_BRIGHTNESS;
  rec->brightness = brightness;
  g_async_queue_push(RMR_events_queue,rec);
}

void RMR_set_x(float x) 
{
  RMR_received_t * rec = g_new0(RMR_received_t,1);
  rec->type = RMR_X;
  rec->x = x;
  g_async_queue_push(RMR_events_queue,rec);
}

void RMR_set_speed(float speed) 
{
  RMR_received_t * rec = g_new0(RMR_received_t,1);
  rec->type = RMR_SPEED;
  rec->speed = speed;
  g_async_queue_push(RMR_events_queue,rec);
}

/* internals */
/* config */
RMR_config_t RMR_config = {
  .playlist = NULL,
  .listen_port = -1,
  .cue_targets = NULL,
  .verbosity = 5,
  .snap_range1 = 100,
  .snap_range2 = 400,
  .inter_speed = 2.0,
  .centre_inter = 0,
};

static AVFrame * frame = NULL;
static GMainLoop * main_loop;
static playlist_entry_t * now_playing = NULL;
static GdkWindow * main_window;
/* speed control */
static double jam_fdt = 0.04; /* frame display time */
static int jam_repeat = 1; /* repeat each frame this much */
static int jam_step = 1; /* move that many frames after each frame */
static int old_step = 0; /* used by toggle_playback_pause() */
static double received_speed = 1.0;
static integrator_t * speed_integrator = NULL;
/* brightness */
static int brightness = 0; /* [-100;100] */
/* pan/crop */
static double pan_x = 0.0;
static GdkRectangle crop = { 0,0,0,0 };
/* player control */
static int jam_skip_current = 0; /* from keyboard command "skip" */
static int jam_step_mode = 0; /* manual stepping mode */
static int jam_dir = 1; /* playback direction */

static playlist_entry_t * last_x = NULL;

/* forward and lost */
static void play_entry(playlist_entry_t * entry);
double trunc(double x);
double ceil(double x);
void eq_MMX(unsigned char *dest, int dstride, unsigned char *src, int sstride,
		    int w, int h, int brightness, int contrast);
static void set_playback_speed(double speed);

void RMR_get_snap_ranges(int * sr1, int * sr2)
{
  *sr1 = RMR_config.snap_range1;
  *sr2 = RMR_config.snap_range2;
}

static void update_playback_speed()
{
  RMR_debug("rec spd: %g nom spd: %g\n",
      received_speed,
     (now_playing->type == 'x' ?
     RMR_config.inter_speed :
     now_playing->nominal_speed));
  set_playback_speed(received_speed / 
		     (now_playing->type == 'x' ?
		     RMR_config.inter_speed :
		     now_playing->nominal_speed));
}

static void play_entry(playlist_entry_t * entry)
{
  assert(entry!=NULL);

  now_playing = entry;
  playlist_seek(entry,0,SEEK_SET);
  update_playback_speed();
  now_playing->cues_left = now_playing->cues;
  now_playing->exits_left = now_playing->exits;
  if (now_playing->type == 'x')
    last_x = now_playing;
}

static void set_speed(double speed)
{
  received_speed = speed;
  update_playback_speed();
}

static void set_playback_speed(double speed)
{

  RMR_debug("SPEED SET TO %g\n",speed);
  if (speed == 0) {
    jam_fdt = 0.04;
    jam_step = 0;
  } else {
    jam_repeat = trunc(1.0/speed)?:1;
    jam_step = ceil(speed);
    jam_fdt = 0.04 * jam_step / (speed*jam_repeat);
  }
}

static void toggle_playback_pause()
{
  if (jam_step) {
    old_step = jam_step;
    jam_step = 0;
  } else
    jam_step = old_step;
}

static guint check_state(guint state, guint on, guint off)
{
  return ((state & on) == on) && ((state & off) == 0);
}

static void handle_key_press(GdkEventKey * ev)
{
  
  switch (ev->keyval) {
    case GDK_q:
    case GDK_Q:
      g_main_loop_quit(main_loop);
      break;
    case GDK_p:
      toggle_playback_pause();
      break;
    case GDK_d:
      jam_dir *= -1;
      break;
    case GDK_F9:
      if (jam_step_mode) {
        jam_step_mode = 0;
	jam_step = old_step;
      }
      break;
    case GDK_F10:
      if (!jam_step_mode) {
	old_step = jam_step;
        jam_step_mode = 1;
      }
      jam_step = 1;
      break;
    case GDK_1:
      if (check_state(ev->state,0,GDK_CONTROL_MASK|GDK_MOD1_MASK))
	set_speed(0.0);
      else if (check_state(ev->state,GDK_CONTROL_MASK,GDK_MOD1_MASK)) 
	brightness = -100;
      break;
    case GDK_2:
      if (check_state(ev->state,0,GDK_CONTROL_MASK|GDK_MOD1_MASK))
	set_speed(0.25);
      else if (check_state(ev->state,GDK_CONTROL_MASK,GDK_MOD1_MASK)) 
	brightness = -80;
      break;
    case GDK_3:
      if (check_state(ev->state,0,GDK_CONTROL_MASK|GDK_MOD1_MASK))
	set_speed(0.5);
      else if (check_state(ev->state,GDK_CONTROL_MASK,GDK_MOD1_MASK)) 
	brightness = -70;
      break;
    case GDK_4:
      if (check_state(ev->state,0,GDK_CONTROL_MASK|GDK_MOD1_MASK))
	set_speed(0.75);
      else if (check_state(ev->state,GDK_CONTROL_MASK,GDK_MOD1_MASK)) 
	brightness = -60;
      break;
    case GDK_5:
      if (check_state(ev->state,0,GDK_CONTROL_MASK|GDK_MOD1_MASK))
	set_speed(1.0);
      else if (check_state(ev->state,GDK_CONTROL_MASK,GDK_MOD1_MASK)) 
	brightness = -50;
      break;
    case GDK_6:
      if (check_state(ev->state,0,GDK_CONTROL_MASK|GDK_MOD1_MASK))
	set_speed(1.5);
      else if (check_state(ev->state,GDK_CONTROL_MASK,GDK_MOD1_MASK)) 
	brightness = -40;
      break;
    case GDK_7:
      if (check_state(ev->state,0,GDK_CONTROL_MASK|GDK_MOD1_MASK))
	set_speed(2.0);
      else if (check_state(ev->state,GDK_CONTROL_MASK,GDK_MOD1_MASK)) 
	brightness = -30;
      break;
    case GDK_8:
      if (check_state(ev->state,0,GDK_CONTROL_MASK|GDK_MOD1_MASK))
	set_speed(3.0);
      else if (check_state(ev->state,GDK_CONTROL_MASK,GDK_MOD1_MASK)) 
	brightness = -20;
      break;
    case GDK_9:
      if (check_state(ev->state,0,GDK_CONTROL_MASK|GDK_MOD1_MASK))
	set_speed(4.0);
      else if (check_state(ev->state,GDK_CONTROL_MASK,GDK_MOD1_MASK)) 
	brightness = -10;
      break;
    case GDK_0:
      if (check_state(ev->state,0,GDK_CONTROL_MASK|GDK_MOD1_MASK))
	set_speed(5.0);
      else if (check_state(ev->state,GDK_CONTROL_MASK,GDK_MOD1_MASK)) 
	brightness = 0;
      break;
    case GDK_space:
      play_entry(playlist_get_street1());
      break;
    case GDK_s:
      jam_skip_current = 1;
      break;
  }
}

static void handle_button(GdkEventButton * ev)
{
  int w,h;

  gdk_drawable_get_size(GDK_DRAWABLE(main_window),&w,&h);
  if (ev->type == GDK_BUTTON_PRESS) {
    switch (ev->button) {
      case 1:
	pan_x = 2.0*ev->x/(double)w - 1.0;
	break;
      case 2:
	brightness = - 100.0*ev->y/(double)h;
	break;
    }
  }
}

static void handle_motion(GdkEventMotion * ev)
{
  int w,h;

  gdk_drawable_get_size(GDK_DRAWABLE(main_window),&w,&h);
  if (ev->state & GDK_BUTTON1_MASK) {
    pan_x = 2.0*ev->x/(double)w - 1.0;
  }
  if (ev->state & GDK_BUTTON2_MASK) {
    brightness = - 100.0*ev->y/(double)h;
  }
}

static void handle_gdk_events(GdkEvent * ev)
{
  switch (ev->type) {
    case GDK_KEY_PRESS:
      handle_key_press(&ev->key);
      break;
    case GDK_CONFIGURE:
      jam_displayer_configure_cb(ev->configure.width,ev->configure.height);
      break;
    case GDK_BUTTON_PRESS:
      handle_button(&ev->button);
      break;
    case GDK_MOTION_NOTIFY:
      handle_motion(&ev->motion);
      break;
    default:
      break;
  }
}

static void jam_player_follow()
{
  switch(now_playing->type) {
    case 'i':
      play_entry(playlist_get_i_movie());
      break;
    case 's':
      /*
      play_entry(playlist_get_next_intersection(
	    now_playing->intersections,
	    RMR_integrator_current(speed_integrator)));
	    */
      play_entry(playlist_get_random_intersection(NULL));
      break;
    case 'x':
      /* FIXME */
      play_entry(playlist_get_i_movie());
      break;
    default:
      RMR_error("Panic: unknown movie type in jam_player_follow\n");
      break;
  }
}

static int pan_x_map(int leftest, int rightest) {
  int whole_width = rightest - leftest;
  int pan_width = whole_width - crop.width;
  int crop_x = leftest + pan_width*(pan_x+1.0)/2.0;
  if (crop_x < 0) crop_x = 0;

  return crop_x;
}

static void check_events()
{
  RMR_received_t * rec;

  while((rec = g_async_queue_try_pop(RMR_events_queue)) != NULL) {
    switch(rec->type) {
      case RMR_GO:
	switch (rec->go) {
	  case 'x':
	    if (last_x) {
	      play_entry(last_x);
	      break;
	    }
	    /* 
	     * no break here legally: if no last intersection - go
	     * to the first movie 
	     */
	  case '1':
	    play_entry(playlist_get_street1());
	    break;
	  case 'i':
	  default:
	    play_entry(playlist_get_i_movie());
	    break;
	}
	break;
      case RMR_BRIGHTNESS:
	brightness = (rec->brightness - 1.0)*100.0;
	break;
      case RMR_X:
	pan_x = rec->x;
	break;
      case RMR_SPEED:
	set_speed(rec->speed);
	break;
      default:
	RMR_warning("unknown event type\n");
    }
    g_free(rec);
  }
}

/*
 * return 0 if the frame shouldn't be displayed 
 */
static int jam_intersection_frame()
{
  static playlist_exit_t * snap_exit = NULL;
  static int snap_y;
  static double snap_slope_left = 0, snap_slope_right = 0;
  int center_x;
  int dy;
  
  if (snap_exit) {
    
    /* check if past an exit */
    if (now_playing->cur_frame >= snap_exit->y2) {
      RMR_debug("exiting %s->%s (frame: %i)\n",now_playing->mrl,snap_exit->entry->mrl,now_playing->cur_frame);
      play_entry(snap_exit->entry);
      snap_exit = NULL;
      return 0;
    }

    /* pan with snapping */
    dy = now_playing->cur_frame - snap_y;
    crop.x = pan_x_map(snap_slope_left*dy,
	now_playing->video_stream->codec.width + snap_slope_right*dy);

  } else {
    int snap_y1 = ((playlist_exit_t*)(now_playing->exits->data))->y1;
    /* pan without snapping */
    if (RMR_config.centre_inter && now_playing->cur_frame < snap_y1) {
      /* if intersections are entered centred, we add pan constraints */
      int constr = (now_playing->video_stream->codec.width - crop.width)/2;
      
      constr -= now_playing->cur_frame * constr / snap_y1;

      crop.x = pan_x_map(constr , now_playing->video_stream->codec.width - constr);

    } else {
      crop.x = pan_x_map(0, now_playing->video_stream->codec.width);
    }
    center_x = crop.x + crop.width/2;

    /* check if snapped now */
    while (now_playing->exits_left) {
      playlist_exit_t * exit = (playlist_exit_t*)now_playing->exits_left->data;
      
      if (now_playing->cur_frame > exit->y2) {
	/* passed safely */
	now_playing->exits_left = now_playing->exits_left->next;
	continue;
      }
      
      if (now_playing->cur_frame >= exit->y1) {
	dy = now_playing->cur_frame - exit->y1;
	if (!now_playing->exits_left->next || /* last exit always snaps */
	    (center_x > exit->left + exit->slope_left*dy &&
	    center_x < exit->right + exit->slope_right*dy)) {
	  snap_exit = exit;
	  snap_y = now_playing->cur_frame;
	  snap_slope_left = (double)(exit->x2 - crop.width/2) / (double)(exit->y2 - snap_y);
	  snap_slope_right = (double)(exit->x2 + crop.width/2 - now_playing->video_stream->codec.width) / (double)(exit->y2 - snap_y);
	  RMR_debug("snapping the %s exit\n",snap_exit->entry->mrl);
	} 
      }
      break;      
    }
  }

  return 1;
}

static void jam_check_cues()
{
  playlist_frame2cue_t * cue = NULL;

  assert(now_playing);
  
  while(now_playing->cues_left && 
      (cue = (playlist_frame2cue_t*)now_playing->cues_left->data) && 
      cue->frame <= now_playing->cur_frame) {
    /* send cue */
    RMR_debug("sending cue: %d\n",cue->cue);
    RMR_osc_send_cue(cue->cue);

    now_playing->cues_left = now_playing->cues_left->next;
  }
}


static int jam_player_iteration()
{
  static AVPacket av_packet;
  static int have_packet = 0;
  static int repeat_count = 0;
  
  int got_picture = 0;
  
  if (now_playing) {

    check_events();

    if (jam_skip_current) {
      jam_player_follow();
      jam_skip_current = 0;
      return TRUE;
    }

    /* read packet if necessary */
    if (!have_packet || (jam_step && (repeat_count>=jam_repeat))) {
      if (have_packet) {
	av_free_packet(&av_packet);
	have_packet = 0;
      }
      if (jam_step*jam_dir != 1) {
	playlist_seek(now_playing,(jam_step*jam_dir)-1,SEEK_CUR);
      }
      if (av_read_frame(now_playing->av_format_context,&av_packet) != 0) {
	/* End of stream */
	repeat_count = 0;
	jam_player_follow();
	return TRUE;
      }
      /* got packet */
      now_playing->cur_frame++;
      repeat_count = 0;
      have_packet = 1;

      if (jam_step_mode)
	jam_step = 0;
    }

    /* decode packet into frame */
    if (avcodec_decode_video(&now_playing->video_stream->codec,frame, 
	  &got_picture,av_packet.data,av_packet.size) < 0) {
      RMR_error("error decoding frame\n");
      exit(1);
    }

    if (got_picture) {
      
      if (now_playing->type == 'x') {
	/* do panning / snapping or even exiting */
	if (!jam_intersection_frame()) {
	  /* motherfucker has exited: burn the packet to avoid showing the last frame */
	  av_free_packet(&av_packet);
	  have_packet = 0;
	  /* return without displaying the frame */
	  return TRUE;
	}
      } else {
	crop.x = pan_x_map(0, now_playing->video_stream->codec.width);
      }

      /* check if passed any cues */
      jam_check_cues();
	  
      /* process picture */
      eq_MMX(frame->data[0],
	  now_playing->video_stream->codec.width,
	  frame->data[0],
	  now_playing->video_stream->codec.width,
	  now_playing->video_stream->codec.width,
	  now_playing->video_stream->codec.height,
	  brightness,
	  0);
      
      /* display picture */
      jam_frame_display(frame,jam_fdt,&crop);
      repeat_count++;
    }

  } else {
    return FALSE;
  }
  return TRUE;
}

static int integrate_received_speed()
{
  RMR_integrator_add(speed_integrator,received_speed);
  return TRUE; /* keep on calling me */
}

static void set_no_cursor(GdkWindow * w)
{
#define cursor1_width 16
#define cursor1_height 16
  static unsigned char cursor1_bits[32] = {};
  static unsigned char cursor1mask_bits[32] = {};
  GdkPixmap *source, *mask;
  GdkColor fg = { };
  GdkColor bg = { }; 
  static GdkCursor * cursor_none = NULL;
  
  if (!cursor_none) {
    source = gdk_bitmap_create_from_data (NULL, cursor1_bits,
					  cursor1_width, cursor1_height);
    mask = gdk_bitmap_create_from_data (NULL, cursor1mask_bits,
					cursor1_width, cursor1_height);
    cursor_none = gdk_cursor_new_from_pixmap (source, mask, &fg, &bg, 8, 8);
    gdk_pixmap_unref (source);
    gdk_pixmap_unref (mask);
  }
  gdk_window_set_cursor(w,cursor_none);
}

int main(int argc,char * argv[])
{
  int s_max_w,s_max_h;
  GdkWindowAttr win_attr = {
    .title = "RunMotherFuckerRun",
    .event_mask = GDK_KEY_PRESS_MASK|GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_BUTTON_MOTION_MASK,
    .x = 0, .y = 0,
    .wclass = GDK_INPUT_OUTPUT,
    .window_type = GDK_WINDOW_TOPLEVEL,
  };
 
  av_register_all();
  av_log_set_level(0); /* not interested */
  
  /* 
   * has to parse BEFORE gdk init (to do --help without DISPLAY) 
   * gdk heeft pech
   */
  RMR_parse_cmdline(argc,argv,&RMR_config);
  RMR_set_verbosity(RMR_config.verbosity);
  
  g_thread_init(NULL);
  gdk_threads_init();
  gdk_init(&argc,&argv);
  
  /* my events queue FIXME ref with osc / unref at exit */
  RMR_events_queue = g_async_queue_new();
  RMR_osc_start(&RMR_config);

  
  /* create the main window */
  win_attr.width = gdk_screen_width();
  win_attr.height = gdk_screen_height();
  main_window = gdk_window_new(NULL,&win_attr,GDK_WA_TITLE|GDK_WA_X|GDK_WA_Y);
  set_no_cursor(main_window);
  /* make this configurable
   * gdk_window_set_type_hint(main_window,GDK_WINDOW_TYPE_HINT_SPLASHSCREEN); */
  gdk_window_show(main_window);
  gdk_event_handler_set((GdkEventFunc)handle_gdk_events,NULL,NULL);

  /* open playlist */
  playlist_open(RMR_config.playlist);
  play_entry(playlist_get_i_movie());

  /* prepare jamplayer */
  playlist_get_s_max(&s_max_w,&s_max_h);
  crop.width = s_max_w;
  crop.height = s_max_h;
  jam_init_displayer(main_window,s_max_w,s_max_h);
  frame = avcodec_alloc_frame();

  /* speed integrator */
  speed_integrator = RMR_integrator_new(SPEED_ITG_SIZE);
  
  /* prepare the main loop */
  main_loop = g_main_loop_new(NULL,FALSE);
  g_idle_add((GSourceFunc)jam_player_iteration,NULL); 
  g_timeout_add(SPEED_ITG_INTERVAL,(GSourceFunc)integrate_received_speed,NULL);

  srand(time(NULL));
  /* start main loop */
  g_main_loop_run(main_loop);
  
  /* cleanup FIXME: move it to at_exit */
  RMR_debug("Releasing resources\n");
  av_free(frame);
  return 0;
}
