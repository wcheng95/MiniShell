#include "ft8_ui_adapter.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static unsigned allocations, frees, presents, blits;
static uint32_t allocation_size, width = 240, height = 135;
static bool fail_alloc;
static uint16_t physical[19][240];
static char top[21];
static mini_result_t alloc(uint32_t size, void **out)
{
    ++allocations; allocation_size = size;
    *out = fail_alloc ? NULL : malloc(size);
    return *out ? MINI_OK : MINI_ERR_NO_MEMORY;
}
static mini_result_t release(void *ptr) { ++frees; free(ptr); return MINI_OK; }
static mini_result_t info(mini_text_display_info_t *p) { p->rows=7; p->columns=20; return MINI_OK; }
static mini_result_t clear(void) { memset(top, 0, sizeof(top)); return MINI_OK; }
static mini_result_t write_row(uint32_t r, uint32_t c, const char *s, uint32_t n)
{ assert(c==0 && n==20); if(r==0) memcpy(top,s,n); return MINI_OK; }
static mini_result_t present(void)
{ ++presents; memset(physical, 0xAA, sizeof(physical)); return MINI_OK; }
static mini_result_t input(mini_key_event_t *p, uint32_t t)
{ (void)p; (void)t; return MINI_ERR_NOT_READY; }
static mini_result_t raster_info(mini_raster_display_info_t *p)
{ p->width=width; p->height=height; return MINI_OK; }
static mini_result_t blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          const uint16_t *pixels, uint32_t stride)
{
    assert(presents && x==0 && y<19 && w==240 && h==1 && stride==240);
    ++blits; memcpy(physical[y],pixels,480); return MINI_OK;
}
static uint16_t yellow(uint8_t v) { return ((v & 0xF8u)<<8)|((v & 0xFCu)<<3); }
static void check_row(unsigned y, uint8_t v)
{ for(unsigned x=0;x<240;++x) assert(physical[y][x]==yellow(v)); }
int main(void)
{
    mini_text_display_api_t text={.struct_size=sizeof(text),.get_info=info,.clear=clear,.write_at=write_row};
    mini_raster_display_api_t raster={.struct_size=sizeof(raster),.get_info=raster_info,.blit_rgb565=blit};
    mini_display_api_t display={.struct_size=sizeof(display),.capabilities=MINI_DISPLAY_CAP_TEXT,
        .text=&text,.present=present,.raster=&raster};
    mini_key_input_api_t key={.struct_size=sizeof(key),.read=input};
    mini_input_api_t in={.struct_size=sizeof(in),.capabilities=MINI_INPUT_CAP_KEY,.key=&key};
    mini_memory_api_t memory={.struct_size=sizeof(memory),.alloc=alloc,.free=release};
    mini_api_t api={.struct_size=sizeof(api),.api_version=MINISHELL_API_VERSION,
        .display=&display,.input=&in,.memory=&memory};
    ft8_ui_adapter_t adapter;
    UiFrame frame={.column_count=20,.row_count=7};
    memcpy(frame.rows[0],"RX normal status    ",20);
    assert(ft8_ui_adapter_init(&adapter,&api,FT8_PRESENTATION_ADV));
    assert(!ft8_ui_adapter_waterfall_supported(&adapter) && !allocations);
    assert(!ft8_ui_adapter_waterfall_prepare(&adapter) && !allocations);
    display.capabilities |= MINI_DISPLAY_CAP_RASTER_RGB565;
    for(unsigned variant=0;variant<7;++variant) {
        display.struct_size=variant==0?offsetof(mini_display_api_t,raster):sizeof(display);
        display.raster=variant==1?NULL:&raster;
        raster.struct_size=variant==2?offsetof(mini_raster_display_api_t,blit_rgb565):sizeof(raster);
        raster.blit_rgb565=variant==3?NULL:blit;
        raster.get_info=variant==6?NULL:raster_info;
        width=variant==4?239:240; height=variant==5?18:135;
        assert(ft8_ui_adapter_init(&adapter,&api,FT8_PRESENTATION_ADV));
        assert(!ft8_ui_adapter_waterfall_supported(&adapter) && !allocations);
        assert(!ft8_ui_adapter_waterfall_prepare(&adapter) && !allocations);
    }
    raster.get_info=raster_info;
    width=240; height=135; fail_alloc=true;
    assert(ft8_ui_adapter_init(&adapter,&api,FT8_PRESENTATION_ADV));
    assert(!ft8_ui_adapter_waterfall_supported(&adapter) && !allocations);
    assert(!ft8_ui_adapter_waterfall_prepare(&adapter));
    assert(!ft8_ui_adapter_waterfall_supported(&adapter) && allocations==1);
    assert(ft8_ui_adapter_render(&adapter,&frame));
    fail_alloc=false;
    assert(ft8_ui_adapter_init(&adapter,&api,FT8_PRESENTATION_ADV));
    assert(!ft8_ui_adapter_waterfall_supported(&adapter) && allocations==1);
    assert(ft8_ui_adapter_waterfall_prepare(&adapter));
    assert(ft8_ui_adapter_waterfall_supported(&adapter) && allocations==2);
    assert(ft8_ui_adapter_waterfall_prepare(&adapter) && allocations==2);
    assert(allocation_size>=4560 && allocation_size<4600);
    assert(sizeof(adapter)<128); /* History must stay off the foreground stack. */
    frame.waterfall_overlay=true;
    assert(ft8_ui_adapter_render(&adapter,&frame));
    assert(blits==19);
    for(unsigned y=0;y<19;++y) check_row(y,0);
    uint8_t row[240]; unsigned before=presents;
    for(unsigned v=1;v<=25;++v) {
        memset(row,v,240); ft8_ui_adapter_push_waterfall(&adapter,row);
    }
    assert(presents==before && blits==19*26);
    for(unsigned y=0;y<19;++y) check_row(y,y+7);
    /* present() clobbers raster; every ordinary redraw must repaint afterward. */
    frame.rows[0][0]='!';
    assert(ft8_ui_adapter_render(&adapter,&frame));
    for(unsigned y=0;y<19;++y) check_row(y,y+7);
    frame.waterfall_overlay=false; before=blits;
    assert(ft8_ui_adapter_render(&adapter,&frame) && blits==before && top[0]=='!');
    memset(row,255,240); ft8_ui_adapter_push_waterfall(&adapter,row);
    assert(blits==before);
    frame.waterfall_overlay=true;
    assert(ft8_ui_adapter_render(&adapter,&frame));
    check_row(17,25); check_row(18,255);
    assert(physical[18][0]==0xFFE0); /* Yellow, never blue. */
    ft8_ui_adapter_shutdown(&adapter); assert(frees==1);
    return 0;
}
