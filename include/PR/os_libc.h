#ifndef _OS_LIBC_H_
#define _OS_LIBC_H_

#include "ultratypes.h"

#ifdef TARGET_N64
// Old deprecated functions from strings.h, replaced by memcpy/memset.
extern void bcopy(const void *, void *, size_t);
extern void bzero(void *, size_t);
#else
#include <string.h>
#include <strings.h>
#undef bcopy
#undef bzero
#undef bcmp
#define bcopy(src, dst, n) memmove((dst), (src), (n))
#define bzero(dst, n) memset((dst), 0, (n))
#define bcmp(a, b, n) memcmp((a), (b), (n))
#endif

/* Printf */

extern int        sprintf(char *s, const char *fmt, ...);
extern void        osSyncPrintf(const char *fmt, ...);
extern void        osAsyncPrintf(const char *fmt, ...);
extern int        osSyncGetChars(char *buf);
extern int        osAsyncGetChars(char *buf);

#endif /* !_OS_LIBC_H_ */
