#include <assert.h>
#include <stdlib.h>
/* Exercise the production controller, encoder, radio, codec and logging.
 * Only MiniShell services are mocked; private RX state is inspected for continuity. */
#define FT8_APP_CONTROLLER_INTERNAL 1
#include "../apps/ft8/src/app_controller/app_controller.c"
#include "app_controller_tx.h"
#include "radio_qmx.h"

static struct { char path[256], text[16384]; size_t size, pos; bool exists, open; } files[12];
static char cat[8192], events[8192], expected[8192];
static uint64_t now_us, anchor;
static uint64_t tone_times[100];
static unsigned tone_count, starts, stops, reads, audio_closes, serial_closes, syncs, file_closes;
static unsigned fail_command, serial_writes, fail_rt; /* RT: open/write/sync/close */
static unsigned stop_delay_ms, write_delay_us;
static bool stop_fail, restart_fail, rt_short, rt_zero, record_expected, short_cat;
static unsigned audio_started;
static bool fail_station_save;
static const AppController *keying_app;
static const char *station_text = "callsign=AG6AQ\ngrid=CM97\ncq_type=0\noffset_src=1\noffset=1500\nband=3\nfd_exchange=1B SCV\n";
static void event(char c) { size_t n = strlen(events); assert(n + 1 < sizeof(events)); events[n] = c; events[n+1] = 0; }
static uint64_t mono(void) { return now_us; }
static mini_result_t utc(mini_utc_time_t *time)
{
    time->unix_seconds = 1789776000 + (int64_t)(now_us / 1000000u); /* 2026-09-19 00:00:00 + elapsed */
    time->nanoseconds = (uint32_t)(now_us % 1000000u) * 1000u;
    return MINI_OK;
}
static mini_result_t alloc_mem(uint32_t size, void **out) { *out = calloc(1, size); return *out ? MINI_OK : MINI_ERR_NO_MEMORY; }
static mini_result_t free_mem(void *p) { free(p); return MINI_OK; }
static void diagnostic(const char *message) { assert(strstr(message, "ft8:")); event('!'); }
static unsigned path_id(const char *path)
{
    for (unsigned i = 0; i < 12; ++i) if (strcmp(path, files[i].path) == 0) return i;
    for (unsigned i = 0; i < 12; ++i) if (!files[i].path[0]) { snprintf(files[i].path, sizeof(files[i].path), "%s", path); return i; }
    abort();
}
static bool is_rt(unsigned i) { return strstr(files[i].path, "/RT") != NULL; }
static mini_result_t fs_open(const char *path, uint32_t flags, mini_file_t *out)
{
    unsigned i = path_id(path);
    if (is_rt(i)) {
        assert(flags == (MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_APPEND));
        if (fail_rt == 1) return MINI_ERR_IO;
    }
    assert(!files[i].open);
    if (!files[i].exists && !(flags & MINI_FS_CREATE)) return MINI_ERR_NOT_FOUND;
    if (flags & MINI_FS_TRUNC) files[i].size = 0;
    files[i].pos = flags & MINI_FS_APPEND ? files[i].size : 0;
    files[i].exists = files[i].open = true; *out = i + 1;
    return MINI_OK;
}
static mini_result_t fs_close(mini_file_t handle)
{
    unsigned i = handle - 1; assert(i < 12 && files[i].open);
    files[i].open = false; ++file_closes;
    if (is_rt(i)) { event('L'); if (fail_rt == 4) return MINI_ERR_IO; }
    return MINI_OK;
}
static mini_result_t fs_read(mini_file_t handle, void *buf, uint32_t count, uint32_t *out)
{
    unsigned i = handle - 1; assert(i < 12 && files[i].open);
    if (count > files[i].size - files[i].pos) count = (uint32_t)(files[i].size - files[i].pos);
    memcpy(buf, files[i].text + files[i].pos, count); files[i].pos += count; *out = count; return MINI_OK;
}
static mini_result_t fs_write(mini_file_t handle, const void *buf, uint32_t count, uint32_t *out)
{
    unsigned i = handle - 1; assert(i < 12 && files[i].open); *out = 0;
    if (is_rt(i) && fail_rt == 2) return MINI_ERR_IO;
    if (is_rt(i) && rt_zero) return MINI_OK;
    if (is_rt(i) && rt_short && count > 7) count = 7;
    assert(files[i].pos + count < sizeof(files[i].text));
    memcpy(files[i].text + files[i].pos, buf, count); files[i].pos += count;
    if (files[i].pos > files[i].size) files[i].size = files[i].pos;
    files[i].text[files[i].size] = 0; *out = count; return MINI_OK;
}
static mini_result_t fs_sync(mini_file_t handle)
{ assert(files[handle-1].open); ++syncs; return is_rt(handle-1) && fail_rt == 3 ? MINI_ERR_IO : MINI_OK; }
static mini_result_t fs_stat(const char *path, mini_fs_stat_t *out)
{
    if (strcmp(path, "/flash/ft8") == 0) { out->type = MINI_FS_TYPE_DIRECTORY; return MINI_OK; }
    unsigned i = path_id(path); if (!files[i].exists) return MINI_ERR_NOT_FOUND;
    out->type = MINI_FS_TYPE_FILE; out->size = files[i].size; return MINI_OK;
}
static mini_result_t fs_rename(const char *from, const char *to)
{
    unsigned a = path_id(from), b = path_id(to); assert(!files[a].open && !files[b].open);
    if (fail_station_save) return MINI_ERR_IO;
    memcpy(files[b].text, files[a].text, files[a].size + 1); files[b].size = files[a].size;
    files[b].exists = true; files[a].exists = false; return MINI_OK;
}
static mini_result_t fs_remove(const char *path) { files[path_id(path)].exists = false; return MINI_OK; }
static mini_result_t fs_mkdir(const char *path) { (void)path; abort(); }
static mini_result_t fs_seek(mini_file_t handle, int64_t offset, uint32_t origin, uint64_t *out)
{
    unsigned i = handle - 1; assert(origin == MINI_FS_SEEK_SET && offset >= 0 && (uint64_t)offset <= files[i].size);
    files[i].pos = (size_t)offset; *out = (uint64_t)offset; return MINI_OK;
}
static mini_result_t audio_open(const char *ep, const mini_audio_format_t *format, mini_audio_stream_t *out)
{ assert(strcmp(ep,"test:live") == 0 && format->sample_rate_hz == 12000); *out = 1; return MINI_OK; }
static mini_result_t audio_start(mini_audio_stream_t stream)
{ assert(stream == 1); ++starts; event('a'); if (restart_fail && starts > 1) return MINI_ERR_IO; audio_started = 1; return MINI_OK; }
static mini_result_t audio_stop(mini_audio_stream_t stream)
{ assert(stream == 1); ++stops; event('s'); now_us += stop_delay_ms * 1000u; if (stop_fail) return MINI_ERR_IO; audio_started = 0; return MINI_OK; }
static mini_result_t audio_close(mini_audio_stream_t stream)
{ assert(stream == 1); ++audio_closes; audio_started = 0; event('c'); return MINI_OK; }
static mini_result_t audio_read(mini_audio_stream_t stream, void *frames, uint32_t count, uint32_t *out, uint32_t timeout)
{ (void)timeout; assert(stream == 1 && audio_started); ++reads; memset(frames, 0, count * 4); *out = count; return MINI_OK; }
static mini_result_t serial_open(const char *ep, mini_serial_t *out)
{ assert(strcmp(ep,"test:cat") == 0); *out = 7; return MINI_OK; }
static mini_result_t serial_close(mini_serial_t stream)
{ assert(stream == 7); ++serial_closes; event('C'); return MINI_OK; }
static mini_result_t serial_write(mini_serial_t stream, const void *buf, uint32_t count, uint32_t *out, uint32_t timeout)
{
    assert(stream == 7);
    if (record_expected) { strncat(expected, buf, count); *out = count; return MINI_OK; }
    assert(strlen(cat) + count < sizeof(cat)); strncat(cat, buf, count);
    bool ta = memcmp(buf,"TA",2) == 0;
    assert(timeout == (ta ? 10u : 200u));
    if (ta) { assert(tone_count < 100); tone_times[tone_count++] = now_us; event('t'); }
    if (count == 3 && memcmp(buf,"TX;",3) == 0) {
        if (keying_app) assert(keying_app->tx.plan.valid &&
            strcmp(keying_app->tx.plan.canonical_text,"CQ POTA AG6AQ CM97")==0);
        event('B');
    }
    if (count == 3 && memcmp(buf,"RX;",3) == 0) event('E');
    now_us += write_delay_us;
    bool fail = ++serial_writes == fail_command;
    *out = fail && short_cat ? count - 1 : count;
    return fail && !short_cat ? MINI_ERR_TIMEOUT : MINI_OK;
}
static const mini_fs_api_t fs = {.struct_size=sizeof(fs), .open=fs_open, .close=fs_close, .read=fs_read,
    .write=fs_write, .sync=fs_sync, .stat=fs_stat, .rename=fs_rename, .remove_file=fs_remove, .mkdir=fs_mkdir, .seek=fs_seek};
static const mini_memory_api_t memory = {.alloc=alloc_mem, .free=free_mem};
static const mini_time_location_api_t clock_api = {.struct_size=sizeof(clock_api), .capabilities=MINI_TIMELOC_CAP_UTC,
    .utc_get=utc, .monotonic_us=mono};
