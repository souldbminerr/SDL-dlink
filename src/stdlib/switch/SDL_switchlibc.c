/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "SDL_internal.h"

#if defined(__SWITCH__) && defined(SDL_DNRO_BUILD)

/* Switch newlib shims for dlink .dnro builds.
 *
 * A .dnro fails to load when it imports anything the host does not
 * provide (bare dlink COULD_NOT_RESOLVE). The functions below are the
 * leaf libc symbols SDL needs that no host is guaranteed to export; they
 * are implemented here -fPIC using only universally host-provided libc,
 * so sdl3.dnro stays loadable under any host. Compiled only when
 * SDL_DNRO_BUILD is set (see CMakeLists); every other build is
 * untouched, avoiding duplicate definitions against a static libc.
 *
 * Notes:
 * - vsscanf covers the conversion set SDL uses (%d %i %u %o %x %X %f
 *   %e %g %s %c %p %n, widths, *, h/hh/l/ll, scansets). Exotic formats
 *   fail the conversion instead of misbehaving.
 * - fesetenv takes saved environments from feholdexcept; FE_DFL_ENV
 *   would dereference newlib's own variable (a host import the ABI
 *   check would flag), so only real storage is supported.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <fenv.h>

#include <switch/kernel/svc.h>

void _Exit(int status)
{
    (void)status;
    svcExitProcess();
    for (;;) {
        __asm__ volatile("wfe");
    }
}

char *itoa(int value, char *str, int base)
{
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    unsigned int u;
    char *p = str;
    char *head;
    char tmp;
    char *tail;
    if (base < 2 || base > 36) {
        *str = 0;
        return str;
    }
    if (value < 0 && base == 10) {
        *p++ = '-';
        u = (unsigned int)(-(value + 1)) + 1u;
    } else {
        u = (unsigned int)value;
    }
    head = p;
    do {
        *p++ = digits[u % (unsigned int)base];
        u /= (unsigned int)base;
    } while (u);
    *p = 0;
    tail = p - 1;
    while (head < tail) {
        tmp = *head;
        *head = *tail;
        *tail = tmp;
        head++;
        tail--;
    }
    return str;
}

size_t strlcat(char *dst, const char *src, size_t n)
{
    size_t dlen = 0;
    size_t slen = strlen(src);
    size_t i;
    size_t avail;
    while (dlen < n && dst[dlen])
        dlen++;
    if (dlen == n)
        return n + slen;
    avail = n - dlen - 1;
    for (i = 0; i < avail && src[i]; i++)
        dst[dlen + i] = src[i];
    dst[dlen + i] = 0;
    return dlen + slen;
}

char *strnstr(const char *s, const char *find, size_t slen)
{
    size_t flen = strlen(find);
    size_t i;
    if (!flen)
        return (char *)s;
    if (flen > slen)
        return NULL;
    for (i = 0; i + flen <= slen; i++) {
        if (s[i] == find[0] && memcmp(s + i, find, flen) == 0)
            return (char *)(s + i);
    }
    return NULL;
}

size_t wcslcat(wchar_t *dst, const wchar_t *src, size_t n)
{
    size_t dlen = 0;
    size_t slen = 0;
    size_t i;
    size_t avail;
    while (src[slen])
        slen++;
    while (dlen < n && dst[dlen])
        dlen++;
    if (dlen == n)
        return n + slen;
    avail = n - dlen - 1;
    for (i = 0; i < avail && src[i]; i++)
        dst[dlen + i] = src[i];
    dst[dlen + i] = 0;
    return dlen + slen;
}

int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
        if (!a[i])
            return 0;
    }
    return 0;
}

size_t wcsnlen(const wchar_t *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (!s[i])
            return i;
    }
    return n;
}

wchar_t *wcsstr(const wchar_t *s, const wchar_t *find)
{
    size_t flen = 0;
    while (find[flen])
        flen++;
    if (!flen)
        return (wchar_t *)s;
    for (; *s; s++) {
        size_t i;
        if (*s != *find)
            continue;
        for (i = 0; i < flen; i++) {
            if (s[i] != find[i])
                break;
        }
        if (i == flen)
            return (wchar_t *)s;
    }
    return NULL;
}

long lround(double x)
{
    if (x >= 0.0) {
        if (x > (double)(LONG_MAX - 1))
            return LONG_MAX;
        return (long)(x + 0.5);
    }
    if (x < (double)LONG_MIN)
        return LONG_MIN;
    return (long)(x - 0.5);
}

int feholdexcept(fenv_t *envp)
{
    uint64_t fpcr;
    uint64_t fpsr;
    __mrs_fpcr(fpcr);
    __mrs_fpsr(fpsr);
    *envp = (fpcr << 32) | (fpsr & 0xFFFFFFFFu);
    fpcr &= ~((uint64_t)_ENABLE_MASK);
    fpsr &= ~((uint64_t)FE_ALL_EXCEPT);
    __msr_fpcr(fpcr);
    __msr_fpsr(fpsr);
    return 0;
}

int fesetenv(const fenv_t *envp)
{
    uint64_t e = *envp;
    __msr_fpcr((uint32_t)(e >> 32));
    __msr_fpsr((uint32_t)(e & 0xFFFFFFFFu));
    return 0;
}

static int scan_isdigit(int c)
{
    return c >= '0' && c <= '9';
}

static int scan_isxdigit(int c)
{
    return scan_isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int scan_isoct(int c)
{
    return c >= '0' && c <= '7';
}

static void scan_space(const char **p, int *consumed)
{
    while (isspace((unsigned char)**p)) {
        (*p)++;
        (*consumed)++;
    }
}

/* Collects up to `width` (unlimited when width <= 0) accepted chars into
 * buf (cap bufcap, always NUL-terminated), returns chars taken. */
