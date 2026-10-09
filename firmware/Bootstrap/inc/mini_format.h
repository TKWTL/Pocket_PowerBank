#ifndef __MINI_FORMAT_H__
#define __MINI_FORMAT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stddef.h>

typedef void (*mini_format_putc_t)(char ch, void *ctx);

/* Lightweight integer/string formatter.
 * Supported: %% %c %s %d %i %u %x %X, optional '-'/'0' flag, width and single 'l'.
 * Intentionally no float/precision support. */
int mini_vformat(mini_format_putc_t putc_fn, void *ctx, const char *fmt, va_list ap);
int mini_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int mini_snprintf(char *buf, size_t size, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