static const mini_audio_rx_api_t audio_rx = {.struct_size=sizeof(audio_rx), .open=audio_open, .start=audio_start,
    .read=audio_read, .stop=audio_stop, .close=audio_close};
static const mini_audio_api_t audio = {.struct_size=sizeof(audio), .capabilities=MINI_AUDIO_CAP_RX, .rx=&audio_rx};
static const mini_serial_api_t serial = {.struct_size=sizeof(serial), .capabilities=MINI_SERIAL_CAP_WRITE,
    .open=serial_open, .write=serial_write, .close=serial_close};
static const mini_system_api_t system_api = {.write=diagnostic};
static const mini_api_t api = {.struct_size=sizeof(api), .fs=&fs, .memory=&memory, .time_location=&clock_api,
    .audio=&audio, .serial=&serial, .system=&system_api};

static void reset(void)
{
    fail_station_save=false; keying_app=NULL;
    memset(files, 0, sizeof(files)); cat[0]=events[0]=expected[0]=0;
    tone_count=starts=stops=reads=audio_closes=serial_closes=syncs=file_closes=0;
    fail_command=serial_writes=fail_rt=stop_delay_ms=write_delay_us=0;
    stop_fail=restart_fail=rt_short=rt_zero=record_expected=short_cat=false;
    now_us=1000000000u; anchor=1005000000u; /* next natural 15 s boundary */
    unsigned i=path_id("/flash/ft8/station.txt"); strcpy(files[i].text,station_text);
    files[i].size=strlen(station_text); files[i].exists=true;
}
static void setup(AppController *app, bool live)
{
    reset(); assert(app_controller_init(app,&api,"/flash/ft8","/flash/ft8/station.txt"));
    assert(strcmp(app->auto_seq.config.callsign,"AG6AQ")==0 && strcmp(app->auto_seq.config.grid,"CM97")==0);
    assert(app_controller_start_cat(app,"test:cat")==MINI_OK);
    assert(strcmp(cat,"MD6;FR0;FT0;FA00014074000;")==0);
    if (live) { AppRxStartConfig cfg={.endpoint="test:live"}; assert(app_controller_start_rx(app,&cfg)); }
    assert(auto_seq_start_cq(&app->auto_seq,1)==AUTO_SEQ_OK);
    bool changed; assert(app_controller_step_tx(app,&changed) && !changed); /* anchor only */
    if (live) {
        app->rx->have_batch=true; app->rx->batch_generation=1;
        app->rx->batch.slot_id=(1789776000+1005)/15-1; app->rx->batch.message_count=0;
        assert(app_process_addressed_batch(app));
    }
    serial_writes=0; cat[0]=events[0]=0;
    now_us=anchor;
}
static const char *rt_contents(void)
{
    for (unsigned i=0;i<12;++i) if (is_rt(i)) return files[i].text;
    return "";
}
static void cleanup(AppController *app)
{
    app_controller_shutdown(app);
    assert(!app->radio.stream && !app->rx && !app->tx.active && !app->tx.rx_paused);
    for(unsigned i=0;i<12;++i) assert(!files[i].open);
}

static void success(unsigned poll_ms, unsigned late_ms, unsigned write_us)
{
    AppController app; setup(&app,true); now_us+=late_ms*1000u; write_delay_us=write_us;
    AutoSeqTxIntent intent; assert(auto_seq_prepare_tx_intent(&app.auto_seq,&intent));
    Ft8TxPlan plan; assert(ft8_tx_encode(&intent,&plan)==FT8_TX_ENCODE_OK);
    assert(strcmp(plan.canonical_text,"CQ AG6AQ CM97")==0);
    UiModel model; app_controller_build_model(&app,&model); assert(!model.tx_active);
    bool changed; assert(app_controller_step_tx(&app,&changed));
    app_controller_build_model(&app,&model); assert(model.tx_active);
    assert(app.tx.active && !changed && !app_controller_rx_active(&app));
    assert(strcmp(events,"sLBt")==0);
    assert(app.tx.schedule.slot_start_us==anchor && !app.tx.physical_tx_count);
    assert(auto_seq_active_count(&app.auto_seq)==1);
    assert(app.rx->timing_pending && app.rx->frontend.decimation_phase==0);
    assert(app_controller_step_rx(&app,&changed) && reads==0);
    unsigned first=late_ms/160; assert(app.tx.schedule.next_tone==first+1);
    AppAction action={.type=APP_ACTION_DROP_TX_QSO,.value.index=0};
    assert(app_controller_apply_action(&app,&action) && auto_seq_active_count(&app.auto_seq)==1);
    while(now_us < anchor+12640000u) {
        now_us+=poll_ms*1000u;
        assert(app_controller_step_tx(&app,&changed));
        if(app.tx.active) {
            assert(!changed && auto_seq_active_count(&app.auto_seq)==1);
            assert(memcmp(&app.tx.plan,&plan,sizeof(plan))==0);
        }
    }
    assert(!app.tx.active && changed && app.tx.physical_tx_count==1 && !app.tx.failed_tx_count);
    app_controller_build_model(&app,&model); assert(!model.tx_active);
    assert(auto_seq_active_count(&app.auto_seq)==0 && app_controller_rx_active(&app));
    assert(strstr(events,"Ea") && starts==2 && stops==1);
    assert(app.rx->timing_pending);
    assert(app_controller_step_rx(&app,&changed));
    assert(reads == 1u + RX_READY_DRAIN_LIMIT);
    assert(!app.rx->timing_pending);
    assert(app.rx->live_capture_schedule_valid && !app.rx->live_capture_active);
    assert(app.rx->live_next_capture_slot == app.rx->framer.slot_id + 1);
    assert(app.rx->framer.slot_id==(1789776000+(int64_t)(now_us/1000000u))/15);
    /* Expected bytes use the production formatter; timing expectations use the immutable plan. */
    strcpy(expected,"MD6;TX;"); record_expected=true;
    int previous=-1; unsigned emitted=0;
    for(unsigned i=first;i<79;++i) if(plan.tones[i]!=previous) {
        assert(radio_qmx_set_tone_hz(&serial,7,ft8_tx_tone_hz(plan.base_hz,plan.tones[i]))==MINI_OK);
        uint64_t lateness=tone_times[emitted++]-anchor-i*160000u;
        assert(i==first || lateness <= poll_ms*1000u+write_us);
        previous=plan.tones[i];
    }
    strcat(expected,"RX;"); record_expected=false;
    assert(tone_count==emitted && strcmp(cat,expected)==0);
    assert(strstr(rt_contents(),"] CQ AG6AQ CM97 1500\n"));
    cleanup(&app);
}

static void failures(void)
{
    for(unsigned which=0;which<10;++which) for(unsigned short_mode=0;short_mode<2;++short_mode) {
        AppController app; setup(&app,true); short_cat=short_mode!=0;
        if(which==0) fail_rt=2;
        if(which==1) stop_fail=true;
        if(which==2) fail_command=1;
        if(which==3) fail_command=2;
        if(which==4) fail_command=3;
        if(which==5) fail_command=4;
        if(which==6) fail_command=4;
        if(which==7) restart_fail=true;
        if(which==8) strcpy(app.auto_seq.config.callsign,"INVALID!");
        if(which==9) stop_delay_ms=1100;
        bool changed; assert(app_controller_step_tx(&app,&changed));
        if(which==5) { now_us=anchor+160000; assert(app_controller_step_tx(&app,&changed)); }
        if(which==6 || which==7) {
            now_us=anchor+12640000;
            bool ok=app_controller_step_tx(&app,&changed); assert(ok==(which!=7));
        }
        assert(!app.tx.active && !changed && app.tx.physical_tx_count==0 && app.tx.failed_tx_count==1);
        assert(auto_seq_active_count(&app.auto_seq)==1);
        assert(app.auto_seq.queue[0].retry_counter==0);
        if(which>=3 && which<=7) assert(strstr(cat,"RX;"));
        if(which==0 || which==1 || which==2 || which==8 || which==9) assert(!strstr(cat,"TX;"));
        if(which!=1 && which!=8) assert(starts==2);
        stop_fail=false; restart_fail=false; fail_rt=fail_command=0;
        cleanup(&app);
    }
}

static void freshness_and_stalls(void)
{
    AppController app; setup(&app,true); app.rx->have_applied_batch=false;
    bool changed; assert(app_controller_step_tx(&app,&changed) && !app.tx.active && app.tx.pending && !cat[0]);
    UiModel projection; app_controller_build_model(&app,&projection); assert(!projection.tx_active);
    now_us=anchor+499000;
    assert(app_controller_step_tx(&app,&changed) && !app.tx.active);
    app.rx->have_applied_batch=true;
    assert(app_controller_step_tx(&app,&changed) && app.tx.active && app.tx.schedule.next_tone==4);
    unsigned before=tone_count; now_us=anchor+3000000;
    assert(app_controller_step_tx(&app,&changed) && tone_count<=before+1 && app.tx.schedule.next_tone==19);
    assert(app_controller_step_tx(&app,&changed) && tone_count<=before+1);
    now_us=anchor+2900000; assert(app_controller_step_tx(&app,&changed) && !app.tx.active && app.tx.failed_tx_count==1);
    app_controller_build_model(&app,&projection); assert(!projection.tx_active);
    assert(auto_seq_active_count(&app.auto_seq)==1); cleanup(&app);
    setup(&app,true); app.rx->have_applied_batch=false;
    assert(app_controller_step_tx(&app,&changed)); now_us=anchor+1000000;
    app.rx->have_applied_batch=true; assert(app_controller_step_tx(&app,&changed) && !app.tx.active && !app.tx.pending);
    now_us=anchor+15000000; assert(app_controller_step_tx(&app,&changed) && !app.tx.active && !cat[0]);
    cleanup(&app);
    setup(&app,true); assert(app_controller_step_tx(&app,&changed) && app.tx.active);
    cleanup(&app); assert(strstr(events,"ECc") && serial_closes==1 && audio_closes==1);
}

