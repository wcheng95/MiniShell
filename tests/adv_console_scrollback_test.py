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
console = console[console.index('static bool s_line_start'):]
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
#include "shell_editor.h"
#include "shell_completion.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <deque>
#include <algorithm>
static bool completion_enabled;
static unsigned completion_reads, completion_opens, completion_closes;
static const char *const root_names[]={"flash","sd",nullptr};
static const char *const flash_names[]={"ft8",nullptr};
static const char *const cwd_names[]={"setting.txt","RT260925.txt","RT260926.txt","RxTxLog.txt",nullptr};
static const char *const *completion_names;
static mini_result_t completion_open(const char *path, mini_dir_t *out) {
 if(!completion_enabled) return MINI_ERR_NOT_FOUND;
 if(!strcmp(path,"/")) completion_names=root_names;
 else if(!strcmp(path,"/flash")) completion_names=flash_names;
 else { assert(!strcmp(path,"/flash/ft8") || !strcmp(path,"."));completion_names=cwd_names; }
 ++completion_opens;completion_reads=0;*out=1;return MINI_OK;
}
static mini_result_t completion_read(mini_dir_t, mini_fs_dir_entry_t *entry, uint32_t *has) {
 const char *name=completion_names[completion_reads++];
 *has=name!=nullptr;
 if(*has) strcpy(entry->name,name);
 return MINI_OK;
}
static mini_result_t completion_close(mini_dir_t) { ++completion_closes;return MINI_OK; }
const mini_api_t *mini_api_get(void) {
 static mini_fs_api_t fs={};static mini_api_t api={};
 fs.dir_open=completion_open;fs.dir_read=completion_read;fs.dir_close=completion_close;
 api.fs=&fs;return &api;
}
static std::string usb_output;
static std::deque<mini_key_event_t> keys;
static std::deque<int> usb_input;
static struct { bool suspended; } s_host_console;
static unsigned polls;
static uint64_t fake_time;
static void (*delay_hook)(void);
static uint64_t adv_monotonic_us(void*) { return fake_time; }
static void adv_console_debug_write(const char *s) { usb_output += s; }
static mini_result_t adv_keyboard_read_event(mini_key_event_t *e) {
 if(keys.empty()) return MINI_ERR_NOT_READY;
 *e=keys.front(); keys.pop_front(); return MINI_OK;
}
static int host_getc(FILE*) {
 if(usb_input.empty()) return EOF;
 int c=usb_input.front(); usb_input.pop_front(); return c;
}
static void vTaskDelay(unsigned ms) { assert(++polls < 1000); fake_time+=ms*1000u; if(delay_hook) delay_hook(); }
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
 assert(adv_display_prepare()==0); usb_output.clear(); s_line_start=true;
 keys.clear(); usb_input.clear(); polls=0; fake_time=0; delay_hook=nullptr; s_cursor_editing=false;
 completion_opens=completion_closes=0;
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
static mini_key_event_t character(char c, unsigned mods=0) {
 mini_key_event_t e={};e.type=MINI_KEY_EVENT_CHAR;e.codepoint=c;e.modifiers=mods;return e;
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
 reset();
 minishell_platform_console_write("unterminated");minishell_platform_console_prompt();
 assert(row(0)==padded("unterminated") && row(1)==padded("M$> "));
 reset();fill(30);
 shell_editor_t editor; shell_editor_init(&editor);
 strcpy(editor.line,"draft");editor.length=editor.cursor=5;
 const shell_editor_t before_scroll=editor;
 const std::string bytes=usb_output;
 auto up=character(';',MINI_MOD_CTRL),down=character('.',MINI_MOD_CTRL);
 assert(accept_key_event(&up,&editor)==0);
 assert(s_console_offset==5);assert_view(18);
 assert(accept_key_event(&up,&editor)==0);
 assert(s_console_offset==10);assert_view(13);
 assert(accept_key_event(&down,&editor)==0);
 assert(s_console_offset==5);
 for(auto key : {MINI_KEY_UP,MINI_KEY_DOWN,MINI_KEY_LEFT,MINI_KEY_RIGHT,MINI_KEY_FN}) {
  auto e=special(key);assert(accept_key_event(&e,&editor)==0);
 }
 assert(!memcmp(&editor,&before_scroll,sizeof(editor)) && usb_output==bytes && s_console_offset==5);
 shell_editor_begin(&editor);
 minishell_platform_console_write("\nM$> ");
 keys={character('a'),up,down,special(MINI_KEY_FN),character('b'),
       special(MINI_KEY_BACKSPACE),character('c'),special(MINI_KEY_DELETE),
       character('d'),special(MINI_KEY_ENTER)};
 assert(minishell_platform_console_read_line(&editor)==2);
 assert(!strcmp(editor.line,"acd") && s_console_offset==0);
 assert(usb_output.find("acd\n")!=std::string::npos);
 shell_editor_begin(&editor);minishell_platform_console_write("M$> ");
 usb_input={'u','v',0x7f,'w','\r'};
 assert(minishell_platform_console_read_line(&editor)==2);
 assert(!strcmp(editor.line,"uw"));
 shell_editor_begin(&editor);minishell_platform_console_write("M$> ");
 usb_input={4};assert(minishell_platform_console_read_line(&editor)==0);
 shell_editor_begin(&editor);
 usb_input={',','/',';','.',0x80,'\n'};
 assert(minishell_platform_console_read_line(&editor)==2 && !strcmp(editor.line,",/;."));

 shell_editor_begin(&editor);minishell_platform_console_prompt();adv_display_console_edit_begin();
 for(char c : std::string(",/;.")) { auto e=character(c);accept_key_event(&e,&editor); }
 assert(!strcmp(editor.line,",/;."));

 // Recall, move into the filename, edit, move right and submit through the
 // physical event path. Cursor keys must preserve navigation and stored text.
 reset();minishell_platform_console_prompt();
 shell_editor_init(&editor);
 strcpy(editor.line,"cp foo.txt /sd");shell_editor_remember(&editor);
 shell_editor_begin(&editor);adv_display_console_edit_begin();
 auto prev=special(MINI_KEY_UP,MINI_MOD_FN),nxt=special(MINI_KEY_DOWN,MINI_MOD_FN);
 auto left=special(MINI_KEY_LEFT,MINI_MOD_FN),right=special(MINI_KEY_RIGHT,MINI_MOD_FN);
 accept_key_event(&prev,&editor);
 assert(!strcmp(editor.line,"cp foo.txt /sd") && editor.navigation==0);
 char recalled[50][20];memcpy(recalled,s_history,sizeof(recalled));
 for(unsigned i=0;i<8;++i) accept_key_event(&left,&editor);
 assert(editor.cursor==6 && editor.navigation==0);
 assert(!memcmp(recalled,s_history,sizeof(recalled)));
 auto back=special(MINI_KEY_BACKSPACE),del=special(MINI_KEY_DELETE);
 accept_key_event(&back,&editor);
 auto insert=character('r');accept_key_event(&insert,&editor);
 accept_key_event(&right,&editor);accept_key_event(&del,&editor);
 insert=character('T');accept_key_event(&insert,&editor);
 assert(!strcmp(editor.line,"cp for.Txt /sd") && editor.cursor==8);
 assert(row(0)==padded("M$> cp for.Txt /sd"));
 assert(!strcmp(editor.history[0],"cp foo.txt /sd"));
 keys={special(MINI_KEY_ENTER)};
 assert(minishell_platform_console_read_line(&editor)==2);
 assert(!strcmp(editor.line,"cp for.Txt /sd"));
 shell_editor_remember(&editor);assert(editor.count==2);
 shell_editor_begin(&editor);minishell_platform_console_prompt();adv_display_console_edit_begin();
 for(char c : std::string("draft")) { auto e=character(c);accept_key_event(&e,&editor); }
 accept_key_event(&left,&editor);
 accept_key_event(&prev,&editor);accept_key_event(&nxt,&editor);
 assert(!strcmp(editor.line,"draft") && editor.cursor==4 && editor.navigation==2);
 // Other Ctrl characters retain the previous behavior, including comma/slash.
 for(char c : std::string(",/x")) { auto e=character(c,MINI_MOD_CTRL);accept_key_event(&e,&editor); }
 assert(!strcmp(editor.line,"draf,/xt"));
 // Cursor boundaries are no-ops, with no redraw/USB output or history lookup.
 for(unsigned i=0;i<20;++i) accept_key_event(&left,&editor);
 auto unchanged=usb_output;accept_key_event(&left,&editor);
 assert(editor.cursor==0 && editor.navigation==2 && usb_output==unchanged);
 for(unsigned i=0;i<20;++i) accept_key_event(&right,&editor);
 unchanged=usb_output;accept_key_event(&right,&editor);
 assert(editor.cursor==editor.length && editor.navigation==2 && usb_output==unchanged);

 // Full wrapped recall at ring capacity, with no new prompt/newline or lost
 // output rows on repeated grow/shrink. Navigation uses the existing Fn events.
 reset();fill(50);minishell_platform_console_write("\nM$> ");
 shell_editor_init(&editor);
 memset(editor.line,'x',255);editor.line[255]=0;shell_editor_remember(&editor);
 shell_editor_begin(&editor);adv_display_console_edit_begin();
 char baseline[50][20];memcpy(baseline,s_history,sizeof(baseline));
 unsigned first=s_history_first,count=s_history_count;
 auto previous=special(MINI_KEY_UP,MINI_MOD_FN),next=special(MINI_KEY_DOWN,MINI_MOD_FN);
 auto newlines=std::count(usb_output.begin(),usb_output.end(),'\n');
 for(unsigned i=0;i<20;++i) {
  accept_key_event(&previous,&editor);
  assert(editor.length==255 && s_history_count==50 && s_console_column==19);
  for(unsigned r=0;r<6;++r) assert(row(r)==std::string(20,'x'));
  assert(row(6)==padded(std::string(19,'x')));
  char before_cursor[50][20];memcpy(before_cursor,s_history,sizeof(before_cursor));
  for(unsigned step=0;step<30;++step) accept_key_event(&left,&editor);
  assert(editor.cursor==225 && editor.navigation==0);
  assert(!memcmp(before_cursor,s_history,sizeof(before_cursor)));
  for(unsigned step=0;step<30;++step) accept_key_event(&right,&editor);
  assert(editor.cursor==255 && editor.navigation==0);
  accept_key_event(&up,&editor);assert(s_console_offset==5);
  assert(editor.length==255);
  accept_key_event(&next,&editor);
  assert(editor.length==0 && s_console_offset==0);
  assert(s_history_first==first && s_history_count==count);
  assert(!memcmp(baseline,s_history,sizeof(baseline)));
 }
 assert(std::count(usb_output.begin(),usb_output.end(),'\n')==newlines);
 for(char c : std::string("draft")) { auto e=character(c);accept_key_event(&e,&editor); }
 accept_key_event(&previous,&editor);accept_key_event(&next,&editor);
 assert(!strcmp(editor.line,"draft") && row(6)==padded("M$> draft"));
 // Backspace can shrink across physical row boundaries.
 accept_key_event(&previous,&editor);
 auto backspace=special(MINI_KEY_BACKSPACE);
 for(unsigned i=0;i<240;++i) accept_key_event(&backspace,&editor);
 assert(editor.length==15 && row(6)==padded("M$> "+std::string(15,'x')));
}

