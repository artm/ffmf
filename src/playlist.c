/*
 * xinemf
 *
 * playlist - operations on a play list.
 * 
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>
#include <assert.h>

double fabs(double x);

#include <glib.h>

#include <avcodec.h>

#include "xinemf.h"
#include "playlist.h"

GList * playlist_parse(const char * fname);

static GList * playlist = NULL, * x_list = NULL;
static playlist_entry_t * i_movie = NULL, * street1 = NULL;
static GHashTable * s_hasj = NULL, * x_hasj = NULL;

static void playlist_entry_free(playlist_entry_t * entry)
{
  if (!entry) return;

  /*
   * don't have to free mrl because it points somewhere inside abs_mrl 
   */
  g_free(entry->abs_mrl);
  if (entry->intersections) 
    g_list_free(entry->intersections);
  if (entry->cues) {
    g_list_foreach(entry->cues,(GFunc)g_free,NULL);
    g_list_free(entry->cues);
  }
  if (entry->exits) {
    g_list_foreach(entry->exits,(GFunc)g_free,NULL);
    g_list_free(entry->exits);
  }
  if (entry->av_format_context)
    av_close_input_file(entry->av_format_context);
  g_free(entry);
}

void playlist_reset()
{
  i_movie = NULL;
  street1 = NULL;
  if (s_hasj) g_hash_table_destroy(s_hasj);
  if (x_hasj) g_hash_table_destroy(x_hasj);
  if (x_list) g_list_free(x_list);

  if (playlist) {
    g_list_foreach(playlist,(GFunc)playlist_entry_free,NULL);
    g_list_free(playlist);
  }

  s_hasj = g_hash_table_new(g_str_hash, g_str_equal);
  x_hasj = g_hash_table_new(g_str_hash, g_str_equal);
  x_list = NULL;
}

static void hasj_entry(playlist_entry_t * entry)
{
  switch(entry->type) {
    case 'i':
      i_movie = entry;
      break;
    case 's':
      g_hash_table_insert(s_hasj,entry->mrl,entry);
      if (!street1) street1 = entry;
      break;
    case 'x':
      g_hash_table_insert(x_hasj,entry->mrl,entry);
      break;
    default:
      RMR_error("unknown playlist entry type\n");
      break;
  }
}

/* return true to remove intersection */
static gboolean x_resolve(char * mrl, playlist_entry_t * entry)
{
  gboolean res = FALSE;
  GList * iter = entry->exits;
  int snap_range1, snap_range2;

  RMR_get_snap_ranges(&snap_range1, &snap_range2);
  
  while(iter) {
    playlist_exit_t * exit = (playlist_exit_t*)iter->data;
    char * mrl = (char*)exit->entry;
  
    exit->entry = g_hash_table_lookup(s_hasj,mrl);
    
    if (!exit->entry) {
      RMR_error("can't resolve exit reference %s, removing entry\n",mrl);
      res = TRUE;
      /* have to continue this loop to free memory allocated for MRLs */
    } else {
      /* calculate the snapping helpers */
      exit->left = exit->x1 - snap_range1;
      exit->right = exit->x1 + snap_range1;
      exit->slope_left = (double)(exit->x2 - snap_range2 - exit->left) / (double)(exit->y2 - exit->y1);
      exit->slope_right = (double)(exit->x2 + snap_range2 - exit->right) / (double)(exit->y2 - exit->y1);
    }

    g_free(mrl);
    iter = iter->next;
  }

  if (res) {
    g_list_remove(playlist,entry);
    playlist_entry_free(entry);
  } else
    x_list = g_list_append(x_list,entry);
  
  return res;
}

static void s_resolve(char * mrl, playlist_entry_t * entry)
{
  GList * iter = entry->intersections;

  if (!iter) 
    RMR_warning("street %s has empty intersections list\n",mrl);
  
  while(iter) {
    GList * next = iter->next;
    char * mrl = (char*)iter->data;

    iter->data = g_hash_table_lookup(x_hasj,mrl);

    if (!iter->data) {
      RMR_error("can't resolve intersection reference %s, removing reference\n",mrl);
      entry->intersections = g_list_delete_link(entry->intersections,iter);
    }
    
    g_free(mrl);
    iter = next;
  }
}

static int s_max_w = 0, s_max_h = 0;

void playlist_get_s_max(int * w, int * h)
{
  *w = s_max_w;
  *h = s_max_h;
}

static int cache_av_context(playlist_entry_t * entry)
{
  char * averror_string(int averror)
  {
    switch (averror) {
      case 0:
	return "no AVERROR [ok]";
      case AVERROR_IO:
	return "AVERROR_IO";
      case AVERROR_NUMEXPECTED:
	return "AVERROR_NUMEXPECTED";
      case AVERROR_INVALIDDATA:
	return "AVERROR_INVALIDDATA";
      case AVERROR_NOMEM:
	return "AVERROR_NOMEM";
      case AVERROR_NOFMT:
	return "AVERROR_NOFMT";
      case AVERROR_UNKNOWN:
      default:
	return "AVERROR_UNKNOWN";
    }
  }

  int averror,i;
  
  assert(entry != NULL);
  
  if ((averror = av_open_input_file(&entry->av_format_context,entry->abs_mrl,NULL,0,NULL)) != 0) {
    RMR_error("can't open playlist entry '%s': %s\n",entry->abs_mrl,averror_string(averror));
    return 0;
  } 
  for(i=0; i<entry->av_format_context->nb_streams; i++) {
    if (entry->av_format_context->streams[i]->codec.codec_type == CODEC_TYPE_VIDEO) {
      AVCodec * codec = NULL;
      entry->video_stream = entry->av_format_context->streams[i];
      codec = avcodec_find_decoder(entry->video_stream->codec.codec_id);
      avcodec_open(&entry->video_stream->codec,codec);
      entry->video_stream->codec.get_buffer = jam_frame_get_buffer;
      entry->video_stream->codec.release_buffer = jam_frame_release_buffer;
      entry->video_stream->codec.flags |= CODEC_FLAG_EMU_EDGE; 
      if (entry->type == 's') {
	if (s_max_w < entry->video_stream->codec.width)
	  s_max_w = entry->video_stream->codec.width;
	if (s_max_h < entry->video_stream->codec.height)
	  s_max_h = entry->video_stream->codec.height;
      }
      return 1;
    }
  }

  RMR_debug("entry '%s' will be removed (wrong format?)\n",entry->mrl);
  return 0;
}

