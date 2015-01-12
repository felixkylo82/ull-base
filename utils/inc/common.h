/*
 * common.h
 *
 *  Created on: Jan 7, 2015
 *      Author: felix
 */

#ifndef COMMON_H_
#define COMMON_H_

#define SISO
#define CACHE_LINE_SIZE 64U
#define MAX_ALIGNMENT 8U

#ifdef DEBUG

#include <cassert>
#define ASSERT(X, MSG) assert(X);

#else	/* DEBUG */

#include <cstdio>
#define ASSERT(X, MSG) if(!X) fprintf(stderr, MSG);

#endif	/* DEBUG */

#endif /* COMMON_H_ */
