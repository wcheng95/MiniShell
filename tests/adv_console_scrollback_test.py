"""Run the ADV renderer and console line-reader logic with host hardware stubs."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
display = (root / 'platform/adv/adv_display.cpp').read_text()
display = display.replace('#include "adv_internal.h"', '#include "minishell/api.h"')
console = (root / 'platform/adv/adv_console.c').read_text()
# Compile the unchanged output routing, character editor, physical event handling,
# and complete polling loop; only ESP-IDF bring-up/diagnostic transport is stubbed.
console = console[console.index('void minishell_platform_console_write('):]
with tempfile.TemporaryDirectory() as tmp:
    d = pathlib.Path(tmp)
    (d / 'M5Unified.h').write_text('''
struct Display {
 char cells[7][20]; unsigned fg[7][20], bg[7][20], color=0, gap=0;
 int row=0, column=0;
 void begin(){} void setRotation(int){} int width(){return 240;} int height(){return 135;}
 void setTextFont(int){} void setTextSize(int){} void setTextWrap(bool){} void fillScreen(unsigned){}
 void fillRect(int x,int y,int,int,unsigned c){
  if(y==19) gap=c; else bg[y==0?0:1+(y-21)/19][x/12]=c;
 }
 void setTextColor(unsigned c){color=c;}
 void setCursor(int x,int y){column=x/12; row=y==1?0:1+(y-22)/19;}
 void write(unsigned char c){cells[row][column]=c;fg[row][column]=color;}
};
struct Board { struct Display Display; } M5;
''')
    (d / 'test.cpp').write_text(display + r'''
#include <cassert>
#include <cstdio>
#include <string>
#include <deque>
#include <algorithm>
static std::string usb_output;
static std::deque<mini_key_event_t> keys;
static std::deque<int> usb_input;
static struct { bool suspended; } s_host_console;
static unsigned polls;
static void adv_console_debug_write(const char *s) { usb_output += s; }
static mini_result_t adv_keyboard_read_event(mini_key_event_t *e) {
 if(keys.empty()) return MINI_ERR_NOT_READY;
 *e=keys.front(); keys.pop_front(); return MINI_OK;
}
static int host_getc(FILE*) {
 if(usb_input.empty()) return EOF;
 int c=usb_input.front(); usb_input.pop_front(); return c;
}
static void vTaskDelay(unsigned) { assert(++polls < 1000); }
#define pdMS_TO_TICKS(x) (x)
#define fgetc host_getc
''' + console + r'''
#undef fgetc

static std::string row(unsigned r) { return std::string(M5.Display.cells[r],20); }
static std::string padded(std::string s) { s.resize(20,' '); return s; }
static std::string numbered(unsigned n) {
 char s[20]; snprintf(s,sizeof(s),"row%02u",n); return s;
}
static void reset() {
 assert(adv_display_prepare()==0); usb_output.clear();
 keys.clear(); usb_input.clear(); polls=0;
}
static void fill(unsigned rows) {
 for(unsigned i=0;i<rows;++i) {
  std::string s=numbered(i); if(i+1<rows) s+='\n';
  minishell_platform_console_write(s.c_str());
 }
}
static mini_key_event_t special(uint32_t key, unsigned mods=0) {
 mini_key_event_t e={};e.type=MINI_KEY_EVENT_SPECIAL;e.key=key;e.modifiers=mods;return e;
}
static mini_key_event_t character(char c) {
 mini_key_event_t e={};e.type=MINI_KEY_EVENT_CHAR;e.codepoint=c;return e;
}
static void assert_view(unsigned first) {
 for(unsigned r=0;r<7;++r) assert(row(r)==padded(numbered(first+r)));
}
static void history_tests() {
 reset();
 for(unsigned i=0;i<7;++i) {
  if(i) minishell_platform_console_write("\n");
  minishell_platform_console_write(numbered(i).c_str());
  for(unsigned r=0;r<7;++r) assert(row(r)==(r<=i?padded(numbered(r)):padded("")));
 }
 reset();
 minishell_platform_console_write("ab\rc\bD\t\x01\x7f\x80");
 assert(row(0)==padded("abD"));
 minishell_platform_console_write("\n\bX");
 assert(row(0)==padded("abD") && row(1)==padded("X"));
 reset(); minishell_platform_console_write("12345678901234567890");
 assert(row(0)=="12345678901234567890" && row(1)==padded(""));
 minishell_platform_console_write("\bZ\n");
 assert(row(0)=="12345678901234567890" && row(1)==padded("Z"));
 assert(s_history_count==3); // The full-width row wraps immediately.

 reset(); fill(50);
 assert(s_history_count==50 && s_history_first==0);
 assert_view(43);
 const std::string bytes=usb_output;
 char history[50][20]; memcpy(history,s_history,sizeof(history));
 adv_display_console_scroll(5); assert_view(38);
 adv_display_console_scroll(5); assert_view(33);
 adv_display_console_scroll(-5); assert_view(38);
 for(unsigned i=0;i<20;++i) adv_display_console_scroll(5);
 assert_view(0); assert(s_console_offset==43);
 assert(!memcmp(history,s_history,sizeof(history)) && usb_output==bytes);
 for(unsigned i=0;i<20;++i) adv_display_console_scroll(-5);
 assert_view(43); assert(s_console_offset==0);
 minishell_platform_console_write("\nrow50");
 assert(s_history_count==50 && s_history_first==1);
 assert_view(44);
 adv_display_console_scroll(100); assert_view(1);
 // Partial command survives viewing and output follows the tail before editing.
 minishell_platform_console_write("\nM$> par");
 adv_display_console_scroll(5); assert(s_console_offset==5);
 minishell_platform_console_write("t");
 assert(s_console_offset==0 && row(6)==padded("M$> part"));
 adv_display_console_scroll(5);
 minishell_platform_console_write("\b \b"); assert(row(6)==padded("M$> par"));
 adv_display_console_scroll(5);
 minishell_platform_console_write("\n"); assert(row(5)==padded("M$> par") && row(6)==padded(""));

 reset();
 std::string long_output;
 for(unsigned i=0;i<65;++i) long_output+=std::string(20,'A'+i%26);
 minishell_platform_console_write(long_output.c_str());
 assert(s_history_count==50);
 adv_display_console_scroll(100);
 // 65 full-width rows plus the newest partial (empty) row: retain rows 16..65.
 for(unsigned r=0;r<7;++r) assert(row(r)==std::string(20,'A'+(16+r)%26));
 adv_display_console_scroll(-100);
 for(unsigned r=0;r<6;++r) assert(row(r)==std::string(20,'A'+(59+r)%26));
 assert(row(6)==padded(""));
}
static void handoff_tests() {
 reset();fill(18);
 adv_display_console_scroll(5);
 char history[50][20]; memcpy(history,s_history,sizeof(history));
 const std::string bytes=usb_output;
 assert(adv_display_text_clear(nullptr)==MINI_OK);
 assert(adv_display_text_write_at_attr(nullptr,0,0,"APP",3,
        MINI_TEXT_ATTR_INVERSE|MINI_TEXT_ATTR_FG_RED)==MINI_OK);
 assert(adv_display_text_write_at(nullptr,1,0,"plain",5)==MINI_OK);
 assert(adv_display_text_clear_at(nullptr,1,0,1,1)==MINI_OK);
 assert(adv_display_set_row_separator(nullptr,0,MINI_TEXT_ATTR_FG_GREEN)==MINI_OK);
 assert(adv_display_present(nullptr)==MINI_OK);
 assert(row(0)==padded("APP") && row(1)==padded(" lain"));
 assert(M5.Display.fg[0][0]==0 && M5.Display.bg[0][0]==0xff0000);
 assert(M5.Display.gap==0x00ff00);
 adv_display_console_scroll(5); // No stealing a foreground app's Display.
 assert(row(0)==padded("APP"));
 assert(!memcmp(history,s_history,sizeof(history)) && usb_output==bytes);
 minishell_platform_console_write("\nM$> ");
 assert(row(0)==padded("row12") && row(5)==padded("row17") && row(6)==padded("M$> "));
 assert(s_console_offset==0 && M5.Display.gap==0);
 for(unsigned r=0;r<7;++r) for(unsigned c=0;c<20;++c)
  assert(M5.Display.fg[r][c]==0xffffff && M5.Display.bg[r][c]==0);
 assert(usb_output==bytes+"\nM$> ");
 adv_display_console_scroll(100);assert_view(0);
}
static void input_tests() {
 reset();fill(30);
 char buffer[32]="draft";size_t length=5;
 const std::string bytes=usb_output;
 auto up=special(MINI_KEY_UP,MINI_MOD_FN),down=special(MINI_KEY_DOWN,MINI_MOD_FN);
 assert(accept_key_event(&up,buffer,sizeof(buffer),&length)==0);
 assert(s_console_offset==5);assert_view(18);
 assert(accept_key_event(&up,buffer,sizeof(buffer),&length)==0);
 assert(s_console_offset==10);assert_view(13);
 assert(accept_key_event(&down,buffer,sizeof(buffer),&length)==0);
 assert(s_console_offset==5);
 assert(!strcmp(buffer,"draft") && length==5 && usb_output==bytes);
 for(auto key : {MINI_KEY_UP,MINI_KEY_DOWN,MINI_KEY_FN}) {
  auto e=special(key);assert(accept_key_event(&e,buffer,sizeof(buffer),&length)==0);
 }
 assert(!strcmp(buffer,"draft") && length==5 && usb_output==bytes && s_console_offset==5);
 keys={character('a'),up,down,special(MINI_KEY_FN),character('b'),
       special(MINI_KEY_BACKSPACE),character('c'),special(MINI_KEY_DELETE),
       character('d'),special(MINI_KEY_ENTER)};
 assert(minishell_platform_console_read_line(buffer,sizeof(buffer))==1);
 assert(!strcmp(buffer,"ad") && usb_output==bytes+"ab\b \bc\b \bd\n");
 assert(s_console_offset==0);
 const std::string before=usb_output;
 usb_input={'u','v',0x7f,'w','\r'};
 assert(minishell_platform_console_read_line(buffer,sizeof(buffer))==1);
 assert(!strcmp(buffer,"uw") && usb_output==before+"uv\b \bw\n");
 usb_input={4};assert(minishell_platform_console_read_line(buffer,sizeof(buffer))==0);
 // Bounded edit buffer and ignored non-ASCII input retain existing semantics.
 usb_input={'a','b','c',0x80,'\n'};
 assert(minishell_platform_console_read_line(buffer,3)==1 && !strcmp(buffer,"ab"));
}
int main(){history_tests();handoff_tests();input_tests();}
''')
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Wno-missing-field-initializers',
                    '-I'+str(d), '-I'+str(root/'include'), str(d/'test.cpp'),
                    '-o', str(d/'test')], check=True)
    subprocess.run([str(d/'test')], check=True)
print('ADV console history, Display handoff, physical and USB line input: PASS')