int playlist_open(const char * fname)
{
  char * video_dir = NULL, * playlist_name = NULL;
  
  char * resolve_video_filename(char ** pfname)
  {
    char * abs = *pfname; 
    if ((*pfname)[0]!='/') { 
      int vd_len, fn_len; 
      abs = malloc( 
	  (vd_len = strlen(video_dir)) + 
	  (fn_len = strlen(*pfname)) + 2); 
      memcpy(abs, video_dir, vd_len); 
      abs[vd_len] = '/'; 
      memcpy(abs+vd_len+1,*pfname,fn_len+1); 
    }
    return abs;
  }

  void sort_parts(playlist_entry_t * entry)
  {
    gint compare_cues(playlist_frame2cue_t * a,playlist_frame2cue_t * b)
    { return (int)a->frame - (int)b->frame; }
    
    gint compare_exits(playlist_exit_t * a,playlist_exit_t * b)
    { return (int)a->y1 - (int)b->y1; }

    void collect_nom_speeds(playlist_exit_t * e, float * sum)
    { 
      (*sum) += e->entry->nominal_speed; 
    }
	
    if (entry->cues) entry->cues = g_list_sort(entry->cues,(GCompareFunc)compare_cues);
    if (entry->exits) {
      entry->exits = g_list_sort(entry->exits,(GCompareFunc)compare_exits);
      /* intersection's nominal speed = avg of exits' */
      entry->nominal_speed = 0.0;
      g_list_foreach(entry->exits,(GFunc)collect_nom_speeds,(gpointer)&(entry->nominal_speed));
      entry->nominal_speed /= (float)g_list_length(entry->exits);
      RMR_debug("'%s' nominal speed: %5.3f\n",entry->mrl,entry->nominal_speed);
    }
  }

  GList * iter;

  playlist_reset();

  if (fname) playlist_name = strdupa(fname);
  video_dir = dirname(playlist_name);

  playlist = playlist_parse(fname);
  
  iter = playlist;
  while (iter) {
    playlist_entry_t * entry = (playlist_entry_t *)iter->data;
    GList * next = iter->next;

    entry->abs_mrl = resolve_video_filename(&entry->mrl);
    if (cache_av_context(entry))
      hasj_entry(entry);
    else {
      RMR_error("file %s doesn't exist, removing entry\n",entry->mrl);
      playlist_entry_free(entry);
      playlist = g_list_delete_link(playlist,iter);
    }

    iter = next;
  }
  
  g_hash_table_foreach_remove(x_hasj,(GHRFunc)x_resolve,NULL);
  g_hash_table_foreach(s_hasj,(GHFunc)s_resolve,NULL);
  g_list_foreach(playlist,(GFunc)sort_parts,NULL);

  return 1;
}

playlist_entry_t * playlist_get_i_movie()
{
  return i_movie;
}

playlist_entry_t * playlist_get_street1()
{
  return street1;
}

playlist_entry_t * playlist_get_random_intersection(GList * list)
{
  unsigned int num, i;

  if (!list)
    list = x_list;

  assert(list != NULL);
  
  num = g_list_length(list);
  i = random() % num;
  return (playlist_entry_t *) g_list_nth_data(list,i);
}

playlist_entry_t * playlist_get_next_intersection(GList * list,double avg_speed)
{
  GList * iter = list;
  playlist_entry_t * candidate;
  double diff;
  
  if (!iter)
    iter = x_list;

  assert(iter != NULL);

  candidate = (playlist_entry_t*)iter->data;
  diff = fabs(candidate->nominal_speed - avg_speed);
  iter = iter->next;
  
  while(iter) {
    playlist_entry_t * cur_entry = (playlist_entry_t*)iter->data;
    double cur_diff = fabs(cur_entry->nominal_speed - avg_speed);
    if (diff > cur_diff) {
      candidate = cur_entry;
      diff = cur_diff;
    }
    iter = iter->next;
  }

  RMR_info("Based on current speed (%.3g) intersection movie '%s' with (nominal speed %.3g) was selected\n",
      avg_speed,candidate->mrl,candidate->nominal_speed);

  return candidate;
}

int playlist_seek(playlist_entry_t * entry,int pos,int whence)
{
  int64_t av_pos;
  int res;
  assert(entry);
  assert(entry->av_format_context != NULL);
 
  switch(whence) {
    case SEEK_CUR:
      pos += entry->cur_frame;
    case SEEK_SET:
      if (pos >= 0)
	break;
    case SEEK_END:
      /* not sure...*/
      pos += (entry->av_format_context->duration/((int64_t)AV_TIME_BASE*25ll));
      break;
  }
  
  av_pos = pos * (AV_TIME_BASE / 25);
  if ((res = av_seek_frame(entry->av_format_context,-1,av_pos)) == 0) {
    entry->cur_frame = pos;
  }
  return res;
}
