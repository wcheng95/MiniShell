#include "ft8_ui_adapter.h"
#include "ui_shell.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>
static char rows[UI_MAX_ROWS][UI_TEXT_CAP];
static uint32_t attrs[UI_MAX_ROWS], bar;
static unsigned plain_writes,color_writes,separators,presents;
static bool unsupported;
static mini_result_t info(mini_text_display_info_t *p){p->rows=7;p->columns=20;return MINI_OK;}
static mini_result_t clear(void){bar=0;memset(rows,0,sizeof(rows));return MINI_OK;}
static mini_result_t plain(uint32_t r,uint32_t c,const char *s,uint32_t n)
{assert(r<7 && c==0 && n==20);memcpy(rows[r],s,n);++plain_writes;return MINI_OK;}
static mini_result_t colored(uint32_t r,uint32_t c,const char *s,uint32_t n,uint32_t attr)
{++color_writes;if(unsupported)return MINI_ERR_UNSUPPORTED;attrs[r]=attr;return plain(r,c,s,n);}
static mini_result_t separator(uint32_t r,uint32_t fg)
{assert(r==0);++separators;if(unsupported)return MINI_ERR_UNSUPPORTED;bar=fg;return MINI_OK;}
static mini_result_t present(void){++presents;return MINI_OK;}
static mini_result_t input(mini_key_event_t *p,uint32_t t){(void)p;(void)t;return MINI_ERR_NOT_READY;}
int main(void)
{
    mini_text_display_api_t text={.struct_size=sizeof(text),.get_info=info,.clear=clear,.write_at=plain,
        .write_at_attr=colored,.set_row_separator=separator};
    mini_display_api_t display={.struct_size=sizeof(display),.text=&text,.present=present};
    mini_key_input_api_t keys={.struct_size=sizeof(keys),.read=input};
    mini_input_api_t in={.struct_size=sizeof(in),.capabilities=MINI_INPUT_CAP_KEY,.key=&keys};
    mini_api_t api={.api_version=MINISHELL_API_VERSION,.struct_size=sizeof(api),.display=&display,.input=&in};
    UiModel model={0};UiShell ui;UiFrame frame;ui_shell_init(&ui,FT8_PRESENTATION_ADV);
    model.rx_count=3;strcpy(model.rx_lines[0],"REPLY");strcpy(model.rx_lines[1],"CQ");strcpy(model.rx_lines[2],"OTHER");
    model.rx_kind[0]=UI_RX_TO_ME;model.rx_kind[1]=UI_RX_CQ;
    ui_shell_render(&ui,&model,&frame);
    for(unsigned variant=0;variant<6;++variant) {
        display.capabilities=MINI_DISPLAY_CAP_TEXT;
        if(variant==0 || variant==1 || variant>=4)display.capabilities|=MINI_DISPLAY_CAP_TEXT_COLOR;
        if(variant==0 || variant==2 || variant>=4)display.capabilities|=MINI_DISPLAY_CAP_ROW_SEPARATOR;
        text.struct_size=variant==5?offsetof(mini_text_display_api_t,write_at_attr):sizeof(text);
        unsupported=variant==4;plain_writes=color_writes=separators=presents=0;
        ft8_ui_adapter_t adapter;assert(ft8_ui_adapter_init(&adapter,&api,FT8_PRESENTATION_ADV));
        assert(ft8_ui_adapter_render(&adapter,&frame) && presents==1 && plain_writes==7);
        for(unsigned r=0;r<7;++r)assert(!memcmp(rows[r],frame.rows[r],20));
        assert(color_writes==((variant==0 || variant==1 || variant==4)?7u:0u));
        assert(separators==((variant==0 || variant==2 || variant==4)?1u:0u));
        if(variant==0 || variant==1)assert(attrs[0]==MINI_TEXT_ATTR_FG_WHITE && attrs[1]==MINI_TEXT_ATTR_FG_RED && attrs[2]==MINI_TEXT_ATTR_FG_GREEN && attrs[3]==MINI_TEXT_ATTR_FG_WHITE);
        if(variant==0 || variant==2) {
            assert(bar==MINI_TEXT_ATTR_FG_WHITE);frame.separator_color=UI_COLOR_RED;
            assert(ft8_ui_adapter_render(&adapter,&frame) && bar==MINI_TEXT_ATTR_FG_RED && separators==2);
            frame.separator_color=UI_COLOR_WHITE;
        }
        ft8_ui_adapter_shutdown(&adapter);assert(bar==0);
    }
    return 0;
}
