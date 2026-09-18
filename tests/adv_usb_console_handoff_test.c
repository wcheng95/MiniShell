#include "adv_usb_console_handoff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
static char trace[128], failing;
static size_t count;
static bool phy_busy;
static int operation(char op)
{
    CHECK(count + 1 < sizeof(trace));
    trace[count++] = op;
    trace[count] = 0;
    if (op == 'D' || op == 'R') CHECK(!phy_busy);
    return failing == op ? -1 : 0;
}
static int suspend(void) { return operation('S'); }
static int uart_begin(void) { return operation('U'); }
static int uart_end(void) { return operation('D'); }
static int resume(void) { return operation('R'); }
static const adv_usb_console_ops_t ops = {suspend, uart_begin, uart_end, resume};
static void reset(char failure)
{
    count = 0;
    trace[0] = 0;
    failing = failure;
    phy_busy = false;
}
int main(void)
{
    adv_usb_console_handoff_t state = {0};
    reset(0);
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        CHECK(adv_usb_console_begin(&state, &ops));
        CHECK(state.suspended && state.uart_attempted);
        CHECK(!adv_usb_console_begin(&state, &ops));
        phy_busy = true; /* partial prepare or failed device/class/host release */
        size_t before = count;
        for (unsigned retry = 0; retry < 3; ++retry)
            CHECK(!adv_usb_console_end(&state, &ops, phy_busy));
        CHECK(count == before && state.suspended && state.uart_attempted);
        phy_busy = false;
        CHECK(adv_usb_console_end(&state, &ops, phy_busy));
        CHECK(!state.suspended && !state.uart_attempted);
        CHECK(adv_usb_console_end(&state, &ops, false));
    }
    CHECK(strcmp(trace, "SUDRSUDRSUDR") == 0);

    reset('S');
    CHECK(!adv_usb_console_begin(&state, &ops));
    CHECK(!state.suspended && !state.uart_attempted);
    CHECK(adv_usb_console_end(&state, &ops, false));
    CHECK(strcmp(trace, "S") == 0);

    reset('U'); /* UART setup can fail after acquiring some resources. */
    CHECK(!adv_usb_console_begin(&state, &ops));
    CHECK(state.suspended && state.uart_attempted);
    failing = 'D';
    CHECK(!adv_usb_console_end(&state, &ops, false));
    CHECK(state.suspended && state.uart_attempted);
    CHECK(strcmp(trace, "SUD") == 0);
    failing = 'R';
    CHECK(!adv_usb_console_end(&state, &ops, false));
    CHECK(state.suspended && !state.uart_attempted);
    failing = 0;
    CHECK(adv_usb_console_end(&state, &ops, false));
    CHECK(strcmp(trace, "SUDDRR") == 0);
    CHECK(!state.suspended && !state.uart_attempted);

    reset(0); /* Host install fails: unwind UART then restore the console. */
    CHECK(adv_usb_console_begin(&state, &ops));
    CHECK(adv_usb_console_end(&state, &ops, false));
    CHECK(strcmp(trace, "SUDR") == 0);
    puts("ADV USB console lease: repeated entry, prepare failure, release/retry PASS");
    return 0;
}
