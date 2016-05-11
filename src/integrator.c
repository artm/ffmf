#include "xinemf.h"
integrator_t * RMR_integrator_new(int table_len)
{
  integrator_t * itg = g_new0(integrator_t,1);
  itg->table_len = table_len;
  itg->values = g_new0(double,table_len);
  itg->dts = g_new0(double,table_len);
  itg->timer = g_timer_new();
  return itg;
}

void RMR_integrator_add(integrator_t * itg,double val)
{
  g_assert(itg != NULL);
  
  if (itg->cur_len) {
    itg->dts[itg->cur_idx] = g_timer_elapsed(itg->timer,NULL);
    /* avg value on a previous integration step */
    itg->values[itg->cur_idx] += val;
    itg->values[itg->cur_idx] /= 2;
  }

  itg->cur_idx++;
  itg->cur_idx %= itg->table_len;

  itg->values[itg->cur_idx] = val;
  if (itg->cur_len<(itg->table_len-1)) itg->cur_len++;
}

double RMR_integrator_current(integrator_t * itg)
{
  int oldest, i;
  double integral = 0.0, dt=0.0;
  
  g_assert(itg != NULL);
  
  oldest = ((itg->cur_idx + itg->table_len) - itg->cur_len);

  for (i = 0; i<itg->cur_len; i++) {
    int idx = (oldest + i) % itg->table_len;
    integral += itg->values[idx]*itg->dts[idx];
    dt += itg->dts[idx];
  }
  return dt>0 ? integral/dt : 0;
}

