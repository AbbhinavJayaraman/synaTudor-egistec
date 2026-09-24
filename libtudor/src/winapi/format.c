//Windows printf-style formatting against an MS-ABI vararg list.
//
//This exists because a variadic __winfnc cannot hand its arguments to
//libc. __winfnc is ms_abi, so its varargs arrive per the Microsoft x64
//convention (rcx/rdx/r8/r9, then the stack, with no SysV register save area).
//Building a plain va_list in such a function and passing it to vsnprintf makes
//glibc read the SysV save area, which the ms_abi prologue never wrote - so
//every argument is whatever happened to be on the stack. That is what crashed
//wsprintfW: the sensor adapter formats its instance ID into
//"SYSTEM\CurrentControlSet\Enum\%s\Device Parameters" and %s got an
//uninitialised slot, so glibc walked a garbage pointer inside __printf_buffer.
//
//So the argument list has to be walked here, with win_va_arg, and only ever
//single concrete values handed to snprintf.
//
//Two argument layouts are supported, see enum winfmt_args.

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include "internal.h"

//Widths a size modifier can select.
enum arg_width { AW_INT, AW_LONG, AW_LLONG, AW_SHORT, AW_CHAR, AW_SIZET };

//A pointer the driver handed us is not trusted. Anything in the low 64K can
//never be a real mapping (Windows reserves it, and it is where small integers
//mistaken for pointers land), and anything outside the canonical x86-64 halves
//cannot be mapped at all. Catching those keeps a bad argument from turning a
//log line into a SIGSEGV - the value is reported instead of dereferenced.
static bool winfmt_ptr_plausible(const void *p) {
    uintptr_t v = (uintptr_t) p;
    if(v < 0x10000) return false;
    if(v >= 0x0000800000000000ull && v < 0xffff800000000000ull) return false;
    return true;
}

//Prints a driver-supplied string to a stream, refusing to dereference a pointer
//that cannot be real. Used by the OutputDebugString shims, which take a bare
//string rather than a format.
void winfmt_put_checked_str(FILE *f, const void *str, bool wide) {
    if(!winfmt_ptr_plausible(str)) {
        log_warn("Driver passed an unusable string pointer: %p", str);
        fprintf(f, "(bad str %p)", str);
        return;
    }
    if(wide) {
        const char16_t *w = (const char16_t*) str;
        for(size_t i = 0; w[i]; i++) fputc((w[i] < 0x80) ? (char) w[i] : '?', f);
    } else {
        fputs((const char*) str, f);
    }
}

//Appends to dst, tracking the full length the way snprintf does so the return
//value stays meaningful even when the buffer runs out.
struct winfmt_out {
    char *dst;
    size_t size;
    size_t len;
};

static void winfmt_putc(struct winfmt_out *o, char c) {
    if(o->dst && o->len + 1 < o->size) o->dst[o->len] = c;
    o->len++;
}

static void winfmt_puts(struct winfmt_out *o, const char *s, size_t n) {
    for(size_t i = 0; i < n; i++) winfmt_putc(o, s[i]);
}

static void winfmt_printf(struct winfmt_out *o, const char *spec, ...) {
    char buf[512];
    va_list va;
    va_start(va, spec);
    int n = vsnprintf(buf, sizeof(buf), spec, va);
    va_end(va);
    if(n < 0) return;
    if(n > (int) sizeof(buf) - 1) n = (int) sizeof(buf) - 1;
    winfmt_puts(o, buf, (size_t) n);
}

//Emits a narrow string, honouring a precision limit and never reading past a
//terminator. Wide input is narrowed one unit at a time; the driver only formats
//ASCII identifiers and log text through here.
static void winfmt_put_str(struct winfmt_out *o, const void *str, bool wide, int width, int prec, bool left) {
    char bad[32];
    const char *fallback = NULL;

    if(!winfmt_ptr_plausible(str)) {
        //Loud, and visible in the output, rather than a plausible-looking empty
        //string that hides the broken argument.
        log_warn("Format string argument is not a usable pointer: %p", str);
        snprintf(bad, sizeof(bad), "(bad str %p)", str);
        fallback = bad;
    }

    size_t len = 0;
    if(fallback) {
        len = strlen(fallback);
    } else if(wide) {
        const char16_t *w = (const char16_t*) str;
        while(w[len] && (prec < 0 || len < (size_t) prec)) len++;
    } else {
        const char *c = (const char*) str;
        while(c[len] && (prec < 0 || len < (size_t) prec)) len++;
    }
    if(prec >= 0 && len > (size_t) prec) len = (size_t) prec;

    size_t pad = (width > 0 && len < (size_t) width) ? (size_t) width - len : 0;
    if(pad && !left) for(size_t i = 0; i < pad; i++) winfmt_putc(o, ' ');

    for(size_t i = 0; i < len; i++) {
        char c;
        if(fallback) c = fallback[i];
        else if(wide) {
            char16_t u = ((const char16_t*) str)[i];
            c = (u < 0x80) ? (char) u : '?';
        } else c = ((const char*) str)[i];
        winfmt_putc(o, c);
    }

    if(pad && left) for(size_t i = 0; i < pad; i++) winfmt_putc(o, ' ');
}

