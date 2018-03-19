/**
 * @file check_utils.h
 * @author Rafael Antoniello
 */

#ifndef UTILS_SRC_CHECK_UTILS_H_
#define UTILS_SRC_CHECK_UTILS_H_

#include <stdio.h>
#include <string.h>

#define __FILENAME__ strrchr("/" __FILE__, '/') + 1

/* Internal MACRO: do not use directly */
#define CHECK_UTILS_DO_(COND, ACTION, LOG) \
    if(!(COND)) {\
        LOG;\
        ACTION;\
    }

/**
 * Simple ASSERT implementation: does not exit the program but just outputs
 * an error trace.
 */
#define ASSERT(COND) \
    CHECK_UTILS_DO_(COND, , fprintf(stderr, "%s:%d:%s Assertion failed.\n",\
    		__FILENAME__, __LINE__, __FUNCTION__))

/**
 * Generic trace for tracking check-points failures.
 */
#define CHECK_DO(COND, ACTION) \
    CHECK_UTILS_DO_(COND, ACTION, fprintf(stderr,\
"%s:%d:%s Check point failed.\n", __FILENAME__, __LINE__, __FUNCTION__))

#endif /* UTILS_SRC_CHECK_UTILS_H_ */
