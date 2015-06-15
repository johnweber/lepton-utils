#ifndef __LEPTSCI_H
#define __LEPTSCI_H

#include <sys/time.h>
#include <time.h>

int leptopen(void);
int leptget(unsigned short *);
int leptclose(void);
double time_subtract(struct timespec ts_old_time, struct timespec ts_time);


#endif