//Pulls one argument. In WINFMT_ARGS_PTR_SIZE each argument is a pointer to the
//value followed by its size, which is how WPP hands TraceMessage its data.
static bool winfmt_next_int(win_va_list *va, enum winfmt_args mode, enum arg_width w, long long *out) {
    if(mode == WINFMT_ARGS_PTR_SIZE) {
        void *p = win_va_arg(*va, void*);
        size_t sz = win_va_arg(*va, size_t);
        if(!winfmt_ptr_plausible(p)) {
            log_warn("Format integer argument is not a usable pointer: %p", p);
            return false;
        }
        switch(sz) {
            case 1: *out = *(const int8_t*) p; return true;
            case 2: *out = *(const int16_t*) p; return true;
            case 8: *out = *(const int64_t*) p; return true;
            default: *out = *(const int32_t*) p; return true;
        }
    }

    switch(w) {
        case AW_LLONG: *out = win_va_arg(*va, long long); break;
        case AW_LONG:  *out = win_va_arg(*va, long); break;
        case AW_SIZET: *out = (long long) win_va_arg(*va, size_t); break;
        //Anything narrower than int is promoted, so read an int and truncate.
        case AW_SHORT: *out = (short) win_va_arg(*va, int); break;
        case AW_CHAR:  *out = (signed char) win_va_arg(*va, int); break;
        default:       *out = win_va_arg(*va, int); break;
    }
    return true;
}

static bool winfmt_next_ptr(win_va_list *va, enum winfmt_args mode, void **out) {
    *out = win_va_arg(*va, void*);
    if(mode == WINFMT_ARGS_PTR_SIZE) win_va_arg(*va, size_t);
    return true;
}