static void qso_completion(void)
{
    AppController app; setup(&app,true);
    AppAction view = {.type=APP_ACTION_LOAD_QSO_PAGE, .value.page_index=0};
    assert(app_controller_apply_action(&app, &view));
    assert(app.qso_loaded && !app.qso.total_count);
    assert(auto_seq_drop_index(&app.auto_seq,0,0));
    AutoSeqRxEvent event_rx={.kind=AUTO_SEQ_MSG_TX1, .flags=AUTO_SEQ_RX_FLAG_CQ|AUTO_SEQ_RX_FLAG_FD,
        .rx_slot_id=(1789776000+1005)/15-1, .offset_hz=1500, .snr_db=-12, .report_db=AUTO_SEQ_SNR_UNKNOWN};
    strcpy(event_rx.dxcall,"W6ABC");
    assert(auto_seq_on_manual_rx(&app.auto_seq,&event_rx)==AUTO_SEQ_OK);
    app.rx->have_applied_batch=false;
    bool changed; assert(app_controller_step_tx(&app,&changed) && app.tx.pending && !cat[0]);
    /* A just-finished previous-slot decode changes the pending reply to RR73. */
    RxMessage message; memset(&message,0,sizeof(message));
    strcpy(message.canonical_text,"AG6AQ W6ABC R 2A ORG"); strcpy(message.call_de,"W6ABC");
    strcpy(message.fd_exchange,"2A ORG"); message.is_to_me=true; message.is_fd=true;
    message.parse_status=FT8_PROTOCOL_PARSE_OK; message.qso_kind=RX_QSO_MSG_TX3;
    message.snr_db=-12; message.offset_hz=1500;
    app.rx->batch.messages=&message; app.rx->batch.message_count=1; ++app.rx->batch_generation;
    now_us=anchor+200000; assert(app_process_addressed_batch(&app));
    assert(app_controller_step_tx(&app,&changed) && app.tx.active && !changed);
    assert(strcmp(app.tx.plan.canonical_text,"W6ABC AG6AQ RR73")==0);
    assert(app.tx.schedule.next_tone==2 && app.tx.last_log_event_valid);
    unsigned adif=path_id("/flash/ft8/20260919.txt"), cabrillo=path_id("/flash/ft8/fieldday.txt");
    assert(!files[adif].exists && !files[cabrillo].exists);
    assert(app.auto_seq.queue[0].retry_counter==0 && !(app.auto_seq.queue[0].flags&AUTO_SEQ_FLAG_LOGGED));
    now_us=anchor+12640000; assert(app_controller_step_tx(&app,&changed) && changed);
    assert(files[adif].exists && files[cabrillo].exists);
    assert(app.qso_dirty);
    AutoSeq logged_seq = app.auto_seq;
    app_controller_step_qso(&app);
    assert(!app.qso_dirty && app.qso.total_count == 1 && strcmp(app.qso.rows[0].call, "W6ABC") == 0);
    assert(memcmp(&logged_seq, &app.auto_seq, sizeof(logged_seq)) == 0);
    assert(strstr(files[adif].text,"AG6AQ") && strstr(files[adif].text,"CM97"));
    assert(strstr(files[cabrillo].text,"AG6AQ") && strstr(files[cabrillo].text,"W6ABC"));
    assert(app.auto_seq.queue[0].retry_counter==1);
    assert((app.auto_seq.queue[0].flags & (AUTO_SEQ_FLAG_LOGGED|AUTO_SEQ_FLAG_CABRILLO_LOGGED)) ==
           (AUTO_SEQ_FLAG_LOGGED|AUTO_SEQ_FLAG_CABRILLO_LOGGED));
    unsigned saved=file_closes;
    assert(app_controller_step_tx(&app,&changed) && !changed && file_closes==saved && app.tx.physical_tx_count==1);
    assert(app.auto_seq.queue[0].retry_counter==1);
    cleanup(&app);
    setup(&app,false); assert(radio_control_close(&app.radio)==MINI_OK); cat[0]=0;
    assert(app_controller_step_tx(&app,&changed) && changed && app.tx.simulated_tx_count==1);
    assert(!app.tx.physical_tx_count && !app.tx.active && !cat[0] && auto_seq_active_count(&app.auto_seq)==0);
    cleanup(&app);
}

static void rt_logging(void)
{
    reset(); LogService log; assert(log_service_init(&log,&fs,&clock_api,"/flash/ft8/station.txt"));
    now_us=0; rt_short=true;
    assert(log_service_write_rt(&log,true,3,"W1ABC K9XYZ -12",0,1500));
    assert(log_service_write_rt(&log,false,1,"K9XYZ W1ABC R-08",-12,1500));
    assert(strcmp(files[path_id("/flash/ft8/RT260919.txt")].text,
        "T [20260919 000000][14.074] W1ABC K9XYZ -12 1500\n"
        "R [20260919 000000][7.074] K9XYZ W1ABC R-08 -12 1500\n")==0);
    assert(syncs==2 && file_closes==2);
    for(fail_rt=1;fail_rt<=4;++fail_rt) {
        assert(!log_service_write_rt(&log,true,3,"CQ AG6AQ CM97",0,1500));
        for(unsigned i=0;i<12;++i) assert(!files[i].open);
    }
    fail_rt=0; rt_zero=true; assert(!log_service_write_rt(&log,true,3,"CQ AG6AQ CM97",0,1500));
    AppController app; setup(&app,true);
    RxMessage messages[2]; memset(messages,0,sizeof(messages));
    strcpy(messages[0].canonical_text,"CQ W1ABC FN42"); messages[0].snr_db=-12; messages[0].offset_hz=1500;
    strcpy(messages[1].canonical_text,"CQ K9XYZ FN42"); messages[1].snr_db=-8; messages[1].offset_hz=1600;
    app.rx->batch.messages=messages; app.rx->batch.message_count=2; ++app.rx->batch_generation;
    assert(app_process_addressed_batch(&app)); unsigned saved=file_closes;
    assert(strstr(rt_contents(),"CQ W1ABC FN42 -12 1500\n") && strstr(rt_contents(),"CQ K9XYZ FN42 -8 1600\n"));
    assert(app_process_addressed_batch(&app) && file_closes==saved);
    fail_rt=2; ++app.rx->batch_generation;
    assert(app_process_addressed_batch(&app) && app.rx->rt_log_failures==2 && strchr(events,'!'));
    app.config.rxtx_log=false; ++app.rx->batch_generation; saved=file_closes;
    assert(app_process_addressed_batch(&app) && file_closes==saved);
    fail_rt=0; bool changed;
    assert(app_controller_step_tx(&app,&changed) && app.tx.active && file_closes==saved);
    cleanup(&app);
    ConfigService config; config_service_defaults(&config); assert(config.rxtx_log);
    assert(config_service_parse(&config,"callsign=AG6AQ\ngrid=CM97\n") && config.rxtx_log);
    assert(config_service_parse(&config,"rxtx_log=0\n") && !config.rxtx_log);
    char text[2048]; assert(config_service_serialize(&config,text,sizeof(text)) && strstr(text,"rxtx_log=0\n"));
    assert(config_service_parse(&config,"rxtx_log=1\n") && config.rxtx_log);
    assert(config_service_serialize(&config,text,sizeof(text)) && strstr(text,"rxtx_log=1\n"));
    assert(!config_service_parse(&config,"rxtx_log=2\n") && config.rxtx_log);
}

static void apply_setting(AppController *app, AppActionType type, int value)
{
    AppAction action={.type=type,.value.int_value=value};
    assert(app_controller_apply_action(app,&action));
}

