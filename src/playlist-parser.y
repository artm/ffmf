/* 
 * xinemf
 *
 * playlist parser. 
 *
 */

%{
#include <stdio.h>
#include <malloc.h>
#include <glib.h>

#include "playlist.h"

GList * result = NULL;

static int yyerror(const char *s);
int yylex();

/* helper functions */

static playlist_entry_t * new_entry();
static playlist_entry_t * add_cue_to_entry(playlist_entry_t * entry, playlist_frame2cue_t * cue);
static playlist_entry_t * add_xref_to_entry(playlist_entry_t * entry, char * xref);
static playlist_entry_t * add_playlist_exit_to_entry(playlist_entry_t * entry, playlist_exit_t * exit);
static playlist_entry_t * set_nom_speed_for_entry(playlist_entry_t * entry, float nominal_speed);


%}
/* bison declarations */

%union {
  char * str;
  unsigned int uint;
  float floatnum;
  playlist_entry_t * entry;
  GList * list;
  playlist_exit_t * exit;
  playlist_frame2cue_t * cue;
}

%token <uint>     UINT 
%token <str>      STRING 
%token <floatnum> FLOAT

%type  <list>  entries
%type  <entry> entry
%type  <entry> intro 
%type  <entry> street s_group 
%type  <entry> intersection x_group
%type  <cue>   a_cue
%type  <str>   x_ref
%type  <exit>  exit_ref
%type  <floatnum> nom_speed number


%% /* grammar rules */

input: entries { result = $1; }
     ;

entries:  /* empty */ { $$ = NULL; }
       |  entries entry { $$ = g_list_append($1,$2); }
       ;

entry: intro
     | street
     | intersection 
     ;

intro: 'i' STRING 
     { 
       $$ = new_entry();
       $$->type = 'i'; 
       $$->mrl = $2;
     }
     ;

street: 's' STRING /* default everything */
      { 
	$$ = new_entry();
        $$->type = 's'; 
        $$->mrl = $2;
      }
      | 's' STRING '{' s_group '}'
      {
        $$ = $4;
        $$->type = 's'; 
	$$->mrl = $2;
      }
      ;

s_group: /* empty */ { $$ = NULL; }
       | s_group a_cue { $$ = add_cue_to_entry($1, $2); }
       | s_group x_ref { $$ = add_xref_to_entry($1, $2); }
       | s_group nom_speed { $$ = set_nom_speed_for_entry($1, $2); }
       ;

a_cue: 'a' UINT UINT
     { 
       $$ = g_new0(playlist_frame2cue_t,1);
       $$->frame = $2;
       $$->cue   = $3;
     }
     ;

x_ref: 'x' STRING { $$ = $2; }
     ;

nom_speed: 'v' number { $$ = $2; }
	 ;

number: UINT { $$ = (float) $1; }
      | FLOAT { $$ = $1; }
      ;

intersection: 'x' STRING '{' x_group '}' 
	    {
	      $$ = $4;
	      $$->type = 'x'; 
	      $$->mrl = $2;
	    }
	    ;

x_group: /* empty */ { $$ = NULL; }
       | x_group a_cue { $$ = add_cue_to_entry($1, $2); }
       | x_group exit_ref { $$ = add_playlist_exit_to_entry($1, $2); }
       ;

exit_ref: 'e' STRING UINT UINT UINT UINT
	{
	  $$ = g_new0(playlist_exit_t,1);
	  $$->entry = (playlist_entry_t*) $2;
	  $$->x1 = $3;
	  $$->y1 = $4 - 1;
	  $$->x2 = $5;
	  $$->y2 = $6 - 1;
	}
	;

%%

static int yyerror(const char *s)
{
  fprintf(stderr,"Playlist format error: %s\n",s);
  return 0;
}

extern FILE * yyin;

GList * playlist_parse(const char * fname)
{
  if (fname) 
    yyin = fopen(fname,"r");
  else
    yyin = stdin;
    
  if (yyparse() ==0)
    return result;
  else
    return NULL;
}

static playlist_entry_t * new_entry()
{
  playlist_entry_t * e = g_new0(playlist_entry_t,1);
  e->nominal_speed = 1.0;
  return e;
}

static playlist_entry_t * add_cue_to_entry(playlist_entry_t * entry, playlist_frame2cue_t * cue)
{
  if (!entry) 
    entry = new_entry(); 
  entry->cues = g_list_append(entry->cues, (gpointer)cue);
  return entry;
}

static playlist_entry_t * add_xref_to_entry(playlist_entry_t * entry, char * xref)
{
  if (!entry) 
    entry = new_entry(); 
  entry->intersections = g_list_append(entry->intersections, (gpointer)xref);
  return entry;
}

static playlist_entry_t * add_playlist_exit_to_entry(playlist_entry_t * entry, playlist_exit_t * exit)
{
  if (!entry) 
    entry = new_entry(); 
  entry->exits = g_list_append(entry->exits, (gpointer)exit);
  return entry;
}

static playlist_entry_t * set_nom_speed_for_entry(playlist_entry_t * entry, float nominal_speed)
{
  if (!entry)
    entry = new_entry();
  entry->nominal_speed = nominal_speed;
  return entry;
}