size_t winfmt_vformat(char *dst, size_t dst_size, const void *fmt, bool fmt_wide, enum winfmt_args mode, win_va_list va) {
    struct winfmt_out o = { .dst = dst, .size = dst_size, .len = 0 };

    //Narrow the format up front so there is only one scanner. A wide format
    //also changes what a bare %s means - see below.
    char *owned = NULL;
    const char *f;
    if(fmt_wide) {
        owned = winstr_to_str((const char16_t*) fmt);
        if(!owned) { if(dst && dst_size) dst[0] = 0; return 0; }
        f = owned;
    } else f = (const char*) fmt;

    for(const char *p = f; *p; p++) {
        if(*p != '%') { winfmt_putc(&o, *p); continue; }
        p++;
        if(*p == '%') { winfmt_putc(&o, '%'); continue; }

        //Flags
        bool left = false, zero = false, plus = false, space = false, alt = false;
        for(;; p++) {
            if(*p == '-') left = true;
            else if(*p == '0') zero = true;
            else if(*p == '+') plus = true;
            else if(*p == ' ') space = true;
            else if(*p == '#') alt = true;
            else break;
        }

        //Width
        int width = -1;
        if(*p == '*') {
            long long v = 0;
            if(!winfmt_next_int(&va, mode, AW_INT, &v)) break;
            width = (int) v;
            if(width < 0) { left = true; width = -width; }
            p++;
        } else if(*p >= '0' && *p <= '9') {
            width = 0;
            while(*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        }

        //Precision
        int prec = -1;
        if(*p == '.') {
            p++;
            if(*p == '*') {
                long long v = 0;
                if(!winfmt_next_int(&va, mode, AW_INT, &v)) break;
                prec = (int) v;
                p++;
            } else {
                prec = 0;
                while(*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0');
            }
        }

        //Size modifiers, including the Microsoft-only I/I32/I64 and w.
        enum arg_width aw = AW_INT;
        bool str_wide = fmt_wide, str_forced = false;
        for(bool more = true; more; ) {
            switch(*p) {
                case 'h':
                    aw = (aw == AW_SHORT) ? AW_CHAR : AW_SHORT;
                    str_wide = false; str_forced = true;
                    p++;
                    break;
                case 'l':
                    if(p[1] == 'l') { aw = AW_LLONG; p += 2; }
                    else { aw = AW_LONG; str_wide = true; str_forced = true; p++; }
                    break;
                case 'w': str_wide = true; str_forced = true; p++; break;
                case 'z': case 'j': case 't': aw = AW_SIZET; p++; break;
                case 'L': aw = AW_LLONG; p++; break;
                case 'I':
                    if(p[1] == '6' && p[2] == '4') { aw = AW_LLONG; p += 3; }
                    else if(p[1] == '3' && p[2] == '2') { aw = AW_INT; p += 3; }
                    else { aw = AW_SIZET; p++; }
                    break;
                default: more = false; break;
            }
        }

        //Rebuild a narrow spec for the scalar conversions. Only one concrete
        //value is ever passed, so this call is an ordinary SysV call.
        char spec[24];
        int si = 0;
        spec[si++] = '%';
        if(left) spec[si++] = '-';
        if(plus) spec[si++] = '+';
        if(space) spec[si++] = ' ';
        if(alt) spec[si++] = '#';
        if(zero) spec[si++] = '0';
        if(width >= 0) si += snprintf(spec + si, sizeof(spec) - si, "%d", width);
        if(prec >= 0) si += snprintf(spec + si, sizeof(spec) - si, ".%d", prec);

        char conv = *p;
        switch(conv) {
            case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
                long long v = 0;
                if(!winfmt_next_int(&va, mode, aw, &v)) { winfmt_puts(&o, "(bad arg)", 9); break; }
                memcpy(spec + si, "lld", 4);
                spec[si + 2] = conv == 'i' ? 'd' : conv;
                if(conv == 'd' || conv == 'i') {
                    winfmt_printf(&o, spec, v);
                } else {
                    //Unsigned conversions must not sign-extend a narrow value.
                    unsigned long long uv;
                    switch(aw) {
                        case AW_LLONG: case AW_LONG: case AW_SIZET: uv = (unsigned long long) v; break;
                        case AW_SHORT: uv = (unsigned short) v; break;
                        case AW_CHAR: uv = (unsigned char) v; break;
                        default: uv = (unsigned int) v; break;
                    }
                    winfmt_printf(&o, spec, uv);
                }
            } break;

            case 'c': case 'C': {
                //In a wide format %c is a wide character, and %C inverts that.
                bool wide_ch = (conv == 'C') ? !fmt_wide : fmt_wide;
                if(str_forced) wide_ch = (conv == 'C') ? !str_wide : str_wide;
                long long v = 0;
                if(!winfmt_next_int(&va, mode, wide_ch ? AW_SHORT : AW_CHAR, &v)) { winfmt_puts(&o, "(bad arg)", 9); break; }
                unsigned long long u = (unsigned long long) v & (wide_ch ? 0xffffu : 0xffu);
                winfmt_putc(&o, (u && u < 0x80) ? (char) u : (u ? '?' : ' '));
            } break;

            case 's': case 'S': {
                //A bare %s follows the format's own width: wide in wsprintfW,
                //narrow in wsprintfA. %S is the other one. h/l/w override both.
                bool wide = str_forced ? str_wide : fmt_wide;
                if(conv == 'S') wide = !wide;
                void *sp = NULL;
                winfmt_next_ptr(&va, mode, &sp);
                winfmt_put_str(&o, sp, wide, width, prec, left);
            } break;

            case 'p': {
                void *pv = NULL;
                winfmt_next_ptr(&va, mode, &pv);
                winfmt_printf(&o, "%p", pv);
            } break;

            case '\0':
                //Trailing '%' with nothing after it.
                winfmt_putc(&o, '%');
                p--;
                break;

            default:
                //Stopping is the only safe option: an unrecognised conversion
                //means the number of arguments consumed so far no longer lines
                //up with what the caller pushed, and guessing would make every
                //later %s read a neighbouring slot.
                log_warn("Unsupported format conversion '%%%c' in \"%s\" - truncating here", conv ? conv : '?', f);
                winfmt_puts(&o, "(unsupported %", 14);
                winfmt_putc(&o, conv);
                winfmt_putc(&o, ')');
                goto done;
        }
    }

done:
    if(dst && dst_size) dst[o.len < dst_size ? o.len : dst_size - 1] = 0;
    free(owned);
    return o.len;
}
