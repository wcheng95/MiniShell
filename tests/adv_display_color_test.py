"""Exercise the actual ADV renderer against a recording M5 display."""
import pathlib, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
source = (root/'platform/adv/adv_display.cpp').read_text()
assert all(word not in source.lower() for word in ('minicw','operator','ft8','reply'))
source = source.replace('#include "adv_internal.h"', '#include "minishell/api.h"')
with tempfile.TemporaryDirectory() as tmp:
    d = pathlib.Path(tmp)
    (d/'M5Unified.h').write_text('''#include <vector>
struct Rect { int x,y,w,h; unsigned color; };
struct Display {
 bool swap = false;
 bool getSwapBytes(){return swap;} void setSwapBytes(bool value){swap=value;}
 std::vector<unsigned> wire;
 void pushImage(int,int,unsigned w,unsigned h,const uint16_t* pixels){
  for(unsigned i=0;i<w*h;++i) {
   unsigned v=pixels[i];
   if(!swap) v=(v>>8)|((v&255)<<8);
   wire.push_back(v);
  }
 }

 std::vector<Rect> rects; std::vector<unsigned> colors;
 void begin(){} void setRotation(int){} int width(){return 240;} int height(){return 135;}
 void setTextFont(int){} void setTextSize(int){} void setTextWrap(bool){} void fillScreen(unsigned){}
 void fillRect(int x,int y,int w,int h,unsigned c){rects.push_back({x,y,w,h,c});}
 void setTextColor(unsigned c){colors.push_back(c);} void setCursor(int,int){} void write(unsigned char){}
};
struct Board { struct Display Display; } M5;
''')
    (d/'test.cpp').write_text(source+'''
#include <cassert>
#include <cstdlib>
extern "C" {
#include "ft8_ui_adapter.h"
}
static void test_overlay_rows()
{
 mini_text_display_api_t text{};text.struct_size=sizeof(text);
 text.get_info=[](mini_text_display_info_t *p){return adv_display_text_get_info(nullptr,&p->columns,&p->rows);};
 text.clear=[](){return adv_display_text_clear(nullptr);};
 text.write_at=[](uint32_t r,uint32_t c,const char *s,uint32_t n){return adv_display_text_write_at(nullptr,r,c,s,n);};
 text.write_at_attr=[](uint32_t r,uint32_t c,const char *s,uint32_t n,uint32_t a){return adv_display_text_write_at_attr(nullptr,r,c,s,n,a);};
 text.set_row_separator=[](uint32_t r,uint32_t a){return adv_display_set_row_separator(nullptr,r,a);};
 mini_raster_display_api_t raster{};raster.struct_size=sizeof(raster);
 raster.get_info=[](mini_raster_display_info_t *p){return adv_display_raster_get_info(nullptr,&p->width,&p->height);};
 raster.blit_rgb565=[](uint32_t x,uint32_t y,uint32_t w,uint32_t h,const uint16_t *p,uint32_t stride){return adv_display_blit_rgb565(nullptr,x,y,w,h,p,stride);};
 mini_display_api_t display{};display.struct_size=sizeof(display);
 display.capabilities=MINI_DISPLAY_CAP_TEXT|MINI_DISPLAY_CAP_TEXT_COLOR|MINI_DISPLAY_CAP_ROW_SEPARATOR|MINI_DISPLAY_CAP_RASTER_RGB565;
 display.text=&text;display.raster=&raster;display.present=[](){return adv_display_present(nullptr);};
 mini_memory_api_t memory{};memory.struct_size=sizeof(memory);
 memory.alloc=[](uint32_t n,void **p){*p=std::malloc(n);return *p?MINI_OK:MINI_ERR_NO_MEMORY;};
 memory.free=[](void *p){std::free(p);return MINI_OK;};
 mini_key_input_api_t key{};key.struct_size=sizeof(key);
 key.read=[](mini_key_event_t *,uint32_t){return MINI_ERR_NOT_READY;};
 mini_input_api_t input{};input.struct_size=sizeof(input);input.capabilities=MINI_INPUT_CAP_KEY;input.key=&key;
 mini_api_t api{};api.struct_size=sizeof(api);api.api_version=MINISHELL_API_VERSION;
 api.memory=&memory;api.display=&display;api.input=&input;
 ft8_ui_adapter_t adapter;assert(ft8_ui_adapter_init(&adapter,&api,FT8_PRESENTATION_ADV));
 assert(ft8_ui_adapter_waterfall_prepare(&adapter));
 UiFrame frame{};frame.column_count=20;frame.row_count=7;frame.separator_after_top=true;frame.separator_color=UI_COLOR_WHITE;
 for(unsigned r=0;r<7;++r) std::memset(frame.rows[r],' ',20);
 frame.rows[0][0]='R';assert(ft8_ui_adapter_render(&adapter,&frame));
 frame.waterfall_overlay=true;
 for(unsigned batch=0;batch<3;++batch){
  frame.rows[0][0]='N';frame.rows[1][0]='A'+batch;
  M5.Display.rects.clear();M5.Display.wire.clear();
  assert(ft8_ui_adapter_render(&adapter,&frame));
  assert(M5.Display.rects.size()==121 && M5.Display.wire.size()==240*19);
  for(auto rect:M5.Display.rects) assert(rect.y>=19);
  assert(s_cells[0][0]=='R' && s_cells[1][0]=='A'+batch);
 }
 frame.waterfall_overlay=false;M5.Display.rects.clear();M5.Display.wire.clear();
 assert(ft8_ui_adapter_render(&adapter,&frame));
 assert(M5.Display.rects.size()==141 && M5.Display.rects[1].y==0 && s_cells[0][0]=='N');
 assert(M5.Display.wire.empty());
 ft8_ui_adapter_shutdown(&adapter);
}
int main(){
 assert(adv_display_prepare()==0);
 for(unsigned row=0;row<7;++row){
  auto cell=M5.Display.rects[1+row*20];
  assert(cell.y==(row==0?0:21+int(row-1)*19) && cell.h==19 && cell.w==12);
 }
 uint32_t width=0,height=0;
 assert(adv_display_raster_get_info(nullptr,&width,&height)==MINI_OK && width==240 && height==135);
 char saved_cells[sizeof(s_cells)]; unsigned char saved_attrs[sizeof(s_attrs)];
 std::memcpy(saved_cells,s_cells,sizeof(s_cells)); std::memcpy(saved_attrs,s_attrs,sizeof(s_attrs));
 auto saved_separator=s_separator; auto saved_console=s_console_mode;
 uint16_t pixels[242];
 for(unsigned x=0;x<242;++x) pixels[x]=x%3==0?0xf800:x%3==1?0x07e0:0x001f;
 for(unsigned y=0;y<19;++y){
  M5.Display.wire.clear(); M5.Display.setSwapBytes(y%2);
  assert(adv_display_blit_rgb565(nullptr,0,y,240,1,pixels,242)==MINI_OK);
  assert(M5.Display.getSwapBytes()==bool(y%2));
  assert(M5.Display.wire.size()==240);
  for(unsigned x=0;x<240;++x) assert(M5.Display.wire[x]==pixels[x]);
 }
 assert(!std::memcmp(saved_cells,s_cells,sizeof(s_cells)));
 assert(!std::memcmp(saved_attrs,s_attrs,sizeof(s_attrs)));
 assert(s_separator==saved_separator && s_console_mode==saved_console);
 M5.Display.wire.clear();
 assert(adv_display_blit_rgb565(nullptr,0,0,0,1,nullptr,0)==MINI_OK && M5.Display.wire.empty());
 assert(adv_display_blit_rgb565(nullptr,0,0,240,1,nullptr,240)==MINI_ERR_INVALID);
 assert(adv_display_blit_rgb565(nullptr,0,0,240,1,pixels,239)==MINI_ERR_INVALID);
 assert(adv_display_blit_rgb565(nullptr,1,0,240,1,pixels,240)==MINI_ERR_INVALID);

 // Display API dirty-row behavior is independent of resident full redraws.
 M5.Display.rects.clear(); adv_display_present(nullptr); assert(M5.Display.rects.empty());
 adv_display_text_write_at(nullptr,1,0,"X",1);
 M5.Display.rects.clear(); adv_display_present(nullptr);
 assert(M5.Display.rects.size()==20);
 for(auto r:M5.Display.rects) assert(r.y==21);
 M5.Display.rects.clear(); adv_display_present(nullptr); assert(M5.Display.rects.empty());
 adv_display_text_write_at(nullptr,2,0,"X",1);
 adv_display_text_write_at_attr(nullptr,5,0,"Y",1,MINI_TEXT_ATTR_FG_GREEN);
 M5.Display.rects.clear(); adv_display_present(nullptr);
 assert(M5.Display.rects.size()==40);
 for(unsigned i=0;i<40;++i) assert(M5.Display.rects[i].y==(i<20?40:97));
 adv_display_text_clear_at(nullptr,3,2,2,4);
 M5.Display.rects.clear(); adv_display_present(nullptr);
 assert(M5.Display.rects.size()==40);
 for(unsigned i=0;i<40;++i) assert(M5.Display.rects[i].y==(i<20?59:78));
 adv_display_set_row_separator(nullptr,0,MINI_TEXT_ATTR_FG_RED);
 M5.Display.rects.clear(); adv_display_present(nullptr);
 assert(M5.Display.rects.size()==1 && M5.Display.rects[0].y==19);
 adv_display_text_clear(nullptr);
 M5.Display.rects.clear(); adv_display_present(nullptr); assert(M5.Display.rects.size()==141);
 M5.Display.rects.clear(); adv_display_present(nullptr); assert(M5.Display.rects.empty());
 // Zero-sized text edits do not dirty rows.
 adv_display_text_write_at(nullptr,0,0,nullptr,0);
 adv_display_text_write_at_attr(nullptr,0,0,nullptr,0,0);
 adv_display_text_clear_at(nullptr,0,0,1,0);
 M5.Display.rects.clear(); adv_display_present(nullptr); assert(M5.Display.rects.empty());

 const unsigned attrs[]={MINI_TEXT_ATTR_FG_WHITE,MINI_TEXT_ATTR_FG_GREEN,MINI_TEXT_ATTR_FG_CYAN,MINI_TEXT_ATTR_FG_RED};
 const unsigned rgb[]={0xffffff,0x00ff00,0x00ffff,0xff0000};
 for(unsigned i=0;i<4;++i){
  adv_display_text_write_at_attr(nullptr,0,0,"A",1,attrs[i]);
  M5.Display.rects.clear(); M5.Display.colors.clear(); adv_display_present(nullptr);
  assert(M5.Display.colors[0]==rgb[i]);
  assert(adv_display_set_row_separator(nullptr,0,attrs[i])==MINI_OK);
  M5.Display.rects.clear(); adv_display_present(nullptr);
  auto gap=M5.Display.rects[0]; assert(gap.y==19 && gap.h==2 && gap.color==rgb[i]);
  adv_display_text_write_at_attr(nullptr,0,0,"A",1,attrs[i]|MINI_TEXT_ATTR_INVERSE);
  M5.Display.rects.clear(); M5.Display.colors.clear(); adv_display_present(nullptr);
  assert(M5.Display.colors[0]==0 && M5.Display.rects[0].color==rgb[i]);
 }
 assert(adv_display_set_row_separator(nullptr,0,MINI_TEXT_ATTR_FG_GREEN)==MINI_OK);
 M5.Display.rects.clear(); adv_display_present(nullptr);
 auto gap=M5.Display.rects[0]; assert(gap.x==0 && gap.y==19 && gap.w==240 && gap.h==2 && gap.color==0x00ff00);
 assert(adv_display_set_row_separator(nullptr,1,MINI_TEXT_ATTR_FG_GREEN)==MINI_ERR_UNSUPPORTED);
 adv_display_text_write_at_attr(nullptr,0,0,"A",1,MINI_TEXT_ATTR_FG_CYAN|MINI_TEXT_ATTR_INVERSE);
 M5.Display.rects.clear(); M5.Display.colors.clear(); adv_display_present(nullptr);
 assert(M5.Display.colors[0]==0 && M5.Display.rects[0].color==0x00ffff);
 adv_display_text_clear(nullptr); M5.Display.rects.clear(); adv_display_present(nullptr);
 assert(M5.Display.rects[0].color==0);
 adv_display_set_row_separator(nullptr,0,MINI_TEXT_ATTR_FG_GREEN);
 M5.Display.rects.clear(); adv_display_console_write("shell\\n"); assert(M5.Display.rects[0].color==0 && M5.Display.rects.size()==141);
 M5.Display.rects.clear(); adv_display_console_edit_begin(); assert(M5.Display.rects.size()==141);
 M5.Display.rects.clear(); adv_display_console_edit_line("edit",2); assert(M5.Display.rects.size()==141);
 M5.Display.rects.clear(); adv_display_console_scroll(0); assert(M5.Display.rects.size()==141);
 M5.Display.rects.clear(); adv_display_console_edit_discard(); assert(M5.Display.rects.size()==141);
 M5.Display.rects.clear(); adv_display_console_clear(); assert(M5.Display.rects.size()==141);
 M5.Display.rects.clear(); adv_display_present(nullptr); assert(M5.Display.rects.empty());
 test_overlay_rows();
}
''')
    includes = ['-I'+str(root/p) for p in ('include','apps/ft8/include','apps/ft8/main','apps/ft8/src/presentation_profile')]
    objects = []
    for name in ('apps/ft8/main/ft8_ui_adapter.c','apps/ft8/src/presentation_profile/presentation_profile.c'):
        obj = d/(pathlib.Path(name).stem+'.o')
        subprocess.run(['cc','-std=c11',*includes,'-c',str(root/name),'-o',str(obj)],check=True)
        objects.append(str(obj))
    subprocess.run(['c++','-std=c++17','-I'+str(d),*includes,str(d/'test.cpp'),*objects,'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
print('ADV Display colors, inverse, separator geometry and console reset: PASS')
