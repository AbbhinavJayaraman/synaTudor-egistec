// MultiByteToWideChar / WideCharToMultiByte used to convert one character and
// stop. These check full strings, the -1 "NUL terminated" length, and the
// zero-destination size query.
#include <stdio.h>
#include <string.h>
#include <uchar.h>

__attribute__((ms_abi)) extern int MultiByteToWideChar(unsigned cp, unsigned flags, const char *mb, int mblen, char16_t *w, int wlen);
__attribute__((ms_abi)) extern int WideCharToMultiByte(unsigned cp, unsigned flags, const char16_t *w, int wlen, char *mb, int mblen, const char *dc, int *ud);

static int fails = 0;
static void expect(const char *what, int ok, const char *detail) {
    if(!ok) fails++;
    printf("%-52s %s  %s\n", what, ok ? "PASS" : "FAIL", detail);
}

int main(void) {
    const char *ansi = "SYSTEM\\CurrentControlSet\\Enum";
    char16_t w[256]; char mb[256]; char shown[256];

    int n = MultiByteToWideChar(65001, 0, ansi, -1, w, 256);
    for(int i = 0; i < n; i++) shown[i] = (char) w[i];
    expect("MB2WC: whole string, terminator counted",
           n == (int) strlen(ansi) + 1 && strcmp(shown, ansi) == 0, shown);

    int need = MultiByteToWideChar(65001, 0, ansi, -1, NULL, 0);
    expect("MB2WC: size query writes nothing", need == (int) strlen(ansi) + 1, need == (int) strlen(ansi) + 1 ? "ok" : "wrong size");

    int m = WideCharToMultiByte(65001, 0, w, -1, mb, sizeof(mb), NULL, NULL);
    expect("WC2MB: round trip back to the same string",
           m == (int) strlen(ansi) + 1 && strcmp(mb, ansi) == 0, mb);

    int need2 = WideCharToMultiByte(65001, 0, w, -1, NULL, 0, NULL, NULL);
    expect("WC2MB: size query writes nothing", need2 == (int) strlen(ansi) + 1, need2 == (int) strlen(ansi) + 1 ? "ok" : "wrong size");

    // Explicit length: no terminator is added or counted.
    int m2 = WideCharToMultiByte(65001, 0, w, 6, mb, sizeof(mb), NULL, NULL);
    mb[m2] = 0;
    expect("WC2MB: explicit length adds no terminator", m2 == 6 && strcmp(mb, "SYSTEM") == 0, mb);

    // Too small a buffer must fail rather than truncate silently.
    int m3 = WideCharToMultiByte(65001, 0, w, -1, mb, 4, NULL, NULL);
    expect("WC2MB: refuses an undersized buffer", m3 == 0, m3 == 0 ? "returns 0" : "wrote anyway");

    printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
