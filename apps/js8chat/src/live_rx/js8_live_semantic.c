#include "js8_live.h"
#include <stdio.h>
#include <string.h>

static int dictionary_read(void *context, uint32_t offset, void *dst, size_t bytes)
{
    Js8Live *s = context; uint64_t position; uint32_t got = 0;
    return s->api->fs->seek(s->dictionary_file, offset, MINI_FS_SEEK_SET, &position) ||
           position != offset || s->api->fs->read(s->dictionary_file, dst, (uint32_t)bytes, &got) ||
           got != bytes ? -1 : 0;
}
static int dictionary_open(Js8Live *s)
{
    if (!s->dictionary_attempted) {
        s->dictionary_attempted = 1;
        const mini_fs_api_t *fs = s->api->fs;
        mini_fs_stat_t stat = {.struct_size = sizeof(stat)};
        if (fs && fs->stat && fs->open && fs->read && fs->seek && fs->close &&
            !fs->stat("/flash/js8chat/jsc.dict", &stat) && stat.size == 1918009 &&
            !fs->open("/flash/js8chat/jsc.dict", MINI_FS_READ, &s->dictionary_file) && s->dictionary_file &&
            !js8_jsc_dictionary_init(dictionary_read, s, 1918009, &s->dictionary)) s->dictionary_ready = 1;
    }
    if (!s->dictionary_ready) s->error = "dictionary";
    return s->dictionary_ready ? 0 : -1;
}
static int fs_write(void *context, const char *bytes, size_t length)
{
    Js8Live *s = context; uint32_t written = 0;
    return s->api->fs->write(s->log_file, bytes, (uint32_t)length, &written) || written != length ? -1 : 0;
}
int js8_live_emit(Js8Live *s, const Js8Activity *event)
{
    const Js8ActivityFields *f = &event->fields;
    char utc[21], line[1400];
    if (js8_log_format_utc(UINT64_C(62135596800)+(uint64_t)f->slot_index*15, utc)) return -1;
    const char *name = f->kind == JS8_ACTIVITY_MESSAGE ? "MESSAGE" : f->kind == JS8_ACTIVITY_DIRECTED ? "DIRECTED" :
                       f->kind == JS8_ACTIVITY_HEARTBEAT || f->kind == JS8_ACTIVITY_CQ ? f->beacon :
                       f->kind == JS8_ACTIVITY_COMPOUND ? "COMPOUND" : "DATA";
    /* Console is bounded summary text; full byte-exact text uses shared JSON. */
    snprintf(line, sizeof(line), "%s audio_millihz=%ld %s %s%s%s %s %.*s\n", utc,
             (long)f->frequency_millihz, name, f->call, f->from, f->to[0] ? " ->" : "",
             f->to[0] ? f->to : f->grid, (int)event->text_len, event->text);
    if (s->api->console && s->api->console->write) s->api->console->write(line);
    else if (s->api->system && s->api->system->write) s->api->system->write(line);
    if (s->log_file) {
        const char *error = js8_activity_json(&s->metadata, event, fs_write, s);
        if (error) { s->error = error; return -1; }
    }
    return 0;
}
int js8_live_payload(Js8Live *s, uint32_t slot, const Js8DecodedPayload *payload)
{
    Js8ProtocolEnvelope envelope;
    if (js8_protocol_envelope_decode(payload->payload_bits, &envelope)) return -1;
    Js8ActivityFields f = {0};
    f.slot_index = slot; f.tx_flags = envelope.tx_flags;
    f.frequency_millihz = (int32_t)s->monitor.req.min_bin*6250 + payload->candidate.freq_offset*6250 +
                         payload->candidate.freq_sub*(6250/(int32_t)s->monitor.config.freq_osr);
    f.score = payload->candidate.score; f.hard_errors = payload->ldpc_errors;
    Js8RxFragment fragment = {0};
    fragment.slot_index = slot; fragment.frequency_millihz = f.frequency_millihz; fragment.tx_flags = f.tx_flags;
    Js8Activity activity;
    Js8HuffmanData huff; Js8JscData jsc;
    const char *text = NULL; size_t length = 0;
    int feed = 0;
    switch (envelope.app_class) {
    case JS8_APP_FRAME_HEARTBEAT: {
        Js8BeaconFrame b;
        if (js8_beacon_decode(payload->payload_bits, &b)) return -1;
        f.kind = b.is_cq ? JS8_ACTIVITY_CQ : JS8_ACTIVITY_HEARTBEAT;
        memcpy(f.call, b.callsign, sizeof(f.call)); memcpy(f.grid, b.grid, sizeof(f.grid));
        strcpy(f.beacon, js8_beacon_name(b.is_cq, b.subtype)); f.subtype = b.subtype;
        break;
    }
    case JS8_APP_FRAME_COMPOUND: {
        Js8CompoundIdentity c;
        if (js8_compound_identity_decode(payload->payload_bits, &c)) return -1;
        f.kind = JS8_ACTIVITY_COMPOUND;
        memcpy(f.call, c.callsign, sizeof(f.call)); memcpy(f.grid, c.grid, sizeof(f.grid));
        f.extra = c.extra16; f.bits3 = c.bits3; break;
    }
    case JS8_APP_FRAME_COMPOUND_DIRECTED: {
        Js8CompoundFields c;
        if (js8_compound_fields_decode(payload->payload_bits, &c)) return -1;
        f.kind = JS8_ACTIVITY_COMPOUND; f.compound_directed = 1;
        memcpy(f.call, c.callsign, sizeof(f.call)); f.extra = c.extra16; f.bits3 = c.bits3; break;
    }
    case JS8_APP_FRAME_DIRECTED: {
        Js8DirectedFrame d;
        if (js8_directed_decode(payload->payload_bits, &d)) return -1;
        f.kind = JS8_ACTIVITY_DIRECTED; f.command_code = d.command_code;
        strcpy(f.command, js8_directed_command_name(d.command_code));
        memcpy(f.from, d.from, sizeof(f.from)); memcpy(f.to, d.to, sizeof(f.to));
        f.has_number = (uint8_t)d.has_number; f.number = d.number;
        f.free_text = (uint8_t)d.is_free_text; f.ack = (uint8_t)d.is_ack; f.end73 = (uint8_t)d.is_73;
        fragment.kind = JS8_RX_FRAGMENT_DIRECTED; fragment.command_code = d.command_code;
        memcpy(fragment.from, d.from, sizeof(d.from)); memcpy(fragment.to, d.to, sizeof(d.to)); feed = 1; break;
    }
    case JS8_APP_FRAME_DATA: {
        Js8HuffmanStatus result = js8_huffman_data_decode(payload->payload_bits, &huff);
        if (result == JS8_HUFF_BAD_PADDING) return 0;
        if (result != JS8_HUFF_OK) return -1;
        f.kind = JS8_ACTIVITY_DATA; f.codec = JS8_ACTIVITY_CODEC_HUFFMAN;
        text = huff.text; length = huff.text_len; feed = 1; break;
    }
    case JS8_APP_FRAME_DATA_COMPRESSED:
        if (dictionary_open(s)) return -1;
        if (js8_jsc_data_decode(payload->payload_bits, &s->dictionary, &jsc)) { s->error = "jsc_decode"; return -1; }
        f.kind = JS8_ACTIVITY_DATA; f.codec = JS8_ACTIVITY_CODEC_JSC;
        text = jsc.text; length = jsc.text_len; feed = 1; break;
    default: return -1;
    }
    if (js8_activity_build(&f, text, length, &activity) || js8_live_emit(s, &activity)) return -1;
    if (feed) {
        if (f.kind == JS8_ACTIVITY_DATA) {
            fragment.kind = JS8_RX_FRAGMENT_DATA; fragment.text = text; fragment.text_len = (uint16_t)length;
        }
        Js8RxMessage message; Js8RxDrops drops;
        Js8RxStatus status = js8_rx_reassembly_feed(&s->reassembly, &fragment, &message, &drops);
        if (status == JS8_RX_COMPLETE) {
            f.kind = JS8_ACTIVITY_MESSAGE; f.first_slot = message.first_slot; f.last_slot = message.last_slot;
            memcpy(f.from, message.from, sizeof(f.from)); memcpy(f.to, message.to, sizeof(f.to));
            if (js8_activity_build(&f, message.text, message.text_len, &activity) || js8_live_emit(s, &activity)) return -1;
        } else if (status == JS8_RX_INVALID) return -1;
    }
    return 0;
}
