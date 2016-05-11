#ifndef _XINEMF_H_
#define _XINEMF_H_

#include <glib.h>
#include <gdk/gdk.h>
#include <avcodec.h>

/* how many values to remember in integrator */
#define SPEED_ITG_SIZE 20
/* how often to call integrator (ms) */
#define SPEED_ITG_INTERVAL 100

/*
 * integrator will calculate the average value for last SPEED_ITG_SIZE * SPEED_ITG_INTERVAL 
 */

/**************************************************************************
 * configuration
 **************************************************************************/
typedef struct {
  int min_cue, max_cue;
  char * host;
  int port;
  void * htmsock;
} RMR_cue_target_t;

typedef struct {
  /* playlist file name */
  char * playlist;
  /* port to listen to osc messages, negative value -> no networking */
  int listen_port; 
  /* port and host to send cues to */
  GList * cue_targets;
  /* verbosity level */
  int verbosity;
  /* snap range (in pixels) */
  int snap_range1, snap_range2;
  /* intersection speed (nominal) */
  float inter_speed;
  /* allways start intersection movies centred */
  int centre_inter;
} RMR_config_t;

/* parse command line */
void RMR_parse_cmdline(int argc, char *argv[], RMR_config_t * config);

/**************************************************************************
 * player state
 **************************************************************************/
/* jump to intro movie */
void RMR_go_i();
/* jump to the first street */
void RMR_go_1();
/* jump to the last intersection */
void RMR_go_x();
/* brightness [0.0;1.0] */
void RMR_set_brightness(float brightness); 

/* 
 * X (participant X coordinate) only affects the player
 * when the movie is wider then RMR_street_movie_width
 * 
 * [-1.0;1.0] ?
 */
void RMR_set_x(float x); 

/* playback speed [0.0;oo] */
void RMR_set_speed(float speed); 

void RMR_get_snap_ranges(int * sr1, int * sr2);

/**************************************************************************
 * displayer
 **************************************************************************/

void jam_init_displayer(GdkWindow * main_window,int outw,int outh);
void jam_displayer_configure_cb(int w,int h);
void jam_frame_display(AVFrame * frame, double fdt, GdkRectangle * crop);
int jam_frame_get_buffer(AVCodecContext * c, AVFrame * av_frame);
void jam_frame_release_buffer(AVCodecContext * c, AVFrame * av_frame);
    
/**************************************************************************
 * integrator
 **************************************************************************/
typedef struct integrator_t {
  int table_len;
  double * values;
  double * dts;
  int cur_len;
  int cur_idx;
  GTimer * timer;
} integrator_t;

integrator_t * RMR_integrator_new(int table_len);
void RMR_integrator_add(integrator_t * itg,double val);
double RMR_integrator_current(integrator_t * itg);

/**************************************************************************
 * OSC
 **************************************************************************/

void RMR_osc_start(RMR_config_t * config);
void RMR_osc_send_cue(int cue);
void RMR_osc_stop();

/**************************************************************************
 * Debug
 **************************************************************************/

void RMR_set_verbosity(int v);
void RMR_info(char * format,...);
void RMR_warning(char * format,...);
void RMR_error(char * format,...);
void RMR_debug(char * format,...);
void RMR_info0(char * format,...);
void RMR_warning0(char * format,...);
void RMR_error0(char * format,...);
void RMR_debug0(char * format,...);

#endif
