#include <stdio.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <unistd.h> /* close() */
#include <malloc.h>
#include <ctype.h>

#include <libomc.h>
#include "OSC-client.h"
#include "htmsocket.h"

#include <glib.h>

#include "xinemf.h"

#include <errno.h>
#include <string.h>

#define OSC_MSG_SIZE  512

static void parse_message(void * retval, int arglen, void * args);

static GThread * recv_thread;
static GMutex * mutex;
static int running = 0;
static int sock;
static int port;
static struct OSCReceiveMemoryTuner rt;

/* for sending cues */
static GThread * cue_thread;
static int cue_running = 0;

static GMutex * cue_mutex;
static GAsyncQueue * cue_queue = NULL;

static GList * cue_targets = NULL;

static void *xMalloc(int numBytes) {
    return malloc(numBytes);
}

static void
velocity_callback(void *context, int arglen, const void *args,
		  OSCTimeTag when, NetworkReturnAddressPtr ra)
{
  float f = 0.0;
  parse_message(&f, arglen, (void*)args);
  RMR_set_speed(f);
}

static void
brightness_callback(void *context, int arglen, const void *args,
		    OSCTimeTag when, NetworkReturnAddressPtr ra)
{
  float f = 0.0;
  parse_message(&f, arglen, (void*)args);
  RMR_set_brightness(f);
}

static void
panning_callback(void *context, int arglen, const void *args,
		 OSCTimeTag when, NetworkReturnAddressPtr ra)
{
  float f = 0.0;
  parse_message(&f, arglen, (void*)args);
  RMR_set_x(f);
}

static void
go_x_callback(void *context, int arglen, const void *args,
	      OSCTimeTag when, NetworkReturnAddressPtr ra)
{
  RMR_go_x();
}

static void
go_i_callback(void *context, int arglen, const void *args,
	      OSCTimeTag when, NetworkReturnAddressPtr ra)
{
  RMR_go_i();
}

static void
go_m_callback(void *context, int arglen, const void *args,
	      OSCTimeTag when, NetworkReturnAddressPtr ra)
{
  int i = 0;
  parse_message(&i, arglen, (void*)args);
  if (i==1) 
    RMR_go_1();
}

static void
receive_packet(int fd)
{
  OSCPacketBuffer pb;
  struct NetworkReturnAddressStruct *ra;
  int capacity = OSCGetReceiveBufferSize();
  int maxclilen = sizeof(struct sockaddr_in);
  int morePackets = 1;
  char *buf;
  int bytes;

  while (morePackets) {
    pb = OSCAllocPacketBuffer();
    
    if (!pb) {
      RMR_warning("Out of memory for packet buffers; packet dropped!\n");
      return;
    }

    buf = OSCPacketBufferGetBuffer(pb);
    ra = (struct NetworkReturnAddressStruct *)OSCPacketBufferGetClientAddr(pb);
    ra->clilen = maxclilen;
    ra->sockfd = fd;

    bytes = recvfrom (fd, buf, capacity, 0, (struct sockaddr*)&(ra->cl_addr), &(ra->clilen));

    if (bytes>0) {
      int *sizep = OSCPacketBufferGetSize(pb);
      *sizep = bytes;
      OSCAcceptPacket(pb);
    } else {
      OSCFreePacket(pb);
      morePackets = 0;
    }
  }
}

static void *
RMR_osc_thread(void * data)
{
  int i;
  struct timeval tv;
  fd_set active_fd, read_fd;

  FD_ZERO(&active_fd);
  FD_SET(sock, &active_fd);
  
  g_mutex_lock(mutex);
  running = 1;
  g_mutex_unlock(mutex);

  while (running) {
    read_fd = active_fd;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    
    if ((i = select(FD_SETSIZE, &read_fd, NULL, NULL, &tv)) < 0) {
      RMR_error("Select returned < 0! \n"); 
      g_mutex_lock(mutex);
      running = 0;
      g_mutex_unlock(mutex);
    }

    if (i==0)
      continue;

    for (i=0;i<FD_SETSIZE;i++) {
      if (FD_ISSET(i, &read_fd)) {
	receive_packet(sock);
      }
    }
  }

  close(sock);
  return NULL;
}

