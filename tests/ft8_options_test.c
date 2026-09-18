#include <stdio.h>

#define main ft8_application_main
#include "../apps/ft8/main/ft8_main.c"
#undef main

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define PARSE(...) parse_options((int)(sizeof((char *[]){__VA_ARGS__}) / sizeof(char *)), \
                                 (char *[]){__VA_ARGS__}, &options)

int main(void)
{
    Ft8Options options;
    CHECK(PARSE("ft8"));
#ifdef FT8_TEST_LINUX_DEFAULTS
    CHECK(options.presentation == FT8_PRESENTATION_ADV);
    CHECK(options.rx_endpoint && strcmp(options.rx_endpoint, "alsa:hw:2,0") == 0);
#else
    CHECK(options.presentation == FT8_PRESENTATION_DESKTOP);
    CHECK(options.rx_endpoint == NULL);
#endif
    CHECK(!options.has_rx_slot);
    CHECK(options.cat_endpoint == NULL);
    CHECK(PARSE("ft8", "--cat", "serial:/dev/ttyACM0"));
    CHECK(strcmp(options.cat_endpoint, "serial:/dev/ttyACM0") == 0);
    CHECK(!PARSE("ft8", "--cat"));
    CHECK(!PARSE("ft8", "--cat", ""));
    CHECK(!PARSE("ft8", "--cat", "--profile", "adv"));
    CHECK(!PARSE("ft8", "--cat", "one", "--cat", "two"));
    CHECK(PARSE("ft8", "--profile", "desktop"));
    CHECK(options.presentation == FT8_PRESENTATION_DESKTOP);

    CHECK(PARSE("ft8", "--rx", "/flash/kfs.wav", "--rx-slot", "12345"));
    CHECK(strcmp(options.rx_endpoint, "/flash/kfs.wav") == 0);
    CHECK(options.has_rx_slot && options.rx_slot_id == 12345);
    CHECK(PARSE("ft8", "--rx-slot", "12345", "--rx", "/flash/kfs.wav",
                "--profile", "desktop"));
    CHECK(options.presentation == FT8_PRESENTATION_DESKTOP);
    CHECK(strcmp(options.rx_endpoint, "/flash/kfs.wav") == 0);
    CHECK(options.has_rx_slot && options.rx_slot_id == 12345);
    CHECK(PARSE("ft8", "--profile", "adv", "--rx", "alsa:hw:2,0"));
    CHECK(options.presentation == FT8_PRESENTATION_ADV);
    CHECK(strcmp(options.rx_endpoint, "alsa:hw:2,0") == 0 && !options.has_rx_slot);

    CHECK(!PARSE("ft8", "--rx-slot", "12345"));
    CHECK(!PARSE("ft8", "--rx", "one", "--rx", "two"));
    CHECK(!PARSE("ft8", "--rx", "one", "--rx-slot", "1", "--rx-slot", "2"));
    CHECK(!PARSE("ft8", "--rx", "one", "--rx-slot", "bad"));
    CHECK(!PARSE("ft8", "--rx", ""));
    CHECK(!PARSE("ft8", "--rx"));
    CHECK(!PARSE("ft8", "--profile", "invalid"));
    CHECK(!PARSE("ft8", "--unknown"));
    puts("FT8 composition defaults, explicit overrides, fixture validation: PASS");
    return 0;
}
