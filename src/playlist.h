/*
 * xinemf
 *
 * xinemf playlist API. 
 *
 * Playlist contains the "map" of the RunMotherfuckerRun world. 
 */
#ifndef _PLAYLIST_H_
#define _PLAYLIST_H_

#include <avformat.h>

typedef struct {
  /* common */
  int type; /* 'i', 's' or 'x', like the tag in a playlist */
  char * mrl, * abs_mrl;

  /* 
   * between 0.0 and 3.0 - running speed corresponding to the original movie speed 
   * for intersection - avg speed of streets
   */
  float nominal_speed;
  
  /* street */
  /* - cues - a list of playlist_frame2cue_t mappings */
  GList * cues;
  GList * cues_left;
  
  /* - intersections
   *   NULL - random intersection, 
   *   list (next!=NULL) - random intersection from the list
   *   list (next==NULL) - given intersection
   *
   *   right after parsing list contains names rather then entries. 
   */
  GList * intersections; 
  
  /* intersection */
  GList * exits;
  GList * exits_left;

  /* cached acess data */
  AVFormatContext * av_format_context;
  AVStream * video_stream;

  int cur_frame;  
} playlist_entry_t;

/*
 * exit descriptor.
 *
 * entry - pointer to exit street movie entry.  
 * x1 - horisontal coordinate of the snap center when snapping begins
 * y1 - frame number when snapping begins is called "y"
 * because it corresponds to y axis in Stock's coordinate system.
 * x2 - horisontal coordinate of the exit's center in a frame.  
 * y2 - frame number when to switch to exit's street movie.  
 */
typedef struct { 
  playlist_entry_t * entry; 
  int x1,y1,x2,y2; 
  int left, right;
  double slope_left, slope_right;
} playlist_exit_t;

typedef struct {
  unsigned int frame;
  unsigned int cue;
} playlist_frame2cue_t;

/* reset playlist to empty */
void playlist_reset();
/* 
 * open playlist. first resets the playlist.
 */
int playlist_open(const char * fname);

playlist_entry_t * playlist_get_i_movie();
playlist_entry_t * playlist_get_street1();
/*
 * choses random intersection from the list.
 * if list == NULL selects from full list of
 * intersections
 */
playlist_entry_t * playlist_get_random_intersection(GList * list);

playlist_entry_t * playlist_get_next_intersection(GList * list,double avvg_speed);

int playlist_seek(playlist_entry_t * entry,int pos,int whence);

/* get max width/height of the street movie */
void playlist_get_s_max(int * w, int * h);

#endif
