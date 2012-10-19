
#ifndef DRIVERS_W1_SLAVES_W1_DS2431_H
#define DRIVERS_W1_SLAVES_W1_DS2431_H

#include "../w1.h"

int w1_f2d_read(struct w1_slave *sl, int off, int count, char *buf);

#endif  /* DRIVERS_W1_SLAVES_W1_DS2431_H */
