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
  assert(M5.Display.colors[0]==0 && M5.Display.rects[1].color==rgb[i]);
 }
 assert(adv_display_set_row_separator(nullptr,0,MINI_TEXT_ATTR_FG_GREEN)==MINI_OK);
 M5.Display.rects.clear(); adv_display_present(nullptr);
 auto gap=M5.Display.rects[0]; assert(gap.x==0 && gap.y==19 && gap.w==240 && gap.h==2 && gap.color==0x00ff00);
 assert(adv_display_set_row_separator(nullptr,1,MINI_TEXT_ATTR_FG_GREEN)==MINI_ERR_UNSUPPORTED);
 adv_display_text_write_at_attr(nullptr,0,0,"A",1,MINI_TEXT_ATTR_FG_CYAN|MINI_TEXT_ATTR_INVERSE);
 M5.Display.rects.clear(); M5.Display.colors.clear(); adv_display_present(nullptr);
 assert(M5.Display.colors[0]==0 && M5.Display.rects[1].color==0x00ffff);
 adv_display_text_clear(nullptr); M5.Display.rects.clear(); adv_display_present(nullptr);
 assert(M5.Display.rects[0].color==0);
 adv_display_set_row_separator(nullptr,0,MINI_TEXT_ATTR_FG_GREEN);
 M5.Display.rects.clear(); adv_display_console_write("shell\\n"); assert(M5.Display.rects[0].color==0);
}
''')
    subprocess.run(['c++','-std=c++17','-I'+str(d),'-I'+str(root/'include'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
print('ADV Display colors, inverse, separator geometry and console reset: PASS')