static int scan_take(const char **p, int *consumed, int width, char *buf, int bufcap,
                     int (*accept)(int))
{
    int n = 0;
    while ((width <= 0 || n < width) && **p && accept((unsigned char)**p)) {
        if (n + 1 < bufcap)
            buf[n] = **p;
        n++;
        (*p)++;
        (*consumed)++;
    }
    buf[n < bufcap ? n : bufcap - 1] = 0;
    return n;
}

/* Length modifier: 0 none, 1 hh, 2 h, 3 l, 4 ll. */
static int scan_length(const char **f)
{
    if (**f == 'h') {
        (*f)++;
        if (**f == 'h') {
            (*f)++;
            return 1;
        }
        return 2;
    }
    if (**f == 'l') {
        (*f)++;
        if (**f == 'l') {
            (*f)++;
            return 4;
        }
        return 3;
    }
    if (**f == 'L' || **f == 'j' || **f == 'z' || **f == 't') {
        (*f)++;
        return 3;
    }
    return 0;
}

static void store_signed(va_list *ap, int len, long long v, int *assigned, int nosave)
{
    if (nosave)
        return;
    if (len == 1)
        *va_arg(*ap, signed char *) = (signed char)v;
    else if (len == 2)
        *va_arg(*ap, short *) = (short)v;
    else if (len == 3)
        *va_arg(*ap, long *) = (long)v;
    else if (len == 4)
        *va_arg(*ap, long long *) = v;
    else
        *va_arg(*ap, int *) = (int)v;
    (*assigned)++;
}

static void store_unsigned(va_list *ap, int len, unsigned long long v, int *assigned, int nosave)
{
    if (nosave)
        return;
    if (len == 1)
        *va_arg(*ap, unsigned char *) = (unsigned char)v;
    else if (len == 2)
        *va_arg(*ap, unsigned short *) = (unsigned short)v;
    else if (len == 3)
        *va_arg(*ap, unsigned long *) = (unsigned long)v;
    else if (len == 4)
        *va_arg(*ap, unsigned long long *) = v;
    else
        *va_arg(*ap, unsigned int *) = (unsigned int)v;
    (*assigned)++;
}

static int scan_tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

