#!/usr/bin/env python3
"""Compile actual ADV display/input implementations against host hardware stubs."""
from pathlib import Path
import subprocess
import tempfile
import sys
root=Path(sys.argv[1]).resolve()
def source(name):
    return '\n'.join(line for line in (root/'platform/adv'/name).read_text().splitlines() if not line.startswith('#include'))
display=r'''
#include "adv_ft8_web_io.h"
#include <cstdint>
#include <cstring>
#include <cassert>
#include <atomic>
#include <mutex>
#include <thread>
using portMUX_TYPE=std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(x) (x)->lock()
#define portEXIT_CRITICAL(x) (x)->unlock()
struct LCD {
 void begin(){} void setRotation(int){} int width(){return 240;} int height(){return 135;}
 void setTextFont(int){} void setTextSize(int){} void setTextWrap(bool){} void fillScreen(int){}
 void fillRect(int,int,int,int,int){} void setTextColor(int){} void setCursor(int,int){} void write(uint8_t){}
};
struct Board { LCD Display; } M5;
'''+source('adv_display.cpp')+r'''
int main(){
 assert(adv_display_prepare()==0);adv_display_snapshot_t before,after;
 adv_display_snapshot(&before);assert(before.generation==1);
 for(unsigned i=0;i<140;++i)assert(before.cells[i]==' ' && before.attrs[i]==0);
 adv_display_text_write_at_attr(nullptr,0,0,"abc",3,MINI_TEXT_ATTR_INVERSE);
 adv_display_snapshot(&after);assert(!std::memcmp(&before,&after,sizeof(before)));
 adv_display_present(nullptr);adv_display_snapshot(&after);assert(after.generation==2);
 assert(!std::memcmp(after.cells,"abc",3));for(unsigned i=0;i<3;++i)assert(after.attrs[i]==MINI_TEXT_ATTR_INVERSE);
 adv_display_text_clear(nullptr);adv_display_snapshot(&before);assert(before.generation==2 && before.cells[0]=='a');
 adv_display_console_write("console");adv_display_snapshot(&after);assert(after.generation==3);
 assert(!std::memcmp(after.cells,"console",7));for(unsigned i=0;i<140;++i)assert(!after.attrs[i]);
 std::atomic<bool> done=false;
 std::thread writer([&]{for(unsigned frame=0;frame<10000;++frame){
   char line[20];std::memset(line,'A'+frame%26,20);
   for(unsigned row=0;row<7;++row)adv_display_text_write_at_attr(nullptr,row,0,line,20,frame%2);
   adv_display_present(nullptr);
 }done=true;});
 do {adv_display_snapshot(&after);if(after.generation>=4){unsigned frame=after.generation-4;
 for(unsigned i=0;i<140;++i){assert(after.cells[i]=='A'+frame%26);assert(after.attrs[i]==frame%2);}}}while(!done);
 writer.join();adv_display_snapshot(&after);assert(after.generation==10003);
}
'''
input_source=r'''
#include "adv_ft8_web_io.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(x) pthread_mutex_lock(x)
#define portEXIT_CRITICAL(x) pthread_mutex_unlock(x)
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
static unsigned allocations;static bool fail_alloc;
static void *heap_caps_calloc(size_t n,size_t size,unsigned caps){assert(caps==3);if(fail_alloc)return NULL;void *p=calloc(n,size);if(p)++allocations;return p;}
static void release(void *p){if(p)--allocations;free(p);}
#define free release
typedef unsigned TickType_t;
#define pdMS_TO_TICKS(x) (x)
#define taskYIELD() ((void)0)
static uint64_t clock_us;
static void vTaskDelay(unsigned ticks){clock_us+=ticks*1000;}
static int64_t esp_timer_get_time(void){return (int64_t)clock_us;}
static bool physical;static mini_key_event_t submitted;
static mini_result_t adv_keyboard_read_event(mini_key_event_t *event){if(!physical)return MINI_ERR_NOT_READY;physical=false;event->type=MINI_KEY_EVENT_CHAR;event->codepoint='P';return MINI_OK;}
static void adv_keyboard_flush(void){physical=false;}
static mini_result_t minishell_services_input_submit(const mini_key_event_t *event){submitted=*event;return MINI_OK;}
'''+source('adv_input.c')+r'''
static atomic_bool finished;
static void *producer(void *unused){(void)unused;mini_key_event_t event={.struct_size=sizeof(event),.type=MINI_KEY_EVENT_CHAR,.codepoint='R'};
 while(!atomic_load(&finished)){mini_result_t rc=adv_remote_input_push(&event);assert(rc==MINI_OK||rc==MINI_ERR_NO_SPACE||rc==MINI_ERR_NOT_READY);}return NULL;}
int main(void){
 mini_key_event_t event={.struct_size=sizeof(event),.type=MINI_KEY_EVENT_CHAR,.codepoint='R'};
 assert(adv_remote_input_push(&event)==MINI_ERR_NOT_READY);fail_alloc=true;assert(!adv_remote_input_start());fail_alloc=false;
 assert(adv_remote_input_start());assert(!adv_remote_input_start() && allocations==1);
 for(unsigned i=0;i<ADV_REMOTE_KEYS;++i){event.codepoint='a'+i;assert(adv_remote_input_push(&event)==MINI_OK);}
 assert(adv_remote_input_push(&event)==MINI_ERR_NO_SPACE);
 physical=true;assert(adv_input_wait(NULL,0)==MINI_OK && submitted.codepoint=='P');
 for(unsigned i=0;i<ADV_REMOTE_KEYS;++i){assert(adv_input_wait(NULL,0)==MINI_OK && submitted.codepoint=='a'+i);}
 assert(adv_input_wait(NULL,0)==MINI_ERR_NOT_READY);
 event.codepoint='R';adv_remote_input_push(&event);physical=true;
 assert(adv_input_wait(NULL,0)==MINI_OK && submitted.codepoint=='P');physical=true;
 assert(adv_input_wait(NULL,0)==MINI_OK && submitted.codepoint=='R');
 assert(adv_input_wait(NULL,0)==MINI_OK && submitted.codepoint=='P');
 adv_remote_input_push(&event);physical=true;adv_input_flush(NULL);assert(adv_input_wait(NULL,0)==MINI_ERR_NOT_READY);
 adv_remote_input_push(&event);adv_remote_input_stop();assert(!allocations);assert(adv_remote_input_push(&event)==MINI_ERR_NOT_READY);
 assert(adv_remote_input_start());assert(adv_input_wait(NULL,2)==MINI_ERR_TIMEOUT);
 pthread_t thread;assert(!pthread_create(&thread,NULL,producer,NULL));
 for(unsigned i=0;i<1000;++i){adv_remote_input_stop();assert(adv_remote_input_start());adv_input_wait(NULL,0);adv_input_flush(NULL);}
 atomic_store(&finished,true);pthread_join(thread,NULL);adv_remote_input_stop();assert(!allocations);
 physical=true;assert(adv_input_wait(NULL,0)==MINI_OK && submitted.codepoint=='P');
}
'''
with tempfile.TemporaryDirectory(prefix='ft8-web-io-') as tmp:
    for name,code,compiler,standard in [('display',display,'c++','c++17'),('input',input_source,'cc','c11')]:
        p=Path(tmp)/(name+('.cpp' if compiler=='c++' else '.c'));p.write_text(code);exe=Path(tmp)/name
        subprocess.run([compiler,'-std='+standard,'-Wall','-Wextra','-Werror','-Wpedantic','-pthread',
                        '-I'+str(root/'include'),'-I'+str(root/'platform/adv'),str(p),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
print('adv_ft8_web_io: PASS')
