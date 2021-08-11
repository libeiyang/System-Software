#ifndef _CONNMGR_H_
#define _CONNMGR_H_
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <inttypes.h>
#include <unistd.h>
#include <time.h>
#include "config.h"

#include "lib/tcpsock.h"
#include "lib/dplist.h"
#include "sbuffer.h"
#include <poll.h>

#ifndef TIMEOUT
#define TIMEOUT 5
#endif


/*
 * Use ERROR_HANDLER() for handling memory allocation problems, invalid sensor IDs, non-existing files, etc.
 */
#define ERROR_HANDLER(condition, ...)    do {                       \
                      if (condition) {                              \
                        printf("\nError: in %s - function %s at line %d: %s\n", __FILE__, __func__, __LINE__, __VA_ARGS__); \
                        exit(EXIT_FAILURE);                         \
                      }                                             \
                    } while(0)

                    
/*
This method holds the core functionality of your connmgr. It starts listening on the given port and when when a sensor node connects it
writes the data to shared buffer.
*/
void connmgr_listen(int port_number, sbuffer_t* sbuffer);

/*
This method should be called to clean up the connmgr, and to free all used memory. After this no new connections will be accepted
*/
void connmgr_free();

#endif /* _CONNMGR_H_ */