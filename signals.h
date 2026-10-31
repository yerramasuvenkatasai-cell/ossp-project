#ifndef SIGNALS_H
#define SIGNALS_H

#include "common.h"

extern volatile sig_atomic_t g_server_running;
extern volatile sig_atomic_t g_dump_stats_flag;

void signals_init(void);

#endif /* SIGNALS_H */
