#include "rtty_wav.h"
#include <stddef.h>
#define END(type,field) (offsetof(type,field)+sizeof(((type *)0)->field))
int main(int argc, char **argv)
{
    const mini_api_t *api=mini_api_get();
    if(!api || api->api_version!=MINISHELL_API_VERSION || api->struct_size<END(mini_api_t,fs) ||
       !api->console || api->console->struct_size<END(mini_console_api_t,write) || !api->console->write) return 2;
    if(argc!=2) { api->console->write("usage: rtty <path.wav>\n"); return 1; }
    const mini_fs_api_t *fs=api->fs;
    if(!fs || fs->struct_size<END(mini_fs_api_t,seek) || !fs->open || !fs->close || !fs->read || !fs->seek) {
        api->console->write("rtty: filesystem unavailable\n"); return 2;
    }
    const char *error=rtty_wav_decode(fs,api->console,argv[1]);
    if(error) { api->console->write(error); return 3; }
    return 0;
}
