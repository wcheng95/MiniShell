#!/usr/bin/env python3
"""One-time V2 oracle, not used by CTest. Usage: generate.py /path/to/Mini-FT8 > vectors.h"""
from pathlib import Path
import subprocess
import sys
import tempfile

PIN = "491e757ae6b1e4cfd2b9a6ba10f48b35643849e0"
FILES = ["ft8/" + name for name in (
    "message.c", "message.h", "text.c", "text.h", "encode.c", "encode.h",
    "constants.c", "constants.h", "crc.c", "crc.h", "debug.h")]
FILES += ["common/stpcpy_compat.c", "common/stpcpy_compat.h"]
HARNESS = r'''
#include "ft8/message.h"
#include "ft8/encode.h"
#include <stdio.h>
#include <string.h>
static const struct { const char *text; int free_text; } cases[] = {
    {"CQ W1XYZ FN42", 0},
    {"W1ABC K9XYZ FN42", 0},
    {"W1ABC K9XYZ -12", 0},
    {"W1ABC K9XYZ R-08", 0},
    {"W1ABC K9XYZ RR73", 0},
    {"W1ABC K9XYZ 73", 0},
    {"CQ SOTA W1XYZ FN42", 0},
    {"CQ POTA W1XYZ FN42", 0},
    {"CQ QRP W1XYZ FN42", 0},
    {"CQ FD W1XYZ FN42", 0},
    {"73 GL", 1},
    {"W1ABC K9XYZ 1D DX", 0},
    {"W1ABC K9XYZ R 16B SCV", 0},
    {"W1ABC K9XYZ 17A EMA", 0},
    {"W1ABC K9XYZ R 32F AB", 0},
    {"W1ABC K9XYZ/P FN42", 0},
    {"W1ABC/R K9XYZ R-08", 0},
    {"CQ 3DA0XYZ FN42", 0},
    {"CQ 3XA0XYZ FN42", 0},
    {"W1ABC K9XYZ -30", 0},
    {"W1ABC K9XYZ +00", 0},
    {"W1ABC K9XYZ +49", 0},
    {"W1ABC K9XYZ +99", 0},
    {"0123456789+-?", 1},
    {"TEST CQ", 1},
    {"W1AW/9 AG6AQ CM97", 0},
    {"W1AW/9 AG6AQ -12", 0},
    {"W1AW/9 AG6AQ R-08", 0},
    {"W1AW/9 AG6AQ RR73", 0},
    {"W1AW/9 AG6AQ 73", 0},
    {"CQ W1AW/9", 0},
    {"AG6AQ W1AW/9 CM97", 0},
    {"AG6AQ W1AW/9", 2},
    {"AG6AQ W1AW/9 RRR", 2},
    {"AG6AQ W1AW/9 RR73", 2},
    {"AG6AQ W1AW/9 73", 2}
};
int main(void) {
    puts("/* Generated from wcheng95/Mini-FT8 " PIN ". See README.md. */");
    puts("static const struct { const char *text; uint8_t payload[10]; const char *tones; } vectors[] = {");
    for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        ftx_message_t message = {0};
        int rc = cases[c].free_text == 2 ?
            ftx_message_encode_nonstd(&message, NULL, "AG6AQ", "W1AW/9",
                strlen(cases[c].text) > 12 ? cases[c].text + 13 : "") : cases[c].free_text ? ftx_message_encode_free(&message, cases[c].text) :
                                     ftx_message_encode(&message, NULL, cases[c].text);
        if (rc != FTX_MESSAGE_RC_OK) return 1;
        uint8_t tones[79];
        ft8_encode(message.payload, tones);
        printf("    {\"%s\", {", cases[c].text);
        for (unsigned i = 0; i < 10; ++i) printf("%s0x%02x", i ? ", " : "", message.payload[i]);
        printf("},\n     \"");
        for (unsigned i = 0; i < 79; ++i) printf("%u", tones[i]);
        puts("\"},");
    }
    puts("};");
    return 0;
}
'''


def main():
    repo = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="ft8-v2-vectors-") as temp:
        root = Path(temp)
        for name in FILES:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(subprocess.check_output([
                "git", "-C", str(repo), "show", f"{PIN}:components/ft8_lib/{name}"]))
        (root / "oracle.c").write_text(HARNESS)
        sources = [str(root / name) for name in FILES if name.endswith(".c")]
        subprocess.run(["cc", "-std=c11", "-O2", f'-DPIN="{PIN}"', "-I", str(root),
                        str(root / "oracle.c"), *sources, "-o", str(root / "oracle")], check=True)
        subprocess.run([str(root / "oracle")], check=True)


if __name__ == "__main__":
    main()