static void cq_beacon_settings(void)
{
    AppController app; setup(&app,false);
    UiModel model; app_controller_build_model(&app,&model);
    assert(model.cq_type==UI_CQ && model.beacon_mode==UI_BEACON_OFF);
    unsigned station=path_id("/flash/ft8/station.txt");
    for (unsigned i=0;i<2;++i) {
        apply_setting(&app,APP_ACTION_SET_CQ_TYPE,i==0 ? UI_CQ_POTA : UI_CQ);
        assert(app.config.cq_type==(i==0 ? FT8_CONFIG_CQ_POTA : FT8_CONFIG_CQ));
        assert(app.auto_seq.config.cq_type==(i==0 ? AUTO_SEQ_CQ_POTA : AUTO_SEQ_CQ));
        assert(strstr(files[station].text,i==0 ? "cq_type=2\n" : "cq_type=0\n"));
        AutoSeqTxIntent intent; Ft8TxPlan plan;
        assert(auto_seq_prepare_tx_intent(&app.auto_seq,&intent));
        assert(ft8_tx_encode(&intent,&plan)==FT8_TX_ENCODE_OK);
        assert(strcmp(plan.canonical_text,i==0 ? "CQ POTA AG6AQ CM97" : "CQ AG6AQ CM97")==0);
        app_controller_build_model(&app,&model);
        assert(model.cq_type==(i==0 ? UI_CQ_POTA : UI_CQ));
    }
    char saved[2048]; strcpy(saved,files[station].text);
    fail_station_save=true;
    AppAction action={.type=APP_ACTION_SET_CQ_TYPE,.value.int_value=UI_CQ_POTA};
    assert(!app_controller_apply_action(&app,&action));
    assert(app.config.cq_type==FT8_CONFIG_CQ && app.auto_seq.config.cq_type==AUTO_SEQ_CQ);
    assert(strcmp(saved,files[station].text)==0);
    fail_station_save=false;
    action.value.int_value=UI_CQ_UNAVAILABLE; assert(!app_controller_apply_action(&app,&action));
    action.value.int_value=-1; assert(!app_controller_apply_action(&app,&action));
    unsigned closes=file_closes;
    const UiBeaconMode modes[]={UI_BEACON_EVEN,UI_BEACON_ODD,UI_BEACON_OFF,
                                UI_BEACON_ODD,UI_BEACON_EVEN,UI_BEACON_OFF};
    for (unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);++i) {
        apply_setting(&app,APP_ACTION_SET_BEACON_MODE,modes[i]);
        app_controller_build_model(&app,&model); assert(model.beacon_mode==modes[i]);
        assert(file_closes==closes && strcmp(saved,files[station].text)==0);
        assert(!strstr(files[station].text,"beacon"));
    }
    assert(auto_seq_active_count(&app.auto_seq)==0); /* mode change removed stale CQ */
    action.type=APP_ACTION_SET_BEACON_MODE; action.value.int_value=3;
    assert(!app_controller_apply_action(&app,&action));
    action.value.int_value=256; assert(!app_controller_apply_action(&app,&action));
    /* OFF specifically removes a newly queued one-shot; it never parks a beacon queue. */
    apply_setting(&app,APP_ACTION_SET_BEACON_MODE,UI_BEACON_EVEN);
    assert(auto_seq_start_cq(&app.auto_seq,0)==AUTO_SEQ_OK);
    apply_setting(&app,APP_ACTION_SET_BEACON_MODE,UI_BEACON_OFF);
    assert(auto_seq_active_count(&app.auto_seq)==0);
    cleanup(&app);
    assert(app_controller_init(&app,&api,"/flash/ft8","/flash/ft8/station.txt"));
    assert(app_controller_get_beacon_mode(&app)==TX_BEACON_OFF); cleanup(&app);

    for (unsigned parity=0;parity<2;++parity) {
        setup(&app,false); apply_setting(&app,APP_ACTION_SET_CQ_TYPE,UI_CQ_POTA);
        apply_setting(&app,APP_ACTION_SET_BEACON_MODE,parity ? UI_BEACON_ODD : UI_BEACON_EVEN);
        assert(!auto_seq_active_count(&app.auto_seq));
        bool changed;
        if (!parity) { /* first observed boundary is odd */
            assert(app_controller_step_tx(&app,&changed) && !app.tx.active && !cat[0]);
            now_us+=15000000;
        }
        keying_app=&app;
        assert(app_controller_step_tx(&app,&changed) && app.tx.active);
        assert(app.tx.plan.tx_parity==parity && strstr(rt_contents(),"CQ POTA AG6AQ CM97"));
        Ft8TxPlan plan=app.tx.plan; closes=file_closes;
        apply_setting(&app,APP_ACTION_SET_CQ_TYPE,UI_CQ);
        apply_setting(&app,APP_ACTION_SET_BEACON_MODE,UI_BEACON_OFF);
        app_controller_build_model(&app,&model);
        assert(model.cq_type==UI_CQ_POTA && model.beacon_mode==(parity ? UI_BEACON_ODD : UI_BEACON_EVEN));
        assert(memcmp(&plan,&app.tx.plan,sizeof(plan))==0 && file_closes==closes);
        cleanup(&app);
    }
    /* A higher-priority reply or free text keeps its own parity despite beacon EVEN. */
    for (unsigned free_text=0;free_text<2;++free_text) {
        setup(&app,false); apply_setting(&app,APP_ACTION_SET_BEACON_MODE,UI_BEACON_EVEN);
        if (free_text) assert(auto_seq_schedule_freetext(&app.auto_seq,"HELLO",1)==AUTO_SEQ_OK);
        else {
            AutoSeqRxEvent rx={.kind=AUTO_SEQ_MSG_TX1,.flags=AUTO_SEQ_RX_FLAG_CQ,
                .rx_slot_id=(1789776000+1005)/15-1,.offset_hz=1500,.snr_db=-12};
            strcpy(rx.dxcall,"W6ABC"); strcpy(rx.dxgrid,"CM87");
            assert(auto_seq_on_manual_rx(&app.auto_seq,&rx)==AUTO_SEQ_OK);
        }
        assert(radio_control_close(&app.radio)==MINI_OK);
        bool changed; int64_t even=(1789776000+1005)/15+1;
        tx_lifecycle_init(&app.tx.lifecycle);
        apply_setting(&app,APP_ACTION_SET_BEACON_MODE,UI_BEACON_EVEN);
        assert(app_controller_observe_tx_slot(&app,even-1,0,0,&changed));
        assert(app_controller_observe_tx_slot(&app,even,0,1000,&changed) && !changed);
        assert(auto_seq_active_count(&app.auto_seq)==1 && !app.tx.simulated_tx_count);
        assert(app_controller_observe_tx_slot(&app,even+1,0,16000,&changed) && changed);
        assert(app.tx.last_intent.type==(free_text ? AUTO_SEQ_TX_INTENT_FREETEXT : AUTO_SEQ_TX_INTENT_QSO));
        assert(app.tx.last_intent.tx_parity==1); cleanup(&app);
    }
}

static void offset_integration(void)
{
    AppController app;
    const char *original=station_text;
    station_text="callsign=AG6AQ\ngrid=CM97\ncq_type=2\nband=3\noffset_src=0\noffset=1600\n";
    setup(&app,false); station_text=original;
    assert(app.tx.offset_rng==tx_offset_seed(1000000000u,1789777000,0));
    apply_setting(&app,APP_ACTION_SET_CQ_TYPE,UI_CQ_POTA);
    unsigned station=path_id("/flash/ft8/station.txt");
    assert(strstr(files[station].text,"offset_src=0\n") && strstr(files[station].text,"offset=1600\n"));
    apply_setting(&app,APP_ACTION_SET_BEACON_MODE,UI_BEACON_ODD);
    app.tx.offset_rng=1; keying_app=&app;
    const int16_t bases[]={734,1389};
    const uint32_t states[]={270369,67634689};
    for (unsigned attempt=0;attempt<2;++attempt) {
        bool changed; cat[0]=0; tone_count=0;
        assert(app_controller_step_tx(&app,&changed) && app.tx.active);
        assert(app.tx.plan.base_hz==bases[attempt] && app.tx.last_intent.offset_hz==bases[attempt]);
        assert(app.tx.offset_rng==states[attempt] && app.auto_seq.queue[0].offset_hz==1500);
        assert(strcmp(app.tx.plan.canonical_text,"CQ POTA AG6AQ CM97")==0);
        char record[80]; snprintf(record,sizeof(record),"] CQ POTA AG6AQ CM97 %d\n",bases[attempt]);
        assert(strstr(rt_contents(),record));
        Ft8TxPlan plan=app.tx.plan;
        uint64_t start=now_us;
        for (unsigned i=1;i<=79;++i) {
            now_us=start+i*160000u;
            assert(app_controller_step_tx(&app,&changed));
            assert(memcmp(&plan,&app.tx.plan,sizeof(plan))==0 && app.tx.offset_rng==states[attempt]);
        }
        assert(!app.tx.active && app.tx.physical_tx_count==attempt+1);
        strcpy(expected,"MD6;TX;"); record_expected=true;
        int previous=-1;
        for (unsigned i=0;i<79;++i) {
            float hz=ft8_tx_tone_hz(plan.base_hz,plan.tones[i]);
            assert(hz>=500.0f && hz<=2543.75f);
            if (previous!=plan.tones[i]) assert(radio_qmx_set_tone_hz(&serial,7,hz)==MINI_OK);
            previous=plan.tones[i];
        }
        record_expected=false; strcat(expected,"RX;"); assert(strcmp(cat,expected)==0);
        now_us=start+15000000; assert(app_controller_step_tx(&app,&changed) && !app.tx.active);
        assert(app.tx.offset_rng==states[attempt]); now_us=start+30000000;
    }
    cleanup(&app);

    for (unsigned source=0;source<3;++source) {
        char station_fixture[128];
        snprintf(station_fixture,sizeof(station_fixture),
            "callsign=AG6AQ\ngrid=CM97\nband=3\noffset_src=%u\noffset=1600\n",source);
        station_text=station_fixture; setup(&app,false); station_text=original;
        assert(auto_seq_drop_index(&app.auto_seq,0,0));
        AutoSeqRxEvent rx={.kind=AUTO_SEQ_MSG_TX1,.flags=AUTO_SEQ_RX_FLAG_CQ,
            .rx_slot_id=(1789776000+1005)/15-1,.offset_hz=1234,.snr_db=-12};
        strcpy(rx.dxcall,"W6ABC"); strcpy(rx.dxgrid,"CM87");
        assert(auto_seq_on_manual_rx(&app.auto_seq,&rx)==AUTO_SEQ_OK);
        app.tx.offset_rng=1;
        bool changed; assert(app_controller_step_tx(&app,&changed) && app.tx.active);
        int16_t base=source==0 ? 734 : source==1 ? 1600 : 1234;
        assert(app.tx.plan.base_hz==base && app.tx.last_intent.offset_hz==base);
        assert(app.auto_seq.queue[0].offset_hz==1234);
        AutoSeqTxIntent semantic; assert(auto_seq_prepare_tx_intent(&app.auto_seq,&semantic));
        assert(semantic.offset_hz==1234); cleanup(&app);
    }
    for (unsigned failure=0;failure<4;++failure) {
        setup(&app,true); app.config.offset_src=FT8_OFFSET_RANDOM; app.tx.offset_rng=1;
        if (failure==0) fail_rt=2;
        if (failure==1) stop_fail=true;
        if (failure==2) fail_command=2;
        if (failure==3) fail_command=3;
        bool changed; assert(app_controller_step_tx(&app,&changed) && !app.tx.active);
        assert(app.tx.offset_rng==270369 && app.tx.failed_tx_count==1 && !app.tx.physical_tx_count);
        assert(app.auto_seq.queue[0].offset_hz==1500 && app.auto_seq.queue[0].retry_counter==0);
        stop_fail=false; fail_rt=fail_command=0;
        now_us=anchor+15000000; assert(app_controller_step_tx(&app,&changed) && !app.tx.active);
        app.rx->applied_slot+=2;
        now_us=anchor+30000000; assert(app_controller_step_tx(&app,&changed) && app.tx.active);
        assert(app.tx.offset_rng==67634689 && app.tx.plan.base_hz==1389);
        assert(app.auto_seq.queue[0].offset_hz==1500 && app.auto_seq.queue[0].retry_counter==0);
        cleanup(&app);
    }
    setup(&app,false); app.config.offset_src=FT8_OFFSET_RANDOM; app.tx.offset_rng=1;
    assert(radio_control_close(&app.radio)==MINI_OK);
    bool changed; assert(app_controller_step_tx(&app,&changed) && changed);
    assert(app.tx.simulated_tx_count==1 && app.tx.last_intent.offset_hz==1500 && app.tx.offset_rng==1);
    cleanup(&app);
    reset(); mini_api_t no_clock=api; no_clock.time_location=NULL;
    assert(app_controller_init(&app,&no_clock,"/flash/ft8","/flash/ft8/station.txt"));
    assert(app.tx.offset_rng==tx_offset_seed(0,0,0)); cleanup(&app);
}

