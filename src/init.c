#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <argp.h>
#include "xinemf.h"

/* prototype's missing */
float strtof(const char *nptr, char **endptr);

const char * argp_program_version = "xinemf-3.0";
const char * argp_program_bug_address = "<artm@v2.nl>";
static char doc[] = "xinemf: Run Motherfucker Run Player";
static char args_doc[] = "playlist";
enum {
  SNAP1 = 256,
  SNAP2,
  INTER_SPEED,
};
static struct argp_option options[] = {
  {"verbosity",  'V', "LEVEL",      0, "verbosity level" },
  {"listen",     'l', "PORT",       0, "listen to OSC messages on PORT" },
  {"cue-target", 'c', "HOST:PORT[/mincue-maxcue]", 0, "osc client and eventual cues range" },
  {"initial-snap-range", '1', "SNAP_RANGE", 0, "initial snap range (pixels)" },
  {"final-snap-range", '2', "SNAP_RANGE", 0, "final snap range (pixels)" },
  {"intersection-speed", 's', "SPEED", 0, "nominal speed for intersection movies" },
  {"start-intersection-centred",'C', NULL, 0, "allways start intersection movies centred" },
  { 0 }
};
static error_t parse_opt(int key, char * arg, struct argp_state * state);
static struct argp argp = { options, parse_opt, args_doc, doc };

static error_t parse_opt(int key, char * arg, struct argp_state * state)
{
  RMR_config_t * config = state->input;

  switch (key) {
    case 'V':
      {
	char * tail = NULL;
	config->verbosity = strtol(arg, &tail, 10);
	if (tail == arg) {
	  fprintf(stderr, "\nError parsing verbosity level '%s'\n\n", arg);
	  argp_usage(state);
	}
      }
      break;
    case 'l': 
      {
	char * tail = NULL;
	config->listen_port = (int)strtol(arg,&tail,10);
	if (tail == arg) {
	  fprintf(stderr,"\nError parsing port number '%s'\n\n",arg);
	  argp_usage(state);
	}
      }
      break;
    case 'c':
      {
	char host[101];
	int port, min_cue=0, max_cue=INT_MAX;
	int res;

	res = sscanf(arg,"%100[^:]:%i/%i-%i",host,&port,&min_cue,&max_cue);
	if (res == 2 || res == 4) {
	  RMR_cue_target_t * targ = g_new(RMR_cue_target_t,1);
	  RMR_debug("parsed cue target"
	            " host: %s port: %i min_cue: %i max_cue: %i\n",
	      host, port, min_cue, max_cue);
	  targ->host = strdup(host);
	  targ->port = port;
	  targ->min_cue = min_cue;
	  targ->max_cue = max_cue;
	  config->cue_targets = g_list_append(config->cue_targets,targ);
	}
	break;
      }
    case 'C':
      config->centre_inter = 1;
      RMR_debug("Via center\n");
      break;      
    case '1':
      {
	char * tail = NULL;
	config->snap_range1 = strtol(arg, &tail, 10);
	if (tail == arg) {
	  fprintf(stderr, "\nError parsing snap range '%s'\n\n", arg);
	  argp_usage(state);
	} else
	  RMR_debug("Snap1: %d\n",config->snap_range1);
      }
      break;
    case '2':
      {
	char * tail = NULL;
	config->snap_range2 = strtol(arg, &tail, 10);
	if (tail == arg) {
	  fprintf(stderr, "\nError parsing snap range '%s'\n\n", arg);
	  argp_usage(state);
	} else
	  RMR_debug("Snap2: %d\n",config->snap_range2);
 
      }
      break;
    case 's':
      {
	char * tail = NULL;
	config->inter_speed = strtof(arg, &tail);
	if (tail == arg) {
	  fprintf(stderr, "\nError parsing intersection speed '%s'\n\n", arg);
	  argp_usage(state);
	} else
	  RMR_debug("Intersection speed: %g\n",config->inter_speed);
      }
      break;
    case ARGP_KEY_ARG:
      config->playlist = arg;
      break;
    case ARGP_KEY_END:
      if (state->arg_num < 1) {
	fprintf(stderr,"\nError: missing playlist file name\n\n");
	argp_usage(state);
      }
      break;
    default:
      return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

void RMR_parse_cmdline(int argc, char *argv[], RMR_config_t * config)
{
  argp_parse(&argp, argc, argv, 0, 0, config);
}
