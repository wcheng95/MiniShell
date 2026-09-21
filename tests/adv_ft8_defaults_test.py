#!/usr/bin/env python3
"""Execute the ADV wrapper with a recording portable entry point."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = Path(sys.argv[2]) if len(sys.argv) > 2 else root / 'platform/adv/main/ft8_static.c'
wrapper = source.read_text().split('int minishell_app_ft8_main', 1)[1]
parser = (root / 'apps/ft8/main/ft8_main.c').read_text().split('int main(', 1)[0]
includes = [root / p for p in ('include', 'core/minishell_services', 'platform/adv',
    'apps/ft8/include', 'apps/ft8/main', 'apps/ft8/src/app_controller',
    'apps/ft8/src/presentation_profile', 'apps/ft8/src/ui_shell')]
harness = r'''
#include "adv_internal.h"
const mini_api_t *mini_api_get(void) { return NULL; }
static int probes, releases;
mini_result_t adv_qmx_discovery_begin(void) { ++probes; return MINI_OK; }
mini_result_t adv_qmx_discovery_end(void) { ++releases; return MINI_OK; }

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
static int expected_count;
static const char **expected;
static int adv_ft8_entry(int argc, char **argv)
{
    assert(argc == expected_count);
    assert(argv[argc] == NULL);
    for (int i = 0; i < argc; ++i) assert(strcmp(argv[i], expected[i]) == 0);
    return 17;
}
'''
cases = [
    (['ft8'], ['ft8', '--rx', 'uac:qmx', '--cat', 'serial:qmx']),
    (['ft8', '--cat', 'explicit'], ['ft8', '--cat', 'explicit', '--rx', 'uac:qmx']),
    (['ft8', '--rx', '/sd/test.wav'], ['ft8', '--rx', '/sd/test.wav']),
    (['ft8', '--rx', 'uac:qmx'], ['ft8', '--rx', 'uac:qmx', '--cat', 'serial:qmx']),
    (['ft8', '--rx', '/sd/test.wav', '--cat', 'explicit'],
     ['ft8', '--rx', '/sd/test.wav', '--cat', 'explicit']),
    (['ft8', '--cat-test-tone', '1500', '--cat-test-ms', '500'],
     ['ft8', '--cat-test-tone', '1500', '--cat-test-ms', '500', '--cat', 'serial:qmx']),
    (['ft8', '--cat', 'explicit', '--cat-test-tone', '1500', '--cat-test-ms', '500'],
     ['ft8', '--cat', 'explicit', '--cat-test-tone', '1500', '--cat-test-ms', '500']),
    # Preserve malformed explicit options for the portable parser to reject.
    (['ft8', '--rx'], ['ft8', '--rx']),
    (['ft8', '--cat'], ['ft8', '--cat', '--rx', 'uac:qmx']),
]
import json
main = '\nint main(void) {\n'
for index, (args, result) in enumerate(cases):
    main += '{ probes = releases = 0; char *args[] = {' + ','.join(json.dumps(a) for a in args) + ',NULL};\n'
    main += 'const char *want[] = {' + ','.join(json.dumps(a) for a in result) + '};\n'
    main += f'expected = want; expected_count = {len(result)}; assert(minishell_app_ft8_main({len(args)}, args) == 17); assert(probes == {int(index in (0, 3))} && releases == probes); }}\n'
main += '''assert(minishell_app_ft8_main(0, NULL) == 2);
char *many[32]; for (int i = 0; i < 32; ++i) many[i] = "ft8";
assert(minishell_app_ft8_main(32, many) == 2);
return 0; }
'''
with tempfile.TemporaryDirectory(prefix='t030-defaults-') as temp:
    path = Path(temp) / 'defaults.c'
    binary = Path(temp) / 'defaults'
    path.write_text(parser + harness + 'int minishell_app_ft8_main' + wrapper + main)
    subprocess.run([sys.argv[1] if len(sys.argv) > 1 else 'cc', '-std=c11',
                    '-Wall', '-Wextra', '-Werror', *[arg for inc in includes for arg in ('-I', str(inc))],
                    str(path), str(root / 'apps/ft8/src/presentation_profile/presentation_profile.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('ADV FT8 defaults: PASS')