int vsscanf(const char *str, const char *fmt, va_list ap)
{
    const char *p = str;
    const char *f = fmt;
    int consumed = 0;
    int assigned = 0;
    int input_failed = 0;

    while (*f) {
        if (isspace((unsigned char)*f)) {
            scan_space(&p, &consumed);
            f++;
            continue;
        }
        if (*f != '%') {
            if (!*p) {
                input_failed = (assigned == 0);
                break;
            }
            if (*p != *f)
                break;
            p++;
            consumed++;
            f++;
            continue;
        }
        f++;
        if (*f == '%') {
            if (!*p) {
                input_failed = (assigned == 0);
                break;
            }
            if (*p != '%')
                break;
            p++;
            consumed++;
            f++;
            continue;
        }
        {
            int nosave = 0;
            int width = 0;
            int len;
            char conv;
            int wleft;
            if (*f == '*') {
                nosave = 1;
                f++;
            }
            while (scan_isdigit((unsigned char)*f)) {
                width = width * 10 + (*f - '0');
                f++;
            }
            len = scan_length(&f);
            conv = *f++;
            if (!conv)
                break;
            if (conv == 'n') {
                int n = consumed;
                if (!nosave) {
                    if (len == 1)
                        *va_arg(ap, signed char *) = (signed char)n;
                    else if (len == 2)
                        *va_arg(ap, short *) = (short)n;
                    else if (len == 3 || len == 4)
                        *va_arg(ap, long long *) = (long long)n;
                    else
                        *va_arg(ap, int *) = n;
                }
                continue;
            }
            if (conv != 'c' && conv != '[')
                scan_space(&p, &consumed);
            if (!*p) {
                input_failed = (assigned == 0);
                break;
            }
            wleft = width > 0 ? width : -1;
            if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'o' || conv == 'x' ||
                conv == 'X' || conv == 'p') {
                char tok[64];
                int tn = 0;
                int neg = 0;
                int digits = 0;
                int base = 10;
                char *eptr = NULL;
                if (*p == '+' || *p == '-') {
                    if (wleft < 0 || wleft > 0) {
                        neg = (*p == '-');
                        if (tn + 1 < (int)sizeof(tok))
                            tok[tn++] = *p;
                        p++;
                        consumed++;
                        if (wleft > 0)
                            wleft--;
                    }
                }
                if (conv == 'i') {
                    if (p[0] == '0' && (wleft < 0 || wleft > 1) && (p[1] == 'x' || p[1] == 'X') &&
                        (wleft < 0 || wleft > 2) && scan_isxdigit((unsigned char)p[2])) {
                        base = 16;
                        if (tn + 2 < (int)sizeof(tok)) {
                            tok[tn++] = *p++;
                            tok[tn++] = *p++;
                            consumed += 2;
                            if (wleft > 0)
                                wleft -= 2;
                        }
                        digits = scan_take(&p, &consumed, wleft, tok + tn,
                                           (int)sizeof(tok) - tn, scan_isxdigit);
                    } else if (p[0] == '0') {
                        base = 8;
                        digits = scan_take(&p, &consumed, wleft, tok + tn,
                                           (int)sizeof(tok) - tn, scan_isoct);
                    } else {
                        base = 10;
                        digits = scan_take(&p, &consumed, wleft, tok + tn,
                                           (int)sizeof(tok) - tn, scan_isdigit);
                    }
                } else if (conv == 'o') {
                    base = 8;
                    digits = scan_take(&p, &consumed, wleft, tok + tn,
                                       (int)sizeof(tok) - tn, scan_isoct);
                } else if (conv == 'x' || conv == 'X' || conv == 'p') {
                    base = 16;
                    if (p[0] == '0' && (wleft < 0 || wleft > 1) && (p[1] == 'x' || p[1] == 'X')) {
                        if (tn + 2 < (int)sizeof(tok)) {
                            tok[tn++] = *p++;
                            tok[tn++] = *p++;
                            consumed += 2;
                            if (wleft > 0)
                                wleft -= 2;
                        }
                    }
                    digits = scan_take(&p, &consumed, wleft, tok + tn,
                                       (int)sizeof(tok) - tn, scan_isxdigit);
                    /* A bare 0x converts just the 0; give the x back. */
                    if (digits == 0 && tn - (neg ? 1 : 0) == 2) {
                        p--;
                        consumed--;
                        tn--;
                        if (wleft > 0)
                            wleft++;
                    }
                } else {
                    base = 10;
                    digits = scan_take(&p, &consumed, wleft, tok + tn,
                                       (int)sizeof(tok) - tn, scan_isdigit);
                }
                tn += digits;
                tok[tn < (int)sizeof(tok) ? tn : (int)sizeof(tok) - 1] = 0;
                if (digits == 0)
                    break;
                if (conv == 'd' || conv == 'i') {
                    long v = strtol(tok, &eptr, base);
                    if (eptr == tok)
                        break;
                    store_signed(&ap, len, (long long)v, &assigned, nosave);
                } else if (conv == 'p') {
                    unsigned long long v = strtoull(tok, &eptr, 16);
                    if (eptr == tok)
                        break;
                    if (!nosave) {
                        *va_arg(ap, void **) = (void *)(uintptr_t)v;
                        assigned++;
                    }
                } else {
                    unsigned long long v = strtoull(tok, &eptr, base);
                    if (eptr == tok)
                        break;
                    store_unsigned(&ap, len, v, &assigned, nosave);
                }
                (void)neg;
            } else if (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' ||
                       conv == 'g' || conv == 'G' || conv == 'a' || conv == 'A') {
                char tok[128];
                int tn = 0;
                int cap = (int)sizeof(tok) - 1;
                int w = wleft;
                if ((*p == '+' || *p == '-') && (w < 0 || w > 0)) {
                    if (tn < cap) {
                        tok[tn++] = *p;
                    }
                    p++;
                    consumed++;
                    if (w > 0)
                        w--;
                }
                if (p[0] == '0' && (w < 0 || w > 1) && (p[1] == 'x' || p[1] == 'X')) {
                    while ((w < 0 || tn < w) && tn < cap && *p &&
                           (scan_isxdigit((unsigned char)*p) || *p == '.' || *p == 'x' ||
                            *p == 'X')) {
                        if ((*p == 'x' || *p == 'X') && tn > 1)
                            break;
                        tok[tn++] = *p++;
                        consumed++;
                    }
                    while ((w < 0 || tn < w) && tn < cap && *p && (*p == 'p' || *p == 'P')) {
                        tok[tn++] = *p++;
                        consumed++;
                        if ((w < 0 || tn < w) && tn < cap && *p && (*p == '+' || *p == '-')) {
                            tok[tn++] = *p++;
                            consumed++;
                        }
                        while ((w < 0 || tn < w) && tn < cap && *p &&
                               scan_isdigit((unsigned char)*p)) {
                            tok[tn++] = *p++;
                            consumed++;
                        }
                        break;
                    }
                } else {
                    while ((w < 0 || tn < w) && tn < cap && *p && scan_isdigit((unsigned char)*p)) {
                        tok[tn++] = *p++;
                        consumed++;
                    }
                    if ((w < 0 || tn < w) && tn < cap && *p == '.') {
                        tok[tn++] = *p++;
                        consumed++;
                        while ((w < 0 || tn < w) && tn < cap && *p &&
                               scan_isdigit((unsigned char)*p)) {
                            tok[tn++] = *p++;
                            consumed++;
                        }
                    }
                    if (tn == 0 || (tn == 1 && tok[0] == '.')) {
                        if ((w < 0 || w >= 3) && tn + 3 < cap && *p &&
                            (*p == 'i' || *p == 'I' || *p == 'n' || *p == 'N')) {
                            int c0 = scan_tolower((unsigned char)p[0]);
                            int c1 = scan_tolower((unsigned char)p[1]);
                            int c2 = scan_tolower((unsigned char)p[2]);
                            if ((c0 == 'i' && c1 == 'n' && c2 == 'f') ||
                                (c0 == 'n' && c1 == 'a' && c2 == 'n')) {
                                tok[tn++] = *p++;
                                tok[tn++] = *p++;
                                tok[tn++] = *p++;
                                consumed += 3;
                            } else {
                                break;
                            }
                        } else {
                            break;
                        }
                    } else if ((w < 0 || tn < w) && tn < cap && *p && (*p == 'e' || *p == 'E')) {
                        const char *savep = p;
                        int savec = consumed;
                        int savetn = tn;
                        tok[tn++] = *p++;
                        consumed++;
                        if ((w < 0 || tn < w) && tn < cap && *p && (*p == '+' || *p == '-')) {
                            tok[tn++] = *p++;
                            consumed++;
                        }
                        {
                            int dn = 0;
                            while ((w < 0 || tn < w) && tn < cap && *p &&
                                   scan_isdigit((unsigned char)*p)) {
                                tok[tn++] = *p++;
                                consumed++;
                                dn++;
                            }
                            if (dn == 0) {
                                p = savep;
                                consumed = savec;
                                tn = savetn;
                            }
                        }
                    }
                }
                tok[tn] = 0;
                if (tn == 0)
                    break;
                {
                    double v = strtod(tok, NULL);
                    if (!nosave) {
                        if (len == 3 || len == 4)
                            *va_arg(ap, double *) = v;
                        else
                            *va_arg(ap, float *) = (float)v;
                        assigned++;
                    }
                }
            } else if (conv == 's') {
                int n = (wleft > 0) ? wleft : INT_MAX;
                if (!nosave) {
                    char *dst = va_arg(ap, char *);
                    int k = 0;
                    while (n > 0 && *p && !isspace((unsigned char)*p)) {
                        dst[k++] = *p++;
                        consumed++;
                        n--;
                    }
                    dst[k] = 0;
                    if (k == 0)
                        break;
                    assigned++;
                } else {
                    int k = 0;
                    while (n > 0 && *p && !isspace((unsigned char)*p)) {
                        p++;
                        consumed++;
                        n--;
                        k++;
                    }
                    if (k == 0)
                        break;
                }
            } else if (conv == 'c') {
                int n = (wleft > 0) ? wleft : 1;
                if (!nosave) {
                    char *dst = va_arg(ap, char *);
                    int k;
                    for (k = 0; k < n; k++) {
                        if (!*p)
                            break;
                        dst[k] = *p++;
                        consumed++;
                    }
                    if (k == 0) {
                        input_failed = (assigned == 0);
                        break;
                    }
                    assigned++;
                } else {
                    int k;
                    for (k = 0; k < n && *p; k++) {
                        p++;
                        consumed++;
                    }
                    if (k == 0) {
                        input_failed = (assigned == 0);
                        break;
                    }
                }
            } else if (conv == '[') {
                char set[256];
                int negset = 0;
                int k = 0;
                int n = (wleft > 0) ? wleft : INT_MAX;
                memset(set, 0, sizeof(set));
                if (*f == '^') {
                    negset = 1;
                    f++;
                }
                if (*f == ']') {
                    set[(unsigned char)']'] = 1;
                    f++;
                }
                while (*f && *f != ']') {
                    if (f[1] == '-' && f[2] && f[2] != ']') {
                        int lo = (unsigned char)f[0];
                        int hi = (unsigned char)f[2];
                        int c;
                        for (c = lo; c <= hi; c++)
                            set[c] = 1;
                        f += 3;
                    } else {
                        set[(unsigned char)*f] = 1;
                        f++;
                    }
                }
                if (*f == ']')
                    f++;
                if (!nosave) {
                    char *dst = va_arg(ap, char *);
                    while (n > 0 && *p && (set[(unsigned char)*p] != 0) != (negset != 0)) {
                        dst[k++] = *p++;
                        consumed++;
                        n--;
                    }
                    dst[k] = 0;
                    if (k == 0)
                        break;
                    assigned++;
                } else {
                    while (n > 0 && *p && (set[(unsigned char)*p] != 0) != (negset != 0)) {
                        p++;
                        consumed++;
                        n--;
                        k++;
                    }
                    if (k == 0)
                        break;
                }
            } else {
                break;
            }
        }
    }
    if (input_failed)
        return -1;
    return assigned;
}

