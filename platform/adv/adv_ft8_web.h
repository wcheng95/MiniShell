#pragma once
/* ADV built-in launcher only; failure of optional networking never skips entry. */
int adv_ft8_web_run(int argc, char **argv, int (*entry)(int, char **));