static void
send_cue(OSCbuf * data,void * htmsock)
{
  char * msg;
  int size;
  
  if (!(msg = OSC_getPacket(data))) {
    RMR_error("RMR_osc: not a valid packet\n");
    return;
  }

  if (!(size = OSC_packetSize(data))) {
    RMR_error("RMR_osc: packet does not have valid size\n");
    return;
  }
 
  SendHTMSocket(htmsock, size, msg);
}

static void *
RMR_osc_cue_thread(void * data)
{
  g_async_queue_ref(cue_queue);
 
  g_mutex_lock(cue_mutex);
  cue_running = 1;
  g_mutex_unlock(cue_mutex);
  
  while(cue_running) {
    GList * iter;
    OSCbuf oscbuf;
    char msgbuf[OSC_MSG_SIZE];
    int cue;

    /* will block */
    
    cue = (int)g_async_queue_pop(cue_queue);
      
    OSC_initBuffer(&oscbuf, OSC_MSG_SIZE, msgbuf);
    if (OSC_writeAddress(&oscbuf, "/rmr/cue")) {
      RMR_error("RMR_osc: error writing cue address\n");
      continue;
    }

    if (OSC_writeIntArg(&oscbuf, cue)) {
      RMR_error("RMR_osc: error writing cue: %d\n", cue);
      continue;
    }

    iter = cue_targets;
    while(iter) {
      RMR_cue_target_t * targ = (RMR_cue_target_t*)iter->data;
      if (cue >= targ->min_cue && cue <= targ->max_cue) {
        send_cue(&oscbuf,targ->htmsock);
	RMR_debug("sending cue #%i to %s:%i\n",cue,targ->host,targ->port);
      }
      iter = iter->next;
    }
    OSC_resetBuffer(&oscbuf);
  }
  
  g_async_queue_unref(cue_queue);
  /* FIXME close sockets */
  return NULL;
}

static void
init_address_space()
{
  struct OSCAddressSpaceMemoryTuner t;
  struct OSCContainerQueryResponseInfoStruct cqinfo;
  struct OSCMethodQueryResponseInfoStruct QueryResponseInfo;
  OSCcontainer OSCTopLevelContainer;
  OSCcontainer rmr_container;
  OSCcontainer rmr_g_container;
    
  t.initNumContainers = 20;
  t.initNumMethods = 20;
  t.InitTimeMemoryAllocator = xMalloc;
  t.RealTimeMemoryAllocator = xMalloc;

  OSCTopLevelContainer = OSCInitAddressSpace (&t);

  OSCInitContainerQueryResponseInfo (&cqinfo);
  rmr_container = OSCNewContainer ("rmr", OSCTopLevelContainer, &cqinfo);
  rmr_g_container = OSCNewContainer ("g", rmr_container, &cqinfo);

  OSCInitMethodQueryResponseInfo (&QueryResponseInfo);
  OSCNewMethod ("v", rmr_container, velocity_callback, NULL, &QueryResponseInfo);
  OSCNewMethod ("b", rmr_container, brightness_callback, NULL, &QueryResponseInfo);
  OSCNewMethod ("p", rmr_container, panning_callback, NULL, &QueryResponseInfo);
  OSCNewMethod ("x", rmr_g_container, go_x_callback, NULL, &QueryResponseInfo);
  OSCNewMethod ("i", rmr_g_container, go_i_callback, NULL, &QueryResponseInfo);
  OSCNewMethod ("m", rmr_g_container, go_m_callback, NULL, &QueryResponseInfo);
}

