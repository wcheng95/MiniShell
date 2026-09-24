#include "rtty_ita2.h"
char rtty_ita2_decode(uint8_t *figures, unsigned code)
{
    /* The pinned encoder's ITA2 variant, including its figures punctuation. */
    static const char letters[32] = "\0E\nA SIU\rDRJNFCKTZLWHYPQOBG\0MXV\0";
    static const char numbers[32] = {'\0','3','\n','-',' ','\'','8','7','\r','$','4','\a',',','!',':','(','5','"',')','2','#','6','0','1','9','?','&','\0','.','/',';','\0'};
    if (!figures || code > 31) return 0;
    if (code == 31) { *figures = 0; return 0; }
    if (code == 27) { *figures = 1; return 0; }
    return *figures ? numbers[code] : letters[code];
}
