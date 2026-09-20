#!/usr/bin/env python3
"""Execute credential selection/display excerpts and check the SDK boundary."""
from pathlib import Path
import subprocess
import sys
import tempfile
root = Path(sys.argv[1]).resolve()
wifi = (root/'platform/adv/adv_webfs_wifi.c').read_text()
app = (root/'platform/adv/adv_webfs.c').read_text()
loader = (root/'platform/adv/adv_webfs_settings.c').read_text()
for text in ('init.nvs_enable = 0;', 'esp_wifi_set_storage(WIFI_STORAGE_RAM)',
             'esp_wifi_set_mode(WIFI_MODE_AP)', 'config.ap.max_connection = 1;',
             'config.ap.authmode = WIFI_AUTH_WPA2_PSK;', 'ESP_IP4TOADDR(192, 168, 4, 1)'):
    assert text in wifi, text
for text in ('fopen(', 'fread(', 'f_close(', 'esp_vfs', 'malloc(', 'ESP_LOG', 'printf('):
    assert text not in loader, text
assert 'fs->open("/flash/minishell/setting.txt", MINI_FS_READ' in loader
assert app.count('webfs_settings_load(') == 1
assert app.index('webfs_settings_load(') < app.index('webfs_wifi_start(') < app.index('webfs_http_start(')
assert 'configured ? &credentials : NULL' in app
assert 'setting.txt' not in wifi and 'mini_api_get' not in wifi
assert 'WIFI_MODE_STA' not in wifi and 'nvs_set' not in wifi
for line in (app + wifi).splitlines():
    if 'ESP_LOG' in line or '->console->write' in line or '->system->write' in line:
        assert 'password' not in line and 'credentials' not in line
selection = wifi[wifi.index('    if (credentials) {'):wifi.index('    esp_netif_config_t net')]
config = wifi[wifi.index('    wifi_config_t config = {0};'):wifi.index('    rc = esp_wifi_start();')]
display = app[app.index('        if (strlen(wifi.ssid)'):app.index('        api->display->present();',app.index('        if (strlen(wifi.ssid)'))]
line_fn = app[app.index('static void line('):app.index('static void heap_report(')]
harness = r'''
#include "adv_webfs_wifi.h"
#include "adv_webfs_logic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned rng_calls, entropy_calls;
static unsigned char seed;
static void esp_fill_random(void *out,size_t size)
{ ++rng_calls;for(size_t i=0;i<size;++i)((unsigned char *)out)[i]=seed++; }
static esp_err_t esp_wifi_set_promiscuous(bool enabled)
{ assert(enabled==(entropy_calls%2==0));++entropy_calls;return ESP_OK; }
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_IF_AP 1
typedef struct { struct { unsigned char ssid[32],password[64];unsigned ssid_len,channel,max_connection,authmode; } ap; } wifi_config_t;
static wifi_config_t captured;
static esp_err_t esp_wifi_set_config(int interface,const wifi_config_t *config)
{ assert(interface==WIFI_IF_AP);captured=*config;return ESP_OK; }
static char screen[7][21];
static mini_result_t write_at(uint32_t row,uint32_t column,const char *text,uint32_t length)
{ assert(row<7 && column==0 && length<=20);memcpy(screen[row],text,length);return MINI_OK; }
'''
tests = r'''
static void show(const webfs_wifi_t *w)
{
    const mini_text_display_api_t text={.write_at=write_at};
    const mini_display_api_t display_api={.text=&text};
    const mini_api_t storage={.display=&display_api};const mini_api_t *api=&storage;
    webfs_wifi_t wifi=*w;
    memset(screen,0,sizeof(screen));
DISPLAY
}
int main(void)
{
    webfs_wifi_t wifi={0};webfs_credentials_t pair={0};
    memset(pair.ssid,'S',32);memset(pair.password,'P',63);pair.password[8]='=';pair.password[9]=' ';
    assert(select_credentials(&wifi,&pair)==ESP_OK);
    assert(!rng_calls && !entropy_calls && !memcmp(wifi.ssid,pair.ssid,33) && !memcmp(wifi.password,pair.password,64));
    assert(captured.ap.ssid_len==32 && !memcmp(captured.ap.ssid,pair.ssid,32));
    assert(!memcmp(captured.ap.password,pair.password,64) && captured.ap.max_connection==1 && captured.ap.authmode==WIFI_AUTH_WPA2_PSK);
    show(&wifi);char combined[128]={0};
    for(unsigned i=0;i<6;++i)strcat(combined,screen[i]);
    assert(!strncmp(combined,"SSID ",5) && !memcmp(combined+5,pair.ssid,32));
    assert(!strncmp(combined+37,"PW ",3) && !strcmp(combined+40,pair.password));
    assert(!strcmp(screen[6],"192.168.4.1 Q/Esc"));
    strcpy(wifi.ssid,"SSID");strcpy(wifi.password,"12345678901234567");show(&wifi);
    assert(!strcmp(screen[2],"PW 12345678901234567"));
    memset(&wifi,0,sizeof(wifi));assert(select_credentials(&wifi,NULL)==ESP_OK);
    assert(entropy_calls==2 && rng_calls && strlen(wifi.password)==8 && !wifi.entropy_rx);
    assert(!strncmp(wifi.ssid,"MiniShell-",10) && strlen(wifi.ssid)==14);
    for(unsigned i=0;i<8;++i)assert(strchr("ABCDEFGHJKMNPQRSTUVWXYZ",wifi.password[i]));
    char first[9];strcpy(first,wifi.password);
    memset(&wifi,0,sizeof(wifi));assert(select_credentials(&wifi,NULL)==ESP_OK);
    assert(strcmp(first,wifi.password) && entropy_calls==4);
    show(&wifi);assert(!strcmp(screen[0],"WebFS") && !strcmp(screen[5],"Q / Esc = stop"));
    puts("adv_webfs_wifi_settings: PASS");return 0;
}
'''.replace('DISPLAY', display)
with tempfile.TemporaryDirectory(prefix='webfs-wifi-settings-') as tmp:
    directory=Path(tmp)
    (directory/'esp_err.h').write_text('typedef int esp_err_t;\n#define ESP_OK 0\n')
    (directory/'esp_netif.h').write_text('typedef struct esp_netif esp_netif_t;\n')
    c=directory/'test.c';exe=directory/'test'
    c.write_text(harness+'\nstatic esp_err_t select_credentials(webfs_wifi_t *wifi, const webfs_credentials_t *credentials) { esp_err_t rc;\n'+selection+config+'return rc;\n}\n'+line_fn+tests)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic',
                    '-I'+tmp,'-I'+str(root/'include'),'-I'+str(root/'platform/adv'),str(c),
                    str(root/'platform/adv/adv_webfs_logic.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