void RMR_osc_start(RMR_config_t * config)
{
  struct sockaddr_in serv_addr;
  
  if (!config) { 
    RMR_error("RMR_osc no config file.\n");
    goto open_cue_targets;
  } 
  if ((port = config->listen_port) < 0) {
    RMR_warning("RMR_osc no port specified.\n");
    goto open_cue_targets;
  }  
 
  if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    RMR_error("RMR_osc: unable to create socket\n");
    goto open_cue_targets;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);

  if (bind(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    RMR_error("RMR_osc: unable to bind socket\n");
    goto open_cue_targets;
  }
 
  fcntl(sock, F_SETFL, FNDELAY);

  init_address_space();

  rt.InitTimeMemoryAllocator = xMalloc;
  rt.RealTimeMemoryAllocator = xMalloc;
  rt.receiveBufferSize = 1000;
  rt.numReceiveBuffers = 100;
  rt.numQueuedObjects = 200;
  rt.numCallbackListNodes = 100;

  if (OSCInitReceive(&rt) < 0) {
    RMR_error("RMR_osc: failed OSCInitReceive()\n");
    close(sock);
    return;
  }
  
  RMR_debug("starting RMR_osc thread (%d).\n", config->listen_port);
  mutex = g_mutex_new();
  recv_thread = g_thread_create(RMR_osc_thread, 
				NULL,
				FALSE,
				NULL);
  if (!recv_thread) {
    RMR_error("oops, OSC thread won't start!\n");
    close(sock);
  }

open_cue_targets:
  cue_targets = config->cue_targets;
  if (cue_targets) {
    GList * iter = config->cue_targets;
    while (iter) {
      RMR_cue_target_t * target = (RMR_cue_target_t*)iter->data;
      target->htmsock = OpenHTMSocket(target->host, target->port);
      iter = iter->next;
    }
    
    RMR_debug("RMR_osc: starting cue thread\n");
    cue_queue = g_async_queue_new();
    cue_mutex = g_mutex_new();
    cue_thread = g_thread_create(RMR_osc_cue_thread, 
				NULL,
				FALSE,
				NULL);
    if (!cue_thread) {
      /* FIXME close all socks */
      RMR_error("oops, cues thread won't start!\n");
    }

  }
}

void RMR_osc_send_cue(int cue)
{
  if (cue_queue) 
    g_async_queue_push(cue_queue, (void*)cue);
}

void RMR_osc_stop()
{
  if (running) {
    g_mutex_lock(mutex);
    running = 0;
    g_mutex_unlock(mutex);
  }

  if (cue_running) {
    g_mutex_lock(cue_mutex);
    cue_running = 0;
    g_mutex_unlock(cue_mutex);
  }
  
  if (cue_queue)
    g_async_queue_unref(cue_queue);
}


/* below is from dumpOSC (with some changes) */

static void parse_typetagged(void *r, void *v, int n);
static void parse_heuristically_typeguessed(void *r, void *v, int n, int skipComma);
char *DataAfterAlignedString(char *string, char *boundary); 
Boolean IsNiceString(char *string, char *boundary);

#define SMALLEST_POSITIVE_FLOAT 0.000001f

static void
parse_message(void * retval, int arglen, void * args)
{
  char * chars = args;

  /* from dumpOSC.c */
  if (arglen != 0) {
    if (chars[0] == ',') {
      if (chars[1] != ',') {
	/* This message begins with a type-tag string */
	parse_typetagged(retval, args, arglen);
      } else {
	/* Double comma means an escaped real comma, not a type string */
	parse_heuristically_typeguessed(retval, args, arglen, 1);
      }
    } else {
      parse_heuristically_typeguessed(retval, args, arglen, 0);
    }
  }
}

static void parse_typetagged(void *r, void *v, int n) { 
    char *typeTags, *thisType;
    char *p;

    typeTags = v;

    if (!IsNiceString(typeTags, typeTags+n)) {
	/* No null-termination, so maybe it wasn't a type tag
	   string after all */
	parse_heuristically_typeguessed(r, v, n, 0);
	return;
    }

    p = DataAfterAlignedString(typeTags, typeTags+n);


    for (thisType = typeTags + 1; *thisType != 0; ++thisType) {
	switch (*thisType) {
	    case 'i': case 'r': case 'm': case 'c':
	    /* RMR_debug("(int) %d ", ntohl(*((int *) p)));*/
	    if (r) *((int*)r) = ntohl(*((int *)p));
	    p += 4;
	    break;

	    case 'f': {
		int i = ntohl(*((int *) p));
		float *floatp = ((float *) (&i));
		/*RMR_debug("(float) %f ", *floatp); */
		if (r) *((float*)r) = *floatp;
		p += 4;
	    }
	    break;

	    case 'h': case 't':
	    RMR_debug("[A 64-bit int] ");
	    p += 8;
	    break;

	    case 'd':
	    RMR_debug("[A 64-bit float] ");
	    p += 8;
	    break;

	    case 's': case 'S':
	    if (!IsNiceString(p, typeTags+n)) {
		RMR_debug("Type tag said this arg is a string but it's not!\n");
		return;
	    } else {
		RMR_debug("\"%s\" ", p);
		p = DataAfterAlignedString(p, typeTags+n);
	    }
	    break;

	    case 'T': RMR_debug("[True] "); break;
	    case 'F': RMR_debug("[False] "); break;
	    case 'N': RMR_debug("[Nil]"); break;
	    case 'I': RMR_debug("[Infinitum]"); break;

	    default:
	    RMR_debug("[Unrecognized type tag %c]", *thisType);
	    return;
	}
    }
}

