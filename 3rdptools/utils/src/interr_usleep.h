/**
 * @file interr_usleep.h
 * @brief Interruptible usleep module.
 * This module ("interruptible usleep") implements a wrapper to the
 * 'usleep()' function to provide the possibility to interrupt the sleep state
 * by 'unlocking' the module instance handler.
 * @author Rafael Antoniello
 */

#ifndef UTILS_SRC_INTERR_USLEEP_H_
#define UTILS_SRC_INTERR_USLEEP_H_

#include <sys/types.h>
#include <inttypes.h>

typedef struct interr_usleep_ctx_s interr_usleep_ctx_t;

/**
 * Initializes (open) interruptible module instance.
 * @return Pointer to the interruptible module instance context structure
 * ("handler") on success, NULL if fails.
 */
interr_usleep_ctx_t* interr_usleep_open();

/**
 * Unlock 'usleep' operation -if applicable- and release interruptible module
 * instance context structure.
 * @param ref_interr_usleep_ctx Reference to the pointer to the interruptible
 * module instance context structure to be release, that was obtained in a
 * previous call to the 'interr_usleep_open()' function. Pointer is set to NULL
 * on return.
 */
void interr_usleep_close(interr_usleep_ctx_t **ref_interr_usleep_ctx);

/**
 * Unlock 'usleep' operation.
 * @param Pointer to the interruptible module instance context structure,
 * that was obtained in a previous call to the 'interr_usleep_open()' function.
 */
void interr_usleep_unblock(interr_usleep_ctx_t *interr_usleep_ctx);

/**
 * Perform the interruptible 'usleep' operation; that is:
 * suspends execution of the calling thread for (at least) usec microseconds.
 * The sleep may be lengthened slightly by any system activity or by the time
 * spent processing the call or by the granularity of system timers.
 * @param Pointer to the interruptible module instance context structure,
 * that was obtained in a previous call to the 'interr_usleep_open()' function.
 * @param usec Unsigned 32-bit integer specifying the amount of microseconds
 * to sleep.
 * @return Status code: <br>
 * - '0' in case of success;
 * - EINTR in the case of interruption caused by a parallel call to
 * 'interr_usleep_unblock()' function;
 * - '-1' or other non-zero status code in the case of an internal error.
 */
int interr_usleep(interr_usleep_ctx_t *interr_usleep_ctx, uint32_t usec);

#endif /* UTILS_SRC_INTERR_USLEEP_H_ */
