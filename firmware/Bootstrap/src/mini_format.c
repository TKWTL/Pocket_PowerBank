#include "mini_format.h"
#include <stdint.h>

typedef struct {
    mini_format_putc_t fn;
    void *ctx;
    int count;
} mini_out_t;

typedef struct {
    char *buf;
    size_t size;
    size_t pos;
} mini_buf_t;

static void mini_emit(mini_out_t *o, char ch)
{
    if (o->fn) o->fn(ch, o->ctx);
    o->count++;
}

static void mini_repeat(mini_out_t *o, char ch, unsigned int n)
{
    while (n--) mini_emit(o, ch);
}

static unsigned int mini_strlen(const char *s)
{
    unsigned int n = 0U;
    if (!s) return 6U; /* "(null)" */
    while (s[n] != '\0') n++;
    return n;
}

static void mini_text(mini_out_t *o, const char *s, unsigned int width, uint8_t left)
{
    unsigned int len;
    if (!s) s = "(null)";
    len = mini_strlen(s);
    if (!left && width > len) mini_repeat(o, ' ', width - len);
    while (*s) mini_emit(o, *s++);
    if (left && width > len) mini_repeat(o, ' ', width - len);
}

static void mini_uint(mini_out_t *o,
                      unsigned long value,
                      unsigned int base,
                      uint8_t upper,
                      uint8_t negative,
                      unsigned int width,
                      uint8_t left,
                      uint8_t zero)
{
    char rev[16];
    unsigned int n = 0U;
    unsigned int total;
    unsigned int pad;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    do {
        rev[n++] = digits[value % base];
        value /= base;
    } while (value != 0UL && n < sizeof(rev));

    total = n + (negative ? 1U : 0U);
    pad = (width > total) ? (width - total) : 0U;

    if (!left && !zero) mini_repeat(o, ' ', pad);
    if (negative) mini_emit(o, '-');
    if (!left && zero) mini_repeat(o, '0', pad);
    while (n) mini_emit(o, rev[--n]);
    if (left) mini_repeat(o, ' ', pad);
}

int mini_vformat(mini_format_putc_t putc_fn, void *ctx, const char *fmt, va_list ap)
{
    mini_out_t o = { putc_fn, ctx, 0 };

    if (!fmt) return 0;

    while (*fmt) {
        uint8_t left, zero, is_long;
        unsigned int width;
        char spec;

        if (*fmt != '%') {
            mini_emit(&o, *fmt++);
            continue;
        }

        fmt++;
        if (*fmt == '%') {
            mini_emit(&o, *fmt++);
            continue;
        }

        left = 0U;
        zero = 0U;
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') left = 1U;
            else zero = 1U;
            fmt++;
        }

        width = 0U;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10U + (unsigned int)(*fmt - '0');
            fmt++;
        }

        is_long = 0U;
        if (*fmt == 'l') {
            is_long = 1U;
            fmt++;
        }

        spec = *fmt ? *fmt++ : '\0';
        switch (spec) {
        case 'c': {
            char ch = (char)va_arg(ap, int);
            if (!left && width > 1U) mini_repeat(&o, ' ', width - 1U);
            mini_emit(&o, ch);
            if (left && width > 1U) mini_repeat(&o, ' ', width - 1U);
            break;
        }
        case 's':
            mini_text(&o, va_arg(ap, const char *), width, left);
            break;
        case 'd':
        case 'i': {
            long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            uint8_t neg = (v < 0) ? 1U : 0U;
            unsigned long mag = neg ? (unsigned long)(-(v + 1L)) + 1UL : (unsigned long)v;
            mini_uint(&o, mag, 10U, 0U, neg, width, left, zero);
            break;
        }
        case 'u': {
            unsigned long v = is_long ? va_arg(ap, unsigned long)
                                      : (unsigned long)va_arg(ap, unsigned int);
            mini_uint(&o, v, 10U, 0U, 0U, width, left, zero);
            break;
        }
        case 'x':
        case 'X': {
            unsigned long v = is_long ? va_arg(ap, unsigned long)
                                      : (unsigned long)va_arg(ap, unsigned int);
            mini_uint(&o, v, 16U, (uint8_t)(spec == 'X'), 0U, width, left, zero);
            break;
        }
        case '\0':
            return o.count;
        default:
            mini_emit(&o, '%');
            if (spec) mini_emit(&o, spec);
            break;
        }
    }

    return o.count;
}

static void mini_buf_putc(char ch, void *ctx)
{
    mini_buf_t *b = (mini_buf_t *)ctx;
    if (b->buf && b->size != 0U && b->pos + 1U < b->size) {
        b->buf[b->pos] = ch;
    }
    b->pos++;
}

int mini_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    mini_buf_t b = { buf, size, 0U };
    int n = mini_vformat(mini_buf_putc, &b, fmt, ap);

    if (size != 0U && buf) {
        size_t end = (b.pos < size) ? b.pos : (size - 1U);
        buf[end] = '\0';
    }
    return n;
}

int mini_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = mini_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
