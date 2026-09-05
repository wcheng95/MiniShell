#include "test_support.h"

#include <string.h>

typedef bool (*test_fn_t)(void);
typedef struct { const char *name; test_fn_t fn; } test_case_t;

static const test_case_t tests[] = {
    {"system", test_system},
    {"memory", test_memory},
    {"filesystem", test_filesystem},
    {"time_location", test_time_location},
    {"display", test_display},
    {"input", test_input},
    {"transfer", test_transfer},
    {"power", test_power},
    {"nano_buffer", test_nano_buffer},
    {"cp_copy", test_cp_copy},
};

int main(int argc, char **argv)
{
    int failures = 0;
    int ran = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (argc > 1 && strcmp(argv[1], tests[i].name) != 0) continue;
        ++ran;
        bool ok = tests[i].fn();
        printf("[%s] %s\n", ok ? "PASS" : "FAIL", tests[i].name);
        if (!ok) ++failures;
    }
    if (ran == 0) {
        fprintf(stderr, "unknown test group\n");
        return 2;
    }
    return failures == 0 ? 0 : 1;
}