long wcstol(const wchar_t *nptr, wchar_t **endptr, int base)
{
    const wchar_t *p = nptr;
    int neg = 0;
    unsigned long acc = 0;
    int any = 0;
    int overflow = 0;

    while (*p == L' ' || (*p >= L'\t' && *p <= L'\r'))
        p++;
    if (*p == L'+') {
        p++;
    } else if (*p == L'-') {
        neg = 1;
        p++;
    }
    if (base == 0) {
        if (*p == L'0') {
            if (p[1] == L'x' || p[1] == L'X') {
                base = 16;
                p += 2;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && p[0] == L'0' && (p[1] == L'x' || p[1] == L'X')) {
        p += 2;
    }
    if (base < 2 || base > 36) {
        errno = EINVAL;
        if (endptr)
            *endptr = (wchar_t *)nptr;
        return 0;
    }
    for (;; p++) {
        int d;
        wchar_t c = *p;
        if (c >= L'0' && c <= L'9')
            d = (int)(c - L'0');
        else if (c >= L'a' && c <= L'z')
            d = (int)(c - L'a') + 10;
        else if (c >= L'A' && c <= L'Z')
            d = (int)(c - L'A') + 10;
        else
            break;
        if (d >= base)
            break;
        any = 1;
        if (acc > (ULONG_MAX - (unsigned long)d) / (unsigned long)base)
            overflow = 1;
        else
            acc = acc * (unsigned long)base + (unsigned long)d;
    }
    if (!any) {
        if (endptr)
            *endptr = (wchar_t *)nptr;
        return 0;
    }
    if (endptr)
        *endptr = (wchar_t *)p;
    if (overflow || (!neg && acc > (unsigned long)LONG_MAX) ||
        (neg && acc > (unsigned long)LONG_MAX + 1u)) {
        errno = ERANGE;
        return neg ? LONG_MIN : LONG_MAX;
    }
    return neg ? -(long)acc : (long)acc;
}

#endif /* defined(__SWITCH__) && defined(SDL_DNRO_BUILD) */