static void decoded_standard(AppController *app, const char *to, const char *from,
                             const char *extra, Ft8ProtocolFieldKind kind, int8_t snr,
                             int64_t slot_id)
{
    Ft8ProtocolMessage typed={.type=FT8_PROTOCOL_STANDARD}, decoded;
    strcpy(typed.data.standard.call_to,to); strcpy(typed.data.standard.call_de,from);
    strcpy(typed.data.standard.extra,extra); typed.data.standard.extra_kind=kind;
    Ft8DecodedPayload payload; memset(&payload,0,sizeof(payload));
    assert(ft8_protocol_encode(&typed,payload.payload)==FT8_PROTOCOL_CODEC_OK);
    assert(ft8_protocol_decode(&payload,NULL,&decoded)==FT8_PROTOCOL_CODEC_OK);
    decoded.snr_db=snr; decoded.offset_hz=1647; /* engine-supplied measurements */
    assert(decoded.type==FT8_PROTOCOL_STANDARD && decoded.parse_status==FT8_PROTOCOL_PARSE_OK);
    assert(!decoded.has_unresolved_hash && decoded.data.standard.extra_kind==kind);
    assert(strcmp(decoded.data.standard.call_to,to)==0 && strcmp(decoded.data.standard.call_de,from)==0);
    assert(strcmp(decoded.data.standard.extra,extra)==0);
    Ft8ProtocolSlot slot; ft8_protocol_slot_init(&slot,slot_id,&decoded,1);
    slot.message_count=1; slot.status=FT8_PROTOCOL_SLOT_OK;
    assert(rx_result_builder_build(&app->rx->builder,&slot,app->rx->rx_messages,
                                    FT8_DECODER_CANDIDATE_CAPACITY,&app->rx->batch)==RX_RESULT_OK);
    rx_complete_batch(app->rx);
    RxMessage *rx=&app->rx->rx_messages[0];
    assert(rx->protocol_type==decoded.type && rx->parse_status==FT8_PROTOCOL_PARSE_OK && !rx->has_unresolved_hash);
    assert(strcmp(rx->call_to,to)==0 && strcmp(rx->call_de,from)==0 && strcmp(rx->extra,extra)==0);
    assert(rx->is_to_me==(strcmp(to,"AG6AQ")==0));
    if (strcmp(extra,"RR73")==0) {
        AutoSeqRxEvent event_rx;
        bool projected=addressed_rx_to_event(&app->rx->batch,rx,&event_rx);
        printf("RR73 boundary: protocol=%d parse=%d unresolved=%d to=%s de=%s extra=%s extra_kind=%d to_me=%d qso_kind=%u projected=%d event_kind=%u\n",
               rx->protocol_type,rx->parse_status,rx->has_unresolved_hash,rx->call_to,rx->call_de,
               rx->extra,kind,rx->is_to_me,rx->qso_kind,projected,projected ? event_rx.kind : 0);
        fflush(stdout);
    }
    assert(app_process_addressed_batch(app));
}

static void expected_tx(AppController *app, bool physical, const char *text, AutoSeqMessageKind kind)
{
    AutoSeqTxIntent intent; Ft8TxPlan plan;
    assert(auto_seq_prepare_tx_intent(&app->auto_seq,&intent));
    assert(ft8_tx_encode(&intent,&plan)==FT8_TX_ENCODE_OK);
    printf("next TX: %s (kind %u)\n",plan.canonical_text,intent.message_kind); fflush(stdout);
    assert(intent.message_kind==kind && strcmp(plan.canonical_text,text)==0);
    if (!physical) { assert(auto_seq_tick(&app->auto_seq,(int64_t)(now_us/1000))); return; }
    bool changed; assert(app_controller_step_tx(app,&changed) && app->tx.active);
    assert(app->tx.last_intent.message_kind==kind && strcmp(app->tx.plan.canonical_text,text)==0);
    assert(app->tx.plan.base_hz>=500 && app->tx.plan.base_hz<=2500);
    char rt[100]; snprintf(rt,sizeof(rt),"] %s %d\n",text,app->tx.plan.base_hz);
    assert(strstr(rt_contents(),rt));
    now_us+=12640000; assert(app_controller_step_tx(app,&changed) && changed && !app->tx.active);
}

static void responder_rr73(bool physical, Ft8ProtocolFieldKind terminal_kind)
{
    AppController app; setup(&app,true); app.config.offset_src=FT8_OFFSET_RANDOM;
    assert(auto_seq_drop_index(&app.auto_seq,0,0));
    int64_t rx_slot=(1789776000+1005)/15-1;
    decoded_standard(&app,"CQ","KF7SEY","CN84",FT8_PROTOCOL_FIELD_GRID,0,rx_slot);
    AppAction select={.type=APP_ACTION_SELECT_RX_MESSAGE,.value.index=0};
    assert(app_controller_apply_action(&app,&select));
    expected_tx(&app,physical,"KF7SEY AG6AQ CM97",AUTO_SEQ_MSG_TX1);
    bool changed;
    now_us=anchor+15000000;
    if (physical) assert(app_controller_step_tx(&app,&changed) && !app.tx.active);
    decoded_standard(&app,"AG6AQ","KF7SEY","+02",FT8_PROTOCOL_FIELD_REPORT,1,rx_slot+2);
    now_us=anchor+30000000;
    expected_tx(&app,physical,"KF7SEY AG6AQ R+00",AUTO_SEQ_MSG_TX3);
    now_us=anchor+45000000;
    if (physical) assert(app_controller_step_tx(&app,&changed) && !app.tx.active);
    decoded_standard(&app,"AG6AQ","KF7SEY","RR73",terminal_kind,2,rx_slot+4);
    AutoSeqTxIntent after_rr73; Ft8TxPlan after_plan;
    assert(auto_seq_prepare_tx_intent(&app.auto_seq,&after_rr73));
    assert(ft8_tx_encode(&after_rr73,&after_plan)==FT8_TX_ENCODE_OK);
    printf("after RR73: state=%d last_rx=%u next=%s\n",app.auto_seq.queue[0].state,
           app.auto_seq.queue[0].last_rx_kind,after_plan.canonical_text); fflush(stdout);
    assert(app.auto_seq.queue[0].last_rx_kind==AUTO_SEQ_MSG_TX4);
    assert(app.auto_seq.queue[0].state==AUTO_SEQ_STATE_SIGNOFF);
    /* Repeated terminal decode never returns the responder to TX3. */
    decoded_standard(&app,"AG6AQ","KF7SEY","RR73",terminal_kind,5,rx_slot+4);
    now_us=anchor+60000000;
    expected_tx(&app,physical,"KF7SEY AG6AQ 73",AUTO_SEQ_MSG_TX5);
    assert(auto_seq_active_count(&app.auto_seq)==0);
    cleanup(&app);
}

