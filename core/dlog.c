#include "dlog.h"

#include "plat.h"

#include <stdarg.h>
#include <stddef.h>

#define DLOG_LINE_MAX 512U

/* value를 base 진법으로 out[pos..]에 채워넣고 새 pos를 반환 */
static size_t format_unsigned(char *out, size_t pos, size_t cap,
                              unsigned long long value, unsigned int base,
                              int uppercase, unsigned int width, int zero_pad)
{
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[24];
    unsigned int count = 0;
    unsigned int i;

    if (value == 0)
    {
        tmp[count++] = '0';
    }

    while (value > 0 && count < sizeof(tmp))
    {
        tmp[count++] = digits[value % base];
        value /= base;
    }

    /* 자릿수가 width보다 짧으면 왼쪽을 0 또는 공백으로 채움 */
    while (count < width && count < sizeof(tmp))
    {
        tmp[count++] = zero_pad ? '0' : ' ';
    }

    /* tmp에는 역순으로 쌓였으므로 뒤집어서 복사 */
    for (i = 0; i < count && pos + 1 < cap; i++)
    {
        out[pos++] = tmp[count - 1 - i];
    }

    return pos;
}

static void format_line(char *out, size_t cap, const char *fmt, va_list args)
{
    size_t pos = 0;

    while (*fmt != '\0' && pos + 1 < cap)
    {
        int zero_pad = 0;
        unsigned int width = 0;
        int is_size_t = 0;

        if (*fmt != '%')
        {
            out[pos++] = *fmt++;
            continue;
        }

        fmt++;

        if (*fmt == '0')
        {
            zero_pad = 1;
            fmt++;
        }

        while (*fmt >= '0' && *fmt <= '9')
        {
            width = width * 10U + (unsigned int)(*fmt - '0');
            fmt++;
        }

        if (*fmt == 'z')
        {
            is_size_t = 1;
            fmt++;
        }

        switch (*fmt)
        {
        case 's':
        {
            const char *s = va_arg(args, const char *);

            if (s == NULL)
            {
                s = "(null)";
            }
            while (*s != '\0' && pos + 1 < cap)
            {
                out[pos++] = *s++;
            }
            break;
        }
        case 'c':
            out[pos++] = (char)va_arg(args, int);
            break;
        case '%':
            out[pos++] = '%';
            break;
        case 'd':
        {
            long long value = (long long)va_arg(args, int);
            unsigned long long magnitude;

            if (value < 0)
            {
                if (pos + 1 < cap)
                {
                    out[pos++] = '-';
                }
                magnitude = (unsigned long long)(-value);
            }
            else
            {
                magnitude = (unsigned long long)value;
            }
            pos = format_unsigned(out, pos, cap, magnitude, 10U, 0, width, zero_pad);
            break;
        }
        case 'u':
        {
            unsigned long long value = is_size_t
                ? (unsigned long long)va_arg(args, size_t)
                : (unsigned long long)va_arg(args, unsigned int);

            pos = format_unsigned(out, pos, cap, value, 10U, 0, width, zero_pad);
            break;
        }
        case 'x':
        case 'X':
        {
            unsigned long long value = is_size_t
                ? (unsigned long long)va_arg(args, size_t)
                : (unsigned long long)va_arg(args, unsigned int);

            pos = format_unsigned(out, pos, cap, value, 16U, *fmt == 'X', width, zero_pad);
            break;
        }
        default:
            /* 지원 안 하는 지정자는 그대로 노출해서 눈에 띄게 한다 */
            out[pos++] = '%';
            if (pos + 1 < cap && *fmt != '\0')
            {
                out[pos++] = *fmt;
            }
            break;
        }

        if (*fmt != '\0')
        {
            fmt++;
        }
    }

    out[pos] = '\0';
}

void dlog_printf(const char *fmt, ...)
{
    char line[DLOG_LINE_MAX];
    va_list args;

    if (fmt == NULL)
    {
        return;
    }

    va_start(args, fmt);
    format_line(line, sizeof(line), fmt, args);
    va_end(args);

    plat_puts(line);
}
