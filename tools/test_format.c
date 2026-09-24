// Checks wsprintfA/W against known-correct output. Declared ms_abi here so the
// call really goes through the Microsoft vararg convention - that mismatch was
// the original bug, so a SysV-called test would prove nothing.
#include <stdio.h>
#include <string.h>
#include <uchar.h>

__attribute__((ms_abi)) extern int wsprintfA(char *dest, const char *fmt, ...);
__attribute__((ms_abi)) extern int wsprintfW(char16_t *dest, const char16_t *fmt, ...);

static int fails = 0;
static void chk(const char *what, const char *got, const char *want) {
    int ok = strcmp(got, want) == 0;
    if(!ok) fails++;
    printf("%-46s %s\n   got  \"%s\"\n   want \"%s\"\n", what, ok ? "PASS" : "FAIL", got, want);
}
static void chkw(const char *what, const char16_t *got, const char *want) {
    char narrow[512]; size_t i = 0;
    for(; got[i] && i < sizeof(narrow) - 1; i++) narrow[i] = (char) got[i];
    narrow[i] = 0;
    chk(what, narrow, want);
}

int main(void) {
    char b[1024]; char16_t w[1024];

    wsprintfA(b, "%s|%d|%u|%x|%X|%c|%%", "abc", -42, 7u, 0xbeefu, 0xbeefu, 'Z');
    chk("A: mixed conversions", b, "abc|-42|7|beef|BEEF|Z|%");

    wsprintfA(b, "[%8d][%-8d][%08d]", 42, 42, 42);
    chk("A: width, left-align, zero-pad", b, "[      42][42      ][00000042]");

    wsprintfA(b, "[%10s][%-10s][%.3s]", "hi", "hi", "truncate");
    chk("A: string width and precision", b, "[        hi][hi        ][tru]");

    wsprintfA(b, "%I64x %I64d", 0x123456789abcdefLL, -1LL);
    chk("A: Microsoft I64 modifier", b, "123456789abcdef -1");

    wsprintfA(b, "%lu %llx %zu", 4000000000UL, 0xdeadbeefcafeULL, (size_t) 99);
    chk("A: l/ll/z modifiers", b, "4000000000 deadbeefcafe 99");

    // A bare %s in a wide format is a WIDE string; %hs is narrow.
    wsprintfW(w, u"wide=%s narrow=%hs", u"UTF16", "ANSI");
    chkw("W: %s is wide, %hs is narrow", w, "wide=UTF16 narrow=ANSI");

    // The exact shape that crashed: a wide path with one wide %s argument.
    wsprintfW(w, u"SYSTEM\\CurrentControlSet\\Enum\\%s\\Device Parameters",
              u"USB\\VID_1C7A&PID_0575\\6&7");
    chkw("W: the path that used to segfault", w,
         "SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_1C7A&PID_0575\\6&7\\Device Parameters");

    // Return value is the length written.
    int n = wsprintfA(b, "12345");
    printf("%-46s %s (%d)\n", "A: returns length", n == 5 ? "PASS" : "FAIL", n);
    if(n != 5) fails++;

    // A bad pointer must be reported, not dereferenced.
    wsprintfA(b, "x%sy", (const char*) 2);
    printf("%-46s %s\n   got  \"%s\"\n", "A: bad %s pointer survives", strstr(b, "bad str") ? "PASS" : "FAIL", b);
    if(!strstr(b, "bad str")) fails++;

    printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
