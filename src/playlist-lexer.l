%{
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

float strtof(const char *nptr, char **endptr);

typedef struct playlist_entry_t playlist_entry_t;
typedef struct playlist_frame2cue_t playlist_frame2cue_t;
typedef struct playlist_exit_t playlist_exit_t;
typedef struct GList GList;
#include "y.tab.h"
%}

%s parameters

%%

<INITIAL>[isxaev]/[[:blank:]] {
  BEGIN(parameters);
  return yytext[0];
}

([ \t]*"\n")+  {
  BEGIN(INITIAL);
}

"{" {
  BEGIN(INITIAL);
  return '{';
}

"}" {
  BEGIN(INITIAL);
  return '}';
}

<parameters>[0-9]+\.[0-9]+ {
  char * tail;
  yylval.floatnum = strtof(yytext,&tail);
  if (yytext == tail) {
    fprintf(stderr,"Error parsing float '%s'\n",yytext);
  } 
  return FLOAT;
}
	  
<parameters>[0-9]+  {
  char * tail;
  yylval.uint = strtoul(yytext,&tail,10);
  if (yytext == tail) {
    fprintf(stderr,"Error parsing integer '%s'\n",yytext);
  }
  return UINT;
}

<parameters>[[:alnum:]_.\/-]+ {
  yylval.str = strdup(yytext);
  return STRING;
}

"#".*$ /* eat comments */ 

[[:blank:]]+  /* eat white space */

. {
  fprintf(stderr,"Playlist format error\n");
}

%%

