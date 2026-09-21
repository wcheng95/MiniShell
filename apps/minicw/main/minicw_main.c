#include "minicw_run.h"
int app_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return minicw_run(mini_api_get());
}