static void originator_signoff(void)
{
    AppController app; setup(&app,true); app.config.offset_src=FT8_OFFSET_RANDOM;
    expected_tx(&app,true,"CQ AG6AQ CM97",AUTO_SEQ_MSG_TX6);
    int64_t rx_slot=(1789776000+1005)/15+1;
    bool changed; now_us=anchor+15000000;
    assert(app_controller_step_tx(&app,&changed) && !app.tx.active);
    decoded_standard(&app,"AG6AQ","KF7SEY","CN84",FT8_PROTOCOL_FIELD_GRID,0,rx_slot);
    now_us=anchor+30000000;
    expected_tx(&app,true,"KF7SEY AG6AQ +00",AUTO_SEQ_MSG_TX2);
    now_us=anchor+45000000; assert(app_controller_step_tx(&app,&changed) && !app.tx.active);
    decoded_standard(&app,"AG6AQ","KF7SEY","R+02",FT8_PROTOCOL_FIELD_REPORT,1,rx_slot+2);
    now_us=anchor+60000000;
    expected_tx(&app,true,"KF7SEY AG6AQ RR73",AUTO_SEQ_MSG_TX4);
    decoded_standard(&app,"AG6AQ","KF7SEY","73",FT8_PROTOCOL_FIELD_TOKEN,2,rx_slot+4);
    assert(auto_seq_active_count(&app.auto_seq)==0); cleanup(&app);
}

static void nonstandard_rr73_metadata(void)
{
    /* Existing RX-1F nonstandard CQ vector, changed to directed RR73 with
     * AG6AQ's 12-bit destination hash. */
    Ft8DecodedPayload payload; memset(&payload,0,sizeof(payload));
    const uint8_t bytes[]={0x00,0x00,0x3E,0x4A,0x34,0xA8,0x6E,0xEB,0x85,0x20};
    memcpy(payload.payload,bytes,sizeof(bytes));
    uint32_t hash; assert(ft8_protocol_callsign_hash22("AG6AQ",&hash)==FT8_PROTOCOL_CODEC_OK);
    uint16_t hash12=(uint16_t)(hash>>10);
    payload.payload[0]=(uint8_t)(hash12>>4);
    payload.payload[1]=(uint8_t)((payload.payload[1]&0x0f)|((hash12&0x0f)<<4));
    Ft8HashStore store; ft8_hash_store_init(&store);
    assert(ft8_hash_store_save(&store,"AG6AQ",hash)==FT8_HASH_STORE_OK);
    AppController app; setup(&app,true);
    for (unsigned resolved=0;resolved<2;++resolved) {
        Ft8ProtocolMessage message;
        assert(ft8_protocol_decode(&payload,resolved ? &store : NULL,&message)==FT8_PROTOCOL_CODEC_OK);
        assert(message.type==FT8_PROTOCOL_NONSTD_CALL && message.parse_status==FT8_PROTOCOL_PARSE_OK);
        assert(message.has_unresolved_hash==!resolved && message.data.nonstandard.terminal==FT8_PROTOCOL_TERMINAL_RR73);
        assert(strcmp(message.data.nonstandard.call_to,resolved ? "<AG6AQ>" : "<...>")==0);
        assert(strcmp(message.data.nonstandard.call_de,"PJ4/KA1ABC")==0);
        Ft8ProtocolSlot slot; ft8_protocol_slot_init(&slot,1,&message,1);
        slot.message_count=1; slot.status=FT8_PROTOCOL_SLOT_OK;
        RxMessage rx; RxBatch batch;
        assert(rx_result_builder_build(&app.rx->builder,&slot,&rx,1,&batch)==RX_RESULT_OK);
        assert(rx.qso_kind==RX_QSO_MSG_TX4 && rx.is_to_me==!!resolved);
        assert(rx.protocol_type==message.type && rx.parse_status==message.parse_status && rx.has_unresolved_hash==!resolved);
        assert(rx.extra[0]==0); /* terminal is represented by qso_kind for this family */
        AutoSeqRxEvent event_rx; assert(addressed_rx_to_event(&batch,&rx,&event_rx)==!!resolved);
        if (resolved) {
            assert(event_rx.kind==AUTO_SEQ_MSG_TX4 && strcmp(event_rx.dxcall,"PJ4/KA1ABC")==0);
            rx.has_unresolved_hash=true; assert(!addressed_rx_to_event(&batch,&rx,&event_rx));
        }
    }
    cleanup(&app);
}

static void nonstandard_cq_reply(void)
{
    AppController app; setup(&app,true);
    assert(auto_seq_drop_index(&app.auto_seq,0,0));
    #include "ft8_tx_vectors/vectors.h"
    Ft8DecodedPayload payload={0}; Ft8ProtocolMessage decoded;
    memcpy(payload.payload,vectors[30].payload,sizeof(payload.payload));
    assert(ft8_protocol_decode(&payload,NULL,&decoded)==FT8_PROTOCOL_CODEC_OK);
    assert(decoded.type==FT8_PROTOCOL_NONSTD_CALL);
    assert(strcmp(decoded.canonical_text,"CQ W1AW/9")==0);
    decoded.offset_hz=1647; decoded.snr_db=-12;
    Ft8ProtocolSlot slot;
    ft8_protocol_slot_init(&slot,(1789776000+1005)/15-1,&decoded,1);
    slot.message_count=1; slot.status=FT8_PROTOCOL_SLOT_OK;
    assert(rx_result_builder_build(&app.rx->builder,&slot,app.rx->rx_messages,
           FT8_DECODER_CANDIDATE_CAPACITY,&app.rx->batch)==RX_RESULT_OK);
    rx_complete_batch(app.rx);
    AppAction select={.type=APP_ACTION_SELECT_RX_MESSAGE,.value.index=0};
    assert(app_controller_apply_action(&app,&select));
    AutoSeqTxIntent intent; Ft8TxPlan plan;
    assert(auto_seq_prepare_tx_intent(&app.auto_seq,&intent));
    assert(strcmp(intent.dxcall,"W1AW/9")==0 && intent.message_kind==AUTO_SEQ_MSG_TX1);
    int rc=ft8_tx_encode(&intent,&plan);
    bool changed; assert(app_controller_step_tx(&app,&changed));
    printf("W1AW/9 regression: encode=%d active=%d text=%s\n",rc,app.tx.active,plan.canonical_text);
    fflush(stdout);
    assert(rc==FT8_TX_ENCODE_OK && app.tx.active);
    assert(strcmp(plan.canonical_text,"W1AW/9 AG6AQ CM97")==0);
    assert(ft8_protocol_get_type(plan.payload)==FT8_PROTOCOL_STANDARD);
    assert(app.tx.plan.base_hz==1500 && strstr(cat,"MD6;TX;TA"));
    assert(strstr(rt_contents(),"] W1AW/9 AG6AQ CM97 1500\n"));
    uint64_t start=now_us;
    for (unsigned i=1;i<=79;++i) {
        now_us=start+i*160000u;
        assert(app_controller_step_tx(&app,&changed));
    }
    assert(!app.tx.active && app.tx.physical_tx_count==1);
    cleanup(&app);
}