static void parse_heuristically_typeguessed(void *r, void *v, int n, int skipComma) {
    int i, thisi;
    float thisf;
    int *ints;
    char *chars;
    char *string, *nextString;
	

    /* Go through the arguments 32 bits at a time */
    ints = v;
    chars = v;

    for (i = 0; i<n/4; ) {
	string = &chars[i*4];
	thisi = ntohl(ints[i]);
	/* Reinterpret the (potentially byte-reversed) thisi as a float */
	thisf = *(((float *) (&thisi)));

	if  (thisi >= -1000 && thisi <= 1000000) {
	    /*RMR_debug("%d ", thisi);*/
	    if (r) *((int*)r) = thisi;
	    i++;
	} else if (thisf >= -1000.f && thisf <= 1000000.f &&
		   (thisf <=0.0f || thisf >= SMALLEST_POSITIVE_FLOAT)) {
	    /*RMR_debug("%f ",  thisf);*/
	    if (r) *((float*)r) = thisf;
	    i++;
	} else if (IsNiceString(string, chars+n)) {
	    nextString = DataAfterAlignedString(string, chars+n);
	    RMR_debug("\"%s\" ", (i == 0 && skipComma) ? string +1 : string);
	    i += (nextString-string) / 4;
	} else {
	    RMR_debug("0x%x ", ints[i]);
	    i++;
	}
    }
}


#define STRING_ALIGN_PAD 4

char *DataAfterAlignedString(char *string, char *boundary) 
{
    /* The argument is a block of data beginning with a string.  The
       string has (presumably) been padded with extra null characters
       so that the overall length is a multiple of STRING_ALIGN_PAD
       bytes.  Return a pointer to the next byte after the null
       byte(s).  The boundary argument points to the character after
       the last valid character in the buffer---if the string hasn't
       ended by there, something's wrong.

       If the data looks wrong, return 0, and set htm_error_string */

    int i;

    if ((boundary - string) %4 != 0) {
	RMR_error("Internal error: DataAfterAlignedString: bad boundary\n");
	return 0;
    }

    for (i = 0; string[i] != '\0'; i++) {
	if (string + i >= boundary) {
	    RMR_error("DataAfterAlignedString: Unreasonably long string");
	    return 0;
	}
    }

    /* Now string[i] is the first null character */
    i++;

    for (; (i % STRING_ALIGN_PAD) != 0; i++) {
	if (string + i >= boundary) {
	    RMR_error("DataAfterAlignedString: Unreasonably long string");
	    return 0;
	}
	if (string[i] != '\0') {
	    RMR_error("DataAfterAlignedString: Incorrectly padded string.");
	    return 0;
	}
    }

    return string+i;
}

Boolean IsNiceString(char *string, char *boundary) 
{
    /* Arguments same as DataAfterAlignedString().  Is the given "string"
       really a string?  I.e., is it a sequence of isprint() characters
       terminated with 1-4 null characters to align on a 4-byte boundary? */

    int i;

    if ((boundary - string) %4 != 0) {
	RMR_error("Internal error: IsNiceString: bad boundary\n");
	return 0;
    }

    for (i = 0; string[i] != '\0'; i++) {
	if (!isprint(string[i])) return FALSE;
	if (string + i >= boundary) return FALSE;
    }

    /* If we made it this far, it's a null-terminated sequence of printing characters 
       in the given boundary.  Now we just make sure it's null padded... */

    /* Now string[i] is the first null character */
    i++;
    for (; (i % STRING_ALIGN_PAD) != 0; i++) {
	if (string[i] != '\0') return FALSE;
    }

    return TRUE;
}