static void cursor_at(unsigned r, unsigned c, char text) {
 unsigned inverse_cells=0;
 for(unsigned y=0;y<7;++y) for(unsigned x=0;x<20;++x) {
  if(M5.Display.bg[y][x]==0xffffff && M5.Display.fg[y][x]==0) {
   ++inverse_cells;assert(y==r && x==c);
  }
 }
 assert(inverse_cells==1 && M5.Display.cells[r][c]==text);
}
static void no_cursor() {
 for(unsigned y=0;y<7;++y) for(unsigned x=0;x<20;++x)
  assert(M5.Display.bg[y][x]==0);
}
static void start_edit(shell_editor_t *e) {
 shell_editor_init(e);minishell_platform_console_prompt();
 adv_display_console_edit_begin();s_cursor_editing=true;cursor_restart();
}
static void cursor_tests() {
 reset();shell_editor_t e;start_edit(&e);
 cursor_at(0,4,' ');
 auto left=special(MINI_KEY_LEFT,MINI_MOD_FN),right=special(MINI_KEY_RIGHT,MINI_MOD_FN);
 auto previous=special(MINI_KEY_UP,MINI_MOD_FN),next=special(MINI_KEY_DOWN,MINI_MOD_FN);
 auto up=character(';',MINI_MOD_CTRL),down=character('.',MINI_MOD_CTRL);
 const std::string initial_usb=usb_output;
 char history[50][20];memcpy(history,s_history,sizeof(history));
 char cells[7][20];memcpy(cells,s_cells,sizeof(cells));
 uint8_t attrs[7][20];memcpy(attrs,s_attrs,sizeof(attrs));
 fake_time=499999;cursor_poll();cursor_at(0,4,' ');
 fake_time=500000;cursor_poll();no_cursor();
 fake_time=1000000;cursor_poll();cursor_at(0,4,' ');
 assert(!memcmp(history,s_history,sizeof(history)) && usb_output==initial_usb);
 assert(!memcmp(cells,s_cells,sizeof(cells)) && !memcmp(attrs,s_attrs,sizeof(attrs)));
 fake_time=1500000;cursor_poll();no_cursor();
 accept_key_event(&left,&e);assert(s_cursor_deadline==2000000);no_cursor(); // Boundary doesn't restart.
 auto ch=character('a');accept_key_event(&ch,&e);cursor_at(0,5,' ');
 assert(s_cursor_deadline==2000000);
 ch=character('b');accept_key_event(&ch,&e);
 ch=character('c');accept_key_event(&ch,&e);cursor_at(0,7,' ');
 accept_key_event(&left,&e);cursor_at(0,6,'c');
 fake_time=2000000;cursor_poll();no_cursor();
 fake_time=2100000;accept_key_event(&left,&e);cursor_at(0,5,'b');
 assert(s_cursor_deadline==2600000);
 accept_key_event(&left,&e);cursor_at(0,4,'a');
 accept_key_event(&right,&e);cursor_at(0,5,'b');
 // Overlay inverts using the underlying attribute without modifying it.
 s_attrs[0][5]=MINI_TEXT_ATTR_FG_GREEN;
 adv_display_console_edit_cursor(false);adv_display_console_edit_cursor(true);
 assert(M5.Display.fg[0][5]==0 && M5.Display.bg[0][5]==0x00ff00);
 assert(s_attrs[0][5]==MINI_TEXT_ATTR_FG_GREEN);
 s_attrs[0][5]=MINI_TEXT_ATTR_FG_GREEN|MINI_TEXT_ATTR_INVERSE;
 adv_display_console_edit_cursor(false);adv_display_console_edit_cursor(true);
 assert(M5.Display.fg[0][5]==0x00ff00 && M5.Display.bg[0][5]==0);
 assert(s_attrs[0][5]==(MINI_TEXT_ATTR_FG_GREEN|MINI_TEXT_ATTR_INVERSE));
 s_attrs[0][5]=0;render_all();
 shell_editor_remember(&e);
 shell_editor_begin(&e);redraw_line(&e);
 ch=character('d');accept_key_event(&ch,&e);accept_key_event(&left,&e);
 accept_key_event(&previous,&e);cursor_at(0,7,' ');
 accept_key_event(&next,&e);assert(e.cursor==0 && !strcmp(e.line,"d"));cursor_at(0,4,'d');
 // Each edit class restarts a hidden phase and shows its new insertion point.
 auto back=special(MINI_KEY_BACKSPACE),del=special(MINI_KEY_DELETE);
 fake_time=s_cursor_deadline;cursor_poll();no_cursor();
 accept_key_event(&del,&e);cursor_at(0,4,' ');
 accept_character('x',&e);
 fake_time=s_cursor_deadline;cursor_poll();no_cursor();
 accept_key_event(&back,&e);cursor_at(0,4,' ');
 fake_time=s_cursor_deadline;cursor_poll();no_cursor();
 accept_key_event(&previous,&e);cursor_at(0,7,' ');
 fake_time=s_cursor_deadline;cursor_poll();no_cursor();
 accept_key_event(&next,&e);cursor_at(0,4,' ');
 cursor_end();no_cursor();assert(!s_edit_active && !s_cursor_editing);

 // Geometry includes the actual prompt tail, not a baked-in four cells.
 reset();minishell_platform_console_write("prefix M$> ");adv_display_console_edit_begin();
 cursor_at(0,11,' ');adv_display_console_edit_line("123456789",9);cursor_at(1,0,' ');
 adv_display_console_edit_line("123456789",8);cursor_at(0,19,'9');
 adv_display_console_edit_end();no_cursor();
 reset();start_edit(&e);
 for(unsigned i=0;i<16;++i) accept_character('x',&e);
 cursor_at(1,0,' ');accept_key_event(&left,&e);cursor_at(0,19,'x');
 accept_key_event(&right,&e);cursor_at(1,0,' ');

 // At capacity, all command positions survive ring wrapping and remain visible.
 reset();fill(50);minishell_platform_console_write("\n");start_edit(&e);
 for(unsigned i=0;i<255;++i) accept_character('a'+i%26,&e);
 assert(s_history_count==50 && s_console_offset==0);cursor_at(6,19,' ');
 memcpy(history,s_history,sizeof(history));
 unsigned first=s_history_first,count=s_history_count;
 for(unsigned i=255;i>0;--i) {
  accept_key_event(&left,&e);
  unsigned row;assert(cursor_cell(&row));
  cursor_at(row,(4+e.cursor)%20,e.line[e.cursor]);
  assert(!memcmp(history,s_history,sizeof(history)) && s_history_first==first && s_history_count==count);
 }
 assert(s_console_offset>0 && e.cursor==0);
 const auto before_scroll=e;
 const std::string before_scroll_usb=usb_output;
 accept_key_event(&up,&e);no_cursor();
 assert(!memcmp(&before_scroll,&e,sizeof(e)) && usb_output==before_scroll_usb);
 fake_time=s_cursor_deadline;cursor_poll();no_cursor();
 fake_time=s_cursor_deadline;cursor_poll();no_cursor();
 // Return to editing from historical output, without jumping the cursor to EOF.
 accept_key_event(&right,&e);unsigned visible_row;assert(cursor_cell(&visible_row));
 cursor_at(visible_row,5,'b');
 for(unsigned i=1;i<255;++i) {
  accept_key_event(&right,&e);unsigned row;assert(cursor_cell(&row));
  cursor_at(row,(4+e.cursor)%20,e.cursor==e.length?' ':e.line[e.cursor]);
 }
 assert(s_console_offset==0 && !memcmp(history,s_history,sizeof(history)));
 accept_key_event(&up,&e);no_cursor();accept_key_event(&down,&e);cursor_at(6,19,' ');
 // No transient inverse bits or glyphs persist, and app Display never gains the overlay.
 for(auto &r:s_attrs) for(auto attr:r) assert(attr==0);
 cursor_end();no_cursor();
 adv_display_text_clear(nullptr);adv_display_text_write_at(nullptr,0,0,"APP",3);
 adv_display_present(nullptr);adv_display_console_edit_cursor(true);cursor_poll();
 assert(row(0)==padded("APP"));no_cursor();
 minishell_platform_console_write("\n");start_edit(&e);cursor_at(6,4,' ');
}
static void idle_blink_hook() {
 if(fake_time==505000) no_cursor();
 if(fake_time==1005000) cursor_at(0,4,' ');
 if(fake_time==1505000) { no_cursor();keys.push_back(special(MINI_KEY_ENTER)); }
}
static void cursor_loop_tests() {
 reset();shell_editor_t e;shell_editor_init(&e);minishell_platform_console_prompt();
 const std::string before=usb_output;
 delay_hook=idle_blink_hook;
 assert(minishell_platform_console_read_line(&e)==2);
 assert(fake_time==1505000 && !s_edit_active && !s_cursor_editing);no_cursor();
 assert(usb_output==before+"\r\033[4C\033[K\n"); // Only submission, never periodic USB output.
 delay_hook=nullptr;minishell_platform_console_prompt();shell_editor_begin(&e);
 usb_input={4};assert(minishell_platform_console_read_line(&e)==0);
 no_cursor();assert(!s_edit_active && !s_cursor_editing);
}
static void physical_completion_case(const std::string &typed, const std::string &expected) {
 reset();shell_editor_t e;start_edit(&e);
 for(char ch:typed) { auto key=character(ch);accept_key_event(&key,&e); }
 assert(e.line==typed && completion_opens==0);
 fake_time=2000000;cursor_poll(); // Idle never triggers pathname lookup.
 assert(e.line==typed && completion_opens==0);
 const auto before=usb_output;
 const auto deadline=s_cursor_deadline;
 auto tab=special(MINI_KEY_TAB);accept_key_event(&tab,&e);
 assert(e.line==expected && e.cursor==expected.size());
 if(typed==expected) assert(usb_output==before && s_cursor_deadline==deadline);
 else {
  unsigned r;assert(cursor_cell(&r));cursor_at(r,(4+e.cursor)%20,' ');
  assert(s_cursor_deadline==fake_time+500000);
 }
 assert(completion_opens==completion_closes);
}
static void usb_completion_case(const std::string &typed, const std::string &expected, bool tab) {
 reset();shell_editor_t e;shell_editor_init(&e);minishell_platform_console_prompt();
 for(char ch:typed) usb_input.push_back(ch);
 if(tab) usb_input.push_back(9);
 usb_input.push_back(10);
 assert(minishell_platform_console_read_line(&e)==2 && e.line==expected);
 assert(e.cursor==expected.size() && completion_opens==completion_closes);
 if(!tab) assert(completion_opens==0);
 no_cursor();
}
static void completion_tests() {
 completion_enabled=true;
 physical_completion_case("cd /f","cd /flash");
 physical_completion_case("cat RT","cat RT26092");
 physical_completion_case("cat RT26092","cat RT26092"); // No longer common prefix.
 physical_completion_case("cat absent","cat absent");
 physical_completion_case("/f","/f"); // Command token never completes.
 physical_completion_case("unknown se","unknown se");
 physical_completion_case("unknown ./s","unknown ./setting.txt");
 usb_completion_case("cd /f","cd /f",false);
 usb_completion_case("cd /f","cd /flash",true);
 usb_completion_case("cd /flash","cd /flash",false);
 usb_completion_case("cat /flash/ft8/setting.txt","cat /flash/ft8/setting.txt",false);
 usb_completion_case("cat setting.txt","cat setting.txt",false);
 usb_completion_case("cat RT","cat RT",false);
 usb_completion_case("cat RT","cat RT26092",true);
 usb_completion_case("unknown se","unknown se",true);
 usb_completion_case("unknown ./s","unknown ./setting.txt",true);
 reset();shell_editor_t e;start_edit(&e);
 for(char ch:std::string("cd /f")) accept_character(ch,&e);
 auto left=special(MINI_KEY_LEFT,MINI_MOD_FN),right=special(MINI_KEY_RIGHT,MINI_MOD_FN);
 auto tab=special(MINI_KEY_TAB),previous=special(MINI_KEY_UP,MINI_MOD_FN);
 accept_key_event(&left,&e);
 const auto before=usb_output;
 accept_key_event(&tab,&e);assert(e.cursor==4 && !strcmp(e.line,"cd /f"));
 assert(completion_opens==0 && usb_output==before);
 accept_key_event(&right,&e);assert(completion_opens==0);
 accept_key_event(&tab,&e);assert(!strcmp(e.line,"cd /flash"));
 shell_editor_remember(&e);shell_editor_begin(&e);redraw_line(&e);
 accept_key_event(&previous,&e);assert(!strcmp(e.line,"cd /flash"));
 auto back=special(MINI_KEY_BACKSPACE),del=special(MINI_KEY_DELETE);
 accept_key_event(&back,&e);accept_key_event(&del,&e);
 assert(completion_opens==1 && !strcmp(e.history[0],"cd /flash"));
 cursor_end();completion_enabled=false;
}
int main(){history_tests();handoff_tests();input_tests();cursor_tests();cursor_loop_tests();completion_tests();}

''')
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Wno-missing-field-initializers',
                    '-I'+str(d), '-I'+str(root/'include'), '-I'+str(root/'core'),
                    str(d/'test.cpp'), str(root/'core/shell_editor.c'), str(root/'core/shell_completion.c'),
                    '-o', str(d/'test')], check=True)
    subprocess.run([str(d/'test')], check=True)
print('ADV console history, Display handoff, physical and USB line input: PASS')