static void rx_display_order(void)
{
    AppController app; setup(&app,true);
    assert(auto_seq_drop_index(&app.auto_seq,0,0));
    Ft8ProtocolMessage messages[6]={0};
    const char *to[]={"W9XYZ","CQ","AG6AQ","CQ","AG6AQ","W9XYZ"};
    const char *from[]={"W1AAA","W1BBB","W1CCC","W1DDD","W1EEE","W1FFF"};
    const int8_t snr[]={-5,-10,-18,2,-3,10};
    for (size_t i=0;i<6;++i) {
        Ft8ProtocolMessage typed={.type=FT8_PROTOCOL_STANDARD}; Ft8DecodedPayload payload={0};
        strcpy(typed.data.standard.call_to,to[i]); strcpy(typed.data.standard.call_de,from[i]);
        strcpy(typed.data.standard.extra,"FN42"); typed.data.standard.extra_kind=FT8_PROTOCOL_FIELD_GRID;
        assert(ft8_protocol_encode(&typed,payload.payload)==FT8_PROTOCOL_CODEC_OK);
        assert(ft8_protocol_decode(&payload,NULL,&messages[i])==FT8_PROTOCOL_CODEC_OK);
        messages[i].snr_db=snr[i]; messages[i].offset_hz=1500;
    }
    Ft8ProtocolSlot slot;
    ft8_protocol_slot_init(&slot,(1789776000+1005)/15-1,messages,6);
    slot.message_count=6; slot.status=FT8_PROTOCOL_SLOT_OK;
    assert(rx_result_builder_build(&app.rx->builder,&slot,app.rx->rx_messages,
           FT8_DECODER_CANDIDATE_CAPACITY,&app.rx->batch)==RX_RESULT_OK);
    RxMessage original[6]; memcpy(original,app.rx->batch.messages,sizeof(original));
    rx_complete_batch(app.rx);
    const size_t expected[]={4,2,3,1,5,0};
    UiModel model; app_controller_build_model(&app,&model);
    assert(model.rx_count==6);
    for (size_t i=0;i<6;++i) {
        assert(app.rx->display_order[i]==expected[i]);
        assert(strcmp(model.rx_lines[i],original[expected[i]].canonical_text)==0);
        const RxMessage *message=&original[expected[i]];
        assert(model.rx_kind[i]==(message->is_to_me?UI_RX_TO_ME:message->is_cq?UI_RX_CQ:UI_RX_NORMAL));
    }
    /* Projection preserves to-me precedence without changing classification. */
    bool cq=app.rx->batch.messages[expected[0]].is_cq;
    app.rx->batch.messages[expected[0]].is_cq=true;
    app_controller_build_model(&app,&model); assert(model.rx_kind[0]==UI_RX_TO_ME);
    app.rx->batch.messages[expected[0]].is_cq=cq;
    AutoSeq raw_seq=app.auto_seq;
    for (size_t i=0;i<6;++i) {
        AutoSeqRxEvent event_rx;
        if (addressed_rx_to_event(&app.rx->batch,&original[i],&event_rx))
            assert(auto_seq_on_addressed_rx(&raw_seq,&event_rx)!=AUTO_SEQ_ERR_INVALID);
    }
    unsigned closes_before=file_closes;
    assert(app_process_addressed_batch(&app));
    assert(memcmp(&raw_seq,&app.auto_seq,sizeof(raw_seq))==0);
    assert(file_closes==closes_before+6);
    const char *log=rt_contents(), *previous=log;
    for (size_t i=0;i<6;++i) {
        const char *entry=strstr(previous,original[i].canonical_text);
        assert(entry); previous=entry+strlen(original[i].canonical_text);
    }
    unsigned lines=0;
    for (const char *p=log;*p;++p) if (*p=='\n') ++lines;
    assert(lines==6);
    assert(app_process_addressed_batch(&app) && file_closes==closes_before+6);
    assert(memcmp(original,app.rx->batch.messages,sizeof(original))==0);
    AppAction select={.type=APP_ACTION_SELECT_RX_MESSAGE,.value.index=2};
    assert(app_controller_apply_action(&app,&select));
    assert(app.rx->selected_rx_index==3);
    bool found=false;
    for (size_t i=0;i<auto_seq_active_count(&app.auto_seq);++i)
        if (strcmp(app.auto_seq.queue[i].dxcall,"W1DDD")==0) found=true;
    assert(found);

    /* Required weak-A/strong-B example, replacing the completed batch. */
    while (auto_seq_active_count(&app.auto_seq)) assert(auto_seq_drop_index(&app.auto_seq,0,0));
    app.rx->rx_messages[0]=original[1]; app.rx->rx_messages[1]=original[3];
    app.rx->batch.message_count=2; rx_complete_batch(app.rx);
    assert(!app.rx->selected_rx_valid);
    app_controller_build_model(&app,&model);
    assert(model.rx_count==2 && strcmp(model.rx_lines[0],original[3].canonical_text)==0);
    select.value.index=0; assert(app_controller_apply_action(&app,&select));
    assert(app.rx->selected_rx_index==1 && strcmp(app.auto_seq.queue[0].dxcall,"W1DDD")==0);
    select.value.index=2; assert(!app_controller_apply_action(&app,&select));
    select.value.index=-1; assert(!app_controller_apply_action(&app,&select));
    ++app.rx->batch_generation; /* a mapping from another generation is unusable */
    select.value.index=0; assert(!app_controller_apply_action(&app,&select));
    app_controller_build_model(&app,&model); assert(model.rx_count==0);

    /* Complete 50-row projection preserves global indexes used by pagination. */
    for (size_t i=0;i<50;++i) {
        app.rx->rx_messages[i]=original[1];
        app.rx->rx_messages[i].snr_db=(int8_t)i;
        snprintf(app.rx->rx_messages[i].canonical_text,RX_RESULT_TEXT_CAP,"row %zu",i);
    }
    app.rx->batch.message_count=50; rx_complete_batch(app.rx);
    app_controller_build_model(&app,&model); assert(model.rx_count==50);
    for (size_t i=0;i<50;++i) {
        char text[32]; snprintf(text,sizeof(text),"row %zu",49-i);
        assert(strcmp(model.rx_lines[i],text)==0);
        assert(model.rx_kind[i]==UI_RX_CQ);
    }
    select.value.index=49; assert(app_controller_apply_action(&app,&select));
    assert(app.rx->selected_rx_index==0);
    UiModel preserved=model;
    uint64_t generation=app.rx->batch_generation;
    RxSlotFramerEvent reset_event={.type=RX_SLOT_FRAMER_EVENT_STREAM_RESET};
    assert(rx_emit_event(app.rx,&reset_event)==0);
    app_controller_build_model(&app,&model);
    assert(model.rx_count==50 && app.rx->selected_rx_valid);
    assert(memcmp(model.rx_lines,preserved.rx_lines,sizeof(model.rx_lines))==0);
    assert(memcmp(model.rx_kind,preserved.rx_kind,sizeof(model.rx_kind))==0);
    assert(app_controller_apply_action(&app,&select) && app.rx->selected_rx_index==0);
    assert(app.rx->batch_generation==generation && app.rx->display_generation==generation);
    /* A real replacement changes both order and selection lifetime exactly once. */
    app.rx->rx_messages[0]=original[3]; app.rx->rx_messages[1]=original[1];
    app.rx->batch.message_count=2; rx_complete_batch(app.rx);
    app_controller_build_model(&app,&model);
    assert(model.rx_count==2 && strcmp(model.rx_lines[0],original[3].canonical_text)==0);
    assert(app.rx->display_order[0]==0 && app.rx->display_order[1]==1);
    assert(app.rx->batch_generation==generation+1 && !app.rx->selected_rx_valid);
    app_controller_build_model(&app,&model); assert(app.rx->batch_generation==generation+1);
    app.rx->batch.message_count=0; rx_complete_batch(app.rx);
    app_controller_build_model(&app,&model); assert(model.rx_count==0);
    cleanup(&app);
}

static void rx_display_tx_lifetime(void)
{
    AppController app; setup(&app,true);
    for (size_t i=0;i<2;++i) {
        RxMessage *message=&app.rx->rx_messages[i];
        memset(message,0,sizeof(*message));
        message->is_cq=true; message->snr_db=i ? 2 : -10;
        strcpy(message->canonical_text,i ? "CQ W1BBB FN42" : "CQ W1AAA FN42");
    }
    app.rx->batch.messages=app.rx->rx_messages; app.rx->batch.message_count=2;
    rx_complete_batch(app.rx);
    UiModel before, during, after; app_controller_build_model(&app,&before);
    assert(before.rx_count==2 && strcmp(before.rx_lines[0],"CQ W1BBB FN42")==0);
    AppAction select={.type=APP_ACTION_SELECT_RX_MESSAGE,.value.index=0};
    assert(app_controller_apply_action(&app,&select));
    assert(app.rx->selected_rx_valid && app.rx->selected_rx_index==1);
    RxMessage original[2]; memcpy(original,app.rx->rx_messages,sizeof(original));
    uint64_t generation=app.rx->batch_generation;
    bool changed; assert(app_controller_step_tx(&app,&changed) && app.tx.active);
    app_controller_build_model(&app,&during);
    printf("RX lifetime: before=%zu during TX=%zu\n",before.rx_count,during.rx_count); fflush(stdout);
    assert(during.rx_count==before.rx_count);
    AutoSeq frozen=app.auto_seq;
    select.value.index=1; assert(app_controller_apply_action(&app,&select));
    assert(memcmp(&frozen,&app.auto_seq,sizeof(frozen))==0 && app.rx->selected_rx_index==1);
    uint64_t start=now_us;
    for (unsigned i=0;i<79;++i) {
        now_us=start+i*160000u; assert(app_controller_step_tx(&app,&changed) && app.tx.active);
        app_controller_build_model(&app,&during);
        assert(during.rx_count==2 && memcmp(before.rx_lines,during.rx_lines,sizeof(before.rx_lines))==0);
        assert(app.rx->display_count==2 && app.rx->display_generation==generation);
        assert(app.rx->batch_generation==generation && app.rx->selected_rx_valid);
    }
    now_us=start+79*160000u;
    assert(app_controller_step_tx(&app,&changed) && !app.tx.active && app.tx.physical_tx_count==1);
    app_controller_build_model(&app,&after);
    assert(after.rx_count==0 && app.rx->display_count==0 && !app.rx->selected_rx_valid);
    assert(app.rx->active && app.rx->batch_generation==generation && app.rx->batch.message_count==2);
    assert(memcmp(original,app.rx->rx_messages,sizeof(original))==0);
    assert(!app_controller_apply_action(&app,&select));
    cleanup(&app);
}

static void set_band(AppController *app, int band)
{
    AppAction action = {.type=APP_ACTION_SET_BAND, .value.index=band};
    assert(app_controller_apply_action(app, &action));
}

