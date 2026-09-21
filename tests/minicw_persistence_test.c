/* Reuse the real MiniShell runtime fixture. Include coordinator/private port for
 * direct quiet-state and fault injection, without adding production test APIs. */
#define main accepted_runtime_scenarios
#include "minicw_runtime_test.c"
#undef main
#include "../apps/minicw/src/port/minicw_port.c"
#include "../apps/minicw/src/app_core/app_core.c"

static void bind(void)
{
    reset(); s_api = &api; s_error = MINI_OK; s_exit = false; s_have_frame = false;
    for (unsigned i = 0; i < 4; ++i) { live[i] = true; s_lines[i] = i + 1; levels[i] = 1; }
}
static void unbind(void)
{
    for (unsigned i = 0; i < 4; ++i) { live[i] = false; s_lines[i] = 0; }
    s_api = NULL;
}
static void fixture_text(const char *text)
{
    strcpy(fs_destination, text); fs_exists = true;
}
static void storage_cases(void)
{
    bind(); storage_snapshot_t s, defaults, round;
    storage_defaults(&defaults);
    assert(storage_load(&s) == STORAGE_MISSING && storage_equal(&s, &defaults));
    assert(!fs_writes && !fs_attempts);
    const char *valid = "[system]\nvolume=99\ntone_hz=999\nkey_in=SK-R\nkey_in_wpm=60\n"
        "[keyer]\nkey_out=SK-M\npaddle=Bug\nsk_wpm=5\ntx_delay_s=99\ntune_timeout_s=0\nrepeat_interval_s=99\n"
        "mycall=A1/B\nm1= A = B \nm2=HELLO WORLD\nm3===\nm4=\nm5=LAST\n";
    fixture_text(valid);
    assert(storage_load(&s) == STORAGE_OK && fs_reads > 1);
    assert(s.volume == 99 && s.tone_hz == 999 && s.key_in == KEYER_KEY_IN_SK_R && s.key_in_wpm == 60);
    assert(s.keyer.key_out_mode == KEYER_KEY_OUT_SK_M && s.keyer.paddle_mode == KEYER_PADDLE_BUG && s.keyer.sk_wpm == 5);
    assert(s.keyer.tx_delay_s == 99 && s.keyer.tune_timeout_s == 0 && s.keyer.repeat_interval_s == 99);
    assert(!strcmp(s.keyer.mycall, "A1/B") && !strcmp(s.keyer.message[0], " A = B "));
    assert(!strcmp(s.keyer.message[1], "HELLO WORLD") && !strcmp(s.keyer.message[2], "=="));
    assert(!s.keyer.message[3][0] && !strcmp(s.keyer.message[4], "LAST"));
    fs_temp_exists = true; strcpy(fs_temporary, "STALE");
    assert(storage_save(&s) && fs_writes > 1 && fs_removes == 1 && !fs_temp_exists && !fs_live);
    assert(storage_load(&round) == STORAGE_OK && storage_equal(&s, &round));
    assert(!strstr(fs_destination,"mute=") && !strstr(fs_destination,"gps") && !strstr(fs_destination,"date="));
    fixture_text("[unknown]\nvolume=BAD\n[system]\nother=BAD\nvolume=0\ntone_hz=300\nkey_in_wpm=5\n[keyer]\nsk_wpm=60\ntx_delay_s=0\ntune_timeout_s=20\nrepeat_interval_s=1\n");
    assert(storage_load(&round) == STORAGE_OK && round.volume == 0 && round.tone_hz == 300 && round.key_in_wpm == 5);
    assert(round.keyer.sk_wpm == 60 && round.keyer.tune_timeout_s == 20 && round.keyer.repeat_interval_s == 1);
    assert(!strcmp(round.keyer.message[0], "CQ POTA"));
    const char *bad[] = {"[system]\nvolume=100", "[system]\ntone_hz=299", "[system]\ntone_hz=1000",
        "[system]\nkey_in_wpm=4", "[system]\nkey_in_wpm=61", "[system]\nvolume=-1", "[system]\nvolume=1x",
        "[system]\nvolume=999999999999999", "[system]\nkey_in=bad", "[keyer]\nkey_out=SKS", "[keyer]\npaddle=bad",
        "[keyer]\nsk_wpm=61", "[keyer]\ntx_delay_s=100", "[keyer]\ntune_timeout_s=21", "[keyer]\nrepeat_interval_s=0",
        "[keyer]\nmycall=lower", "[keyer]\nmycall=ABC!", "[keyer]\nmycall=ABCDEFGHIJKLM", "[keyer]\nm1=bad\ttext",
        "[keyer]\nm1=bad\177text"};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        char text[512]; snprintf(text, sizeof(text), "[system]\nvolume=42\n%s", bad[i]); fixture_text(text);
        assert(storage_load(&round) == STORAGE_INVALID && storage_equal(&round, &defaults));
    }
    char large[8192]; memset(large, 'X', sizeof(large)); large[sizeof(large)-1] = 0;
    fixture_text(large); assert(storage_load(&round) == STORAGE_INVALID);
    char boundary[180]; strcpy(boundary, "[keyer]\nm1=");
    size_t start = strlen(boundary); memset(boundary+start, 'A', KEYER_MESSAGE_MAX_LEN); boundary[start+KEYER_MESSAGE_MAX_LEN] = 0;
    assert(storage_parse(boundary, &round)); boundary[start+KEYER_MESSAGE_MAX_LEN] = 'A'; boundary[start+KEYER_MESSAGE_MAX_LEN+1] = 0;
    assert(!storage_parse(boundary, &round));
    fixture_text(valid); fs_fail="nul"; assert(storage_load(&round)==STORAGE_INVALID);
    const char *read_fail[] = {"open_read", "read", "read_late", "close_read"};
    for (unsigned i=0;i<4;++i) { fs_reads=0; fixture_text(valid); fs_fail=read_fail[i]; assert(storage_load(&round)==STORAGE_READ_FAILED && storage_equal(&round,&defaults) && !fs_live); }
    const char *save_fail[] = {"mkdir", "open_write", "write", "write_late", "zero_write", "sync", "close_write", "rename"};
    for (unsigned i=0;i<sizeof(save_fail)/sizeof(save_fail[0]);++i) {
        fs_writes=0; fs_fail=save_fail[i]; fixture_text("PREVIOUS");
        assert(!storage_save(&s) && !strcmp(fs_destination,"PREVIOUS") && !fs_live && !fs_temp_exists);
    }
    fs_fail="remove"; assert(storage_save(&s)); /* stale cleanup is best effort; TRUNC succeeds */
    fs_fail=NULL; unbind();
}
static void observe_change(void)
{
    keyer_service_set_key_in_wpm(24);
    assert(keyer_service_get_key_in_wpm()==24);
    app_core_persistence_update(); assert(s_settings_dirty && !fs_writes);
}
static void tick_quiet(unsigned ms)
{
    now_us += ms * 1000U; app_core_persistence_update();
}
static void coordinator_cases(void)
{
    for (unsigned blocker=0;blocker<8;++blocker) {
        bind(); app_core_init(); observe_change();
        switch (blocker) {
        case 0: s_keyer.tx_pending=true; break;
        case 1: assert(keyer_service_tx_append_text("EEEE",false)); keyer_service_tx_start(); break;
        case 2: assert(keyer_service_tx_append_text("E",false)); break;
        case 3: s_keyer.m1_repeat_active=true; s_keyer.m1_repeat_waiting=true; break;
        case 4: app_core_keyer_set_tune_active(true); break;
        case 5: audio_service_tone_on(); break;
        case 6: input_tip=0; keyer_service_set_mute(true); break;
        case 7: input_ring=0; keyer_service_set_mute(true); break;
        }
        tick_quiet(2000); assert(!fs_writes);
        keyer_service_set_key_in_wpm(29); tick_quiet(1000); assert(!fs_writes);
        s_keyer.tx_pending=false; app_core_keyer_cancel_repeat(); keyer_service_tx_clear();
        app_core_keyer_set_tune_active(false); audio_service_stop_all(); input_tip=input_ring=1;
        tick_quiet(249); assert(!fs_writes);
        tick_quiet(1); assert(fs_commits==1 && !s_settings_dirty);
        assert(strstr(fs_destination,"key_in_wpm=29\n")); tick_quiet(500); assert(fs_commits==1);
        app_core_shutdown(); unbind();
    }
    bind(); app_core_init();
    ui_input_event_t edit = {.value=42}; app_core_handle_volume_changed(&edit);
    edit.value=880; app_core_handle_tone_changed(&edit);
    assert(audio_service_get_volume()==42 && audio_service_get_tone_hz()==880);
    app_core_persistence_update(); assert(s_settings_dirty && !fs_writes);
    tick_quiet(40); assert(!fs_writes); tick_quiet(10); assert(!fs_writes);
    tick_quiet(250); assert(fs_commits==1 && strstr(fs_destination,"volume=42") && strstr(fs_destination,"tone_hz=880"));
    keyer_service_set_mute(true); tick_quiet(1000); assert(fs_commits==1); /* mute is session-only */
    app_core_shutdown(); unbind();
    bind(); app_core_init(); observe_change(); fs_fail="rename"; tick_quiet(250);
    assert(s_settings_dirty && s_save_failed && fs_attempts==1 && strstr(frame[6],"Save failed"));
    tick_quiet(10000); assert(fs_attempts==1);
    keyer_service_set_key_in_wpm(30); tick_quiet(10); tick_quiet(250); assert(fs_attempts==2);
    app_core_shutdown(); app_core_save_on_exit(); assert(fs_attempts==3 && levels[2]==1 && levels[3]==1);
    unbind();
    /* Neither invalid input nor an I/O failure silently rewrites the file. */
    for (unsigned i=0;i<2;++i) {
        bind(); fixture_text("[system]\nvolume=bad"); fs_fail=i ? "read" : NULL;
        app_core_init();
        assert(strstr(frame[6], i ? "Settings read failed" : "Settings invalid"));
        tick_quiet(1000); app_core_shutdown(); app_core_save_on_exit();
        assert(!fs_attempts && !strcmp(fs_destination,"[system]\nvolume=bad")); unbind();
    }
}
static unsigned tone_opens, tone_closes;
static mini_audio_tone_config_t opened, configured;
static mini_result_t observed_open(const mini_audio_tone_config_t *c,mini_audio_tone_t *out)
{
    assert(!fs_live && fs_closes==1 && fs_reads>0); ++tone_opens; opened=*c; *out=1; return MINI_OK;
}
static mini_result_t observed_config(mini_audio_tone_t h,const mini_audio_tone_config_t *c) { assert(h==1); configured=*c; return MINI_OK; }
static mini_result_t observed_close(mini_audio_tone_t h) { assert(h==1); ++tone_closes; return MINI_OK; }
static mini_result_t observed_stop(mini_audio_tone_t h) { assert(h==1); return MINI_OK; }
static mini_result_t observed_enqueue(mini_audio_tone_t h,uint32_t n) { (void)n; return observed_stop(h); }
static mini_result_t observed_busy(mini_audio_tone_t h,uint32_t *out) { *out=0; return observed_stop(h); }
static mini_result_t exit_input(mini_key_event_t *key,uint32_t timeout)
{
    (void)timeout;
    assert(tone_opens==1 && opened.volume==80 && opened.pitch_hz==700);
    assert(configured.volume==35 && configured.pitch_hz==850);
    assert(audio_service_get_volume()==35 && audio_service_get_tone_hz()==850);
    keyer_service_set_key_out_mode(KEYER_KEY_OUT_SK_M);
    keyer_service_set_key_in_wpm(27);
    return ch(key,3);
}
static void save_after_tone_close(void) { assert(tone_closes==1); }
static void exit_cases(void)
{
    mini_audio_tone_api_t tone={sizeof(tone),observed_open,observed_config,observed_enqueue,observed_enqueue,observed_stop,observed_busy,observed_close};
    mini_audio_api_t audio={.struct_size=sizeof(audio),.capabilities=MINI_AUDIO_CAP_TONE,.tone=&tone};
    mini_key_input_api_t keys={.struct_size=sizeof(keys),.read=exit_input};
    mini_input_api_t in=input_api; in.key=&keys;
    for (unsigned i=0;i<2;++i) {
        reset(); fixture_text("[system]\nvolume=35\ntone_hz=850\n"); fs_fail=i ? "rename" : NULL;
        tone_opens=tone_closes=0; api.audio=&audio; api.input=&in; fs_before_save=save_after_tone_close;
        assert(minicw_run(&api)==0 && tone_closes==1 && closes==4 && levels[2]==1 && levels[3]==1);
        assert(fs_attempts==1 && fs_commits==(i ? 0U : 1U));
        if (!i) assert(strstr(fs_destination,"key_out=SK-M") && strstr(fs_destination,"key_in_wpm=27"));
    }
    api.audio=NULL; api.input=&input_api;
}
int main(void)
{
    storage_cases(); coordinator_cases(); exit_cases();
    puts("Mini-CW persistence: PASS (transaction faults, roundtrip, startup ordering, all quiet guards, retry, cleanup)");
    return 0;
}
