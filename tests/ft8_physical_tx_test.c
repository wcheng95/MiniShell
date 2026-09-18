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
static const char *station_text = "callsign=AG6AQ\ngrid=CM97\ncq_type=0\nband=3\nfd_exchange=1B SCV\n";
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
    bool changed; assert(app_controller_step_tx(&app,&changed));
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
    assert(auto_seq_active_count(&app.auto_seq)==0 && app_controller_rx_active(&app));
    assert(strstr(events,"Ea") && starts==2 && stops==1);
    assert(app.rx->timing_pending);
    assert(app_controller_step_rx(&app,&changed) && reads==1);
    assert(!app.rx->timing_pending && app.rx->framer.waiting_for_full_boundary);
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
        if(which==8) strcpy(app.auto_seq.config.callsign,"INVALID");
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
    now_us=anchor+499000;
    assert(app_controller_step_tx(&app,&changed) && !app.tx.active);
    app.rx->have_applied_batch=true;
    assert(app_controller_step_tx(&app,&changed) && app.tx.active && app.tx.schedule.next_tone==4);
    unsigned before=tone_count; now_us=anchor+3000000;
    assert(app_controller_step_tx(&app,&changed) && tone_count<=before+1 && app.tx.schedule.next_tone==19);
    assert(app_controller_step_tx(&app,&changed) && tone_count<=before+1);
    now_us=anchor+2900000; assert(app_controller_step_tx(&app,&changed) && !app.tx.active && app.tx.failed_tx_count==1);
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

int main(void)
{
    success(1,0,0); success(5,20,3000); success(10,499,0); success(20,33,0);
    failures(); freshness_and_stalls(); qso_completion(); rt_logging(); cq_beacon_settings();
    puts("physical FT8: absolute timing, exact CAT, station identity, completion, failure cleanup, RX recovery and RT logging PASS");
    return 0;
}
