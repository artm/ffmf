#include <stdarg.h>
#include <stdio.h>
#include "xine.h"

int verbosity = 5;

void RMR_set_verbosity(int v)
{
  verbosity = v;
}

void RMR_info(char * format,...)
{
  va_list ap;

  printf("INFO: ");
  va_start(ap,format);
  vprintf(format,ap);
  va_end(ap);
}

void RMR_info0(char * format,...)
{
  va_list ap;

  va_start(ap,format);
  vprintf(format,ap);
  va_end(ap);
}

void RMR_error(char * format,...)
{
  va_list ap;

  fprintf(stderr,"ERROR: ");
  va_start(ap,format);
  vfprintf(stderr,format,ap);
  va_end(ap);
}

void RMR_error0(char * format,...)
{
  va_list ap;

  va_start(ap,format);
  vfprintf(stderr,format,ap);
  va_end(ap);
}

void RMR_warning(char * format,...)
{
  va_list ap;

  if (verbosity > 0) {
    fprintf(stderr,"WARNING: ");
    va_start(ap,format);
    vfprintf(stderr,format,ap);
    va_end(ap);
  }
}

void RMR_warning0(char * format,...)
{
  va_list ap;

  if (verbosity > 0) {
    va_start(ap,format);
    vfprintf(stderr,format,ap);
    va_end(ap);
  }
}

void RMR_debug(char * format,...)
{
  va_list ap;

  if (verbosity > 1) {
    fprintf(stderr,"DEBUG: ");
    va_start(ap,format);
    vfprintf(stderr,format,ap);
    va_end(ap);
  }
}

void RMR_debug0(char * format,...)
{
  va_list ap;

  if (verbosity > 1) {
    va_start(ap,format);
    vfprintf(stderr,format,ap);
    va_end(ap);
  }
}