static void band_cat(void)
{
    AppController app; bool changed; UiModel model;
    setup(&app, true);
    AutoSeq saved = app.auto_seq;
    set_band(&app, 4);
    app_controller_build_model(&app, &model);
    assert(model.band_index == 4 && strcmp(model.band_name, "17m") == 0);
    assert(strstr(files[path_id("/flash/ft8/station.txt")].text, "band=4"));
    now_us += 999000;
    assert(app_controller_step_cat(&app) && !serial_writes);
    now_us += 1000;
    assert(app_controller_step_cat(&app));
    assert(strcmp(cat, "MD6;FR0;FT0;FA00018100000;") == 0 && serial_writes == 4);
    assert(app_controller_step_cat(&app) && serial_writes == 4);
    assert(memcmp(&saved, &app.auto_seq, sizeof(saved)) == 0);
    assert(starts == 1 && stops == 0 && !serial_closes);
    cleanup(&app);

    setup(&app, false);
    set_band(&app, 3);
    for (int band = 4; band <= 6; ++band) {
        now_us += 700000; set_band(&app, band);
        assert(app_controller_step_cat(&app) && serial_writes == 0);
    }
    now_us += 999000; assert(app_controller_step_cat(&app) && !serial_writes);
    now_us += 1000; assert(app_controller_step_cat(&app));
    assert(strcmp(cat, "MD6;FR0;FT0;FA00028074000;") == 0 && serial_writes == 4);
    cleanup(&app);

    reset(); assert(app_controller_init(&app, &api, "/flash/ft8", "/flash/ft8/station.txt"));
    set_band(&app, 5); now_us += 2000000;
    assert(app_controller_step_cat(&app) && !serial_writes && !app.cat_band_sync_pending);
    assert(app_controller_start_cat(&app, "test:cat") == MINI_OK);
    assert(strcmp(cat, "MD6;FR0;FT0;FA00021074000;") == 0 && serial_writes == 4);
    cleanup(&app);

    for (unsigned short_failure = 0; short_failure < 2; ++short_failure) {
        setup(&app, false); saved = app.auto_seq;
        set_band(&app, 4); now_us += 1000000;
        fail_command = 4; short_cat = short_failure;
        assert(!app_controller_step_cat(&app));
        assert(strchr(events, '!') && app.cat_band_sync_pending);
        assert(!app_controller_step_cat(&app) && serial_writes == 4); // No auto-retry.
        now_us = anchor + 30000000;
        assert(app_controller_step_tx(&app, &changed) && !app.tx.active);
        assert(!strstr(cat, "TX;") && !strstr(cat, "RX;"));
        assert(memcmp(&saved, &app.auto_seq, sizeof(saved)) == 0 && !app.tx.failed_tx_count);
        cleanup(&app);
    }

    // Deadline in the first 500 ms of a TX slot: no catch-up, even if no
    // pending-TX step ran before the CAT step first noticed the boundary.
    for (unsigned observe_pending = 0; observe_pending < 2; ++observe_pending) {
        setup(&app, false); saved = app.auto_seq;
        now_us = anchor - 800000; set_band(&app, 4);
        now_us = anchor;
        if (observe_pending) assert(app_controller_step_tx(&app, &changed) && !app.tx.active);
        now_us = anchor + 200000; assert(app_controller_step_cat(&app));
        assert(app_controller_step_tx(&app, &changed) && !app.tx.active);
        assert(memcmp(&saved, &app.auto_seq, sizeof(saved)) == 0);
        now_us = anchor + 15000000; assert(app_controller_step_tx(&app, &changed) && !app.tx.active);
        now_us = anchor + 30000000; assert(app_controller_step_tx(&app, &changed) && app.tx.active);
        cleanup(&app);
    }

    // A blocking sync itself crosses the boundary; consume that opportunity too.
    setup(&app, false);
    now_us = anchor - 1100000; set_band(&app, 4);
    now_us = anchor - 100000; write_delay_us = 50000;
    assert(app_controller_step_cat(&app));
    assert(now_us == anchor + 100000);
    assert(app_controller_step_tx(&app, &changed) && !app.tx.active);
    cleanup(&app);

    // A boundary retained waiting for RX freshness must not survive the edit.
    setup(&app, true); app.rx->have_applied_batch = false;
    assert(app_controller_step_tx(&app, &changed) && app.tx.pending);
    set_band(&app, 4); assert(!app.tx.pending);
    now_us += 1000000; assert(app_controller_step_cat(&app));
    app.rx->have_applied_batch = true;
    assert(app_controller_step_tx(&app, &changed) && !app.tx.active);
    cleanup(&app);

    setup(&app, false);
    assert(app_controller_step_tx(&app, &changed) && app.tx.active);
    unsigned before = serial_writes;
    set_band(&app, 4);
    assert(app.config.band_index == 3 && !app.cat_band_sync_pending);
    assert(app_controller_step_cat(&app) && serial_writes == before);
    cleanup(&app);

    // Missing clock at action time, or a clock lost before expiry, cannot
    // create an immortal debounce. Both use a receive-safe immediate sync.
    for (unsigned lost = 0; lost < 2; ++lost) {
        setup(&app, false);
        mini_time_location_api_t no_clock = clock_api; no_clock.monotonic_us = NULL;
        mini_api_t no_clock_api = api; no_clock_api.time_location = &no_clock;
        if (!lost) app.api = &no_clock_api;
        set_band(&app, 4);
        app.api = &no_clock_api;
        assert(app_controller_step_cat(&app) && serial_writes == 4 && !app.cat_band_sync_pending);
        cleanup(&app);
    }
    puts("band CAT debounce, final selection, failure inhibit, slot consumption and TX freeze PASS");
}

static uint64_t snapshot_hash(const void *bytes, size_t count)
{
    uint64_t hash = 1469598103934665603ull;
    const unsigned char *p = bytes;
    while (count--) hash = (hash ^ *p++) * 1099511628211ull;
    return hash;
}
static mini_result_t qso_read_error(mini_file_t f, void *b, uint32_t n, uint32_t *out)
{ (void)f; (void)b; (void)n; *out=0; return MINI_ERR_IO; }
static void qso_view(void)
{
    AppController app; setup(&app, true);
    LogStationFacts station = {.callsign="AG6AQ", .effective_grid="CM97", .fd_exchange="", .band_index=3};
    LogQsoFacts qso = {.dxcall="W1AW/9", .dxgrid="", .fd_rx_exchange=""};
    assert(log_service_write_adif(&app.log, &station, &qso));
    AutoSeq seq = app.auto_seq; AppTxState tx = app.tx; RadioControl radio = app.radio;
    ConfigService config = app.config;
    uint64_t rx_hash = snapshot_hash(app.rx, sizeof(*app.rx));
    AppAction view = {.type=APP_ACTION_LOAD_QSO_PAGE, .value.page_index=0};
    assert(app_controller_apply_action(&app, &view));
    UiModel model; app_controller_build_model(&app, &model);
    assert(model.qso.total_count == 1 && strcmp(model.qso.rows[0].call, "W1AW/9") == 0);
    assert(memcmp(&seq, &app.auto_seq, sizeof(seq)) == 0);
    assert(memcmp(&tx, &app.tx, sizeof(tx)) == 0 && memcmp(&radio, &app.radio, sizeof(radio)) == 0);
    assert(memcmp(&config, &app.config, sizeof(config)) == 0 && snapshot_hash(app.rx, sizeof(*app.rx)) == rx_hash);
    unsigned before = file_closes;
    for (unsigned i=0; i<5; ++i) { app_controller_step_qso(&app); app_controller_build_model(&app, &model); }
    assert(file_closes == before); // Neither rendering nor clean progression rescans.
    qso.dxcall = "K1ABC";
    assert(log_service_write_adif(&app.log, &station, &qso));
    assert(app_controller_apply_action(&app, &view)); // Re-entry refreshes disk data.
    assert(app.qso.total_count == 2 && strcmp(app.qso.rows[1].call, "K1ABC") == 0);
    mini_fs_api_t broken = fs; broken.read = qso_read_error;
    app.log.fs = &broken;
    assert(app_controller_apply_action(&app, &view));
    assert(app.qso.status == LOG_QSO_VIEW_READ_ERROR && app_controller_rx_active(&app));
    assert(snapshot_hash(app.rx, sizeof(*app.rx)) == rx_hash && memcmp(&seq, &app.auto_seq, sizeof(seq)) == 0);
    app.log.fs = &fs;
    before = file_closes;
    app.tx.active = true; // Requests are queued; disk scans cannot interrupt symbol timing.
    assert(app_controller_apply_action(&app, &view) && app.qso_dirty && file_closes == before);
    app_controller_step_qso(&app); assert(file_closes == before);
    app.tx.active = false; app_controller_step_qso(&app); assert(!app.qso_dirty && app.qso.total_count == 2);
    cleanup(&app);
    // Recreate controller without clearing persisted fake files: earlier-launch QSOs survive.
    assert(app_controller_init(&app, &api, "/flash/ft8", "/flash/ft8/station.txt"));
    assert(app_controller_apply_action(&app, &view) && app.qso.total_count == 2);
    now_us += 86400000000ull;
    assert(app_controller_apply_action(&app, &view) && !app.qso.total_count && app.qso.status == LOG_QSO_VIEW_OK);
    assert(log_service_write_adif(&app.log, &station, &qso));
    assert(app_controller_apply_action(&app, &view) && app.qso.total_count == 1);
    assert(files[path_id("/flash/ft8/20260920.txt")].exists);
    cleanup(&app);
    puts("QSO daily snapshot, refresh, day rollover, persistence and read-only ownership PASS");
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--qso") == 0) { qso_view(); qso_completion(); return 0; }
    if (argc == 2 && strcmp(argv[1], "--band-cat") == 0) { band_cat(); return 0; }
    rx_display_tx_lifetime();
    rx_display_order();
    nonstandard_cq_reply();
    responder_rr73(false,FT8_PROTOCOL_FIELD_TOKEN);
    responder_rr73(false,FT8_PROTOCOL_FIELD_GRID);
    responder_rr73(true,FT8_PROTOCOL_FIELD_GRID);
    responder_rr73(true,FT8_PROTOCOL_FIELD_TOKEN);
    originator_signoff();
    nonstandard_rr73_metadata();
    success(1,0,0); success(5,20,3000); success(10,499,0); success(20,33,0);
    failures(); freshness_and_stalls(); qso_completion(); rt_logging(); cq_beacon_settings(); offset_integration();
    puts("physical FT8: absolute timing, exact CAT, station identity, completion, failure cleanup, RX recovery and RT logging PASS");
    return 0;
}
