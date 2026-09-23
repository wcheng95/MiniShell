#include "js8_decoder.h"
#include "js8_frame.h"
#include "js8_protocol_frame.h"
#include "js8_compound.h"
#include "js8_directed.h"
#include "js8_huffman.h"
#include "js8_jsc.h"
#include "js8_reassembly.h"
#include "js8_activity_log.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FILE *file;
    uint32_t samples;
    uint32_t total_samples;
    uint64_t data_offset;
} HostWav;

#define HOST_SLOT_INPUT_SAMPLES 180000u
#define HOST_WINDOW_INPUT_SAMPLES (2u * JS8_MONITOR_LINEAR_BLOCKS * JS8_MONITOR_BLOCK_SIZE)
_Static_assert(HOST_WINDOW_INPUT_SAMPLES == 178560u, "Normal window geometry");
_Static_assert(HOST_SLOT_INPUT_SAMPLES - HOST_WINDOW_INPUT_SAMPLES == 1440u,
               "Aligned Normal slot tail");

static uint32_t full_slots(uint32_t input_samples)
{
    return input_samples / HOST_SLOT_INPUT_SAMPLES;
}

static uint64_t slot_input_start(uint32_t slot)
{
    return (uint64_t)slot * HOST_SLOT_INPUT_SAMPLES;
}

static int seek_slot(HostWav *wav, uint32_t slot)
{
    uint64_t sample = slot_input_start(slot);
    if (sample + HOST_SLOT_INPUT_SAMPLES > wav->total_samples) return -1;
    uint64_t offset = wav->data_offset + sample * 2;
    if (offset > LONG_MAX || fseek(wav->file, (long)offset, SEEK_SET)) return -1;
    wav->samples = wav->total_samples - (uint32_t)sample;
    return 0;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int wav_open(const char *path, HostWav *wav)
{
    FILE *f = fopen(path, "rb");
    uint8_t header[16];
    long length;
    uint64_t data_offset = 0;
    uint32_t data_bytes = 0;
    int have_fmt = 0, have_data = 0;
    if (!f)
        return -1;
    /* Validate every chunk against both RIFF and physical file bounds, even
     * the trailing data that will not form a complete monitor block.
     */
    if (fseek(f, 0, SEEK_END) || (length = ftell(f)) < 12 ||
        fseek(f, 0, SEEK_SET) || fread(header, 1, 12, f) != 12 ||
        memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4))
        goto invalid;
    uint64_t end = (uint64_t)le32(header + 4) + 8;
    if (end < 12 || end > (uint64_t)length)
        goto invalid;
    for (uint64_t pos = 12; pos < end;) {
        if (end - pos < 8 || fseek(f, (long)pos, SEEK_SET) ||
            fread(header, 1, 8, f) != 8)
            goto invalid;
        uint32_t bytes = le32(header + 4);
        uint64_t next = pos + 8 + (uint64_t)bytes + (bytes & 1u);
        /* The pinned upstream WAV excludes its final odd LIST padding byte
         * from RIFF size. Accept that one pad byte only when it exists in the
         * physical file; chunk contents must still be entirely within RIFF.
         */
        if (pos + 8 + (uint64_t)bytes > end || next > (uint64_t)length)
            goto invalid;
        if (!memcmp(header, "fmt ", 4)) {
            if (have_fmt || bytes < 16 || fread(header, 1, 16, f) != 16 ||
                le16(header) != 1 || le16(header + 2) != 1 ||
                le32(header + 4) != 12000 || le32(header + 8) != 24000 ||
                le16(header + 12) != 2 || le16(header + 14) != 16)
                goto invalid;
            have_fmt = 1;
        } else if (!memcmp(header, "data", 4)) {
            if (have_data || (bytes & 1u))
                goto invalid;
            have_data = 1;
            data_offset = pos + 8;
            data_bytes = bytes;
        }
        pos = next;
    }
    if (!have_fmt || !have_data || fseek(f, (long)data_offset, SEEK_SET))
        goto invalid;
    wav->file = f;
    wav->samples = wav->total_samples = data_bytes / 2u;
    wav->data_offset = data_offset;
    return 0;
invalid:
    fclose(f);
    return -1;
}

static int read_block(HostWav *wav, float block[JS8_MONITOR_BLOCK_SIZE])
{
    for (unsigned i = 0; i < JS8_MONITOR_BLOCK_SIZE; ++i) {
        uint8_t pair[4];
        unsigned consumed = wav->samples >= 2 ? 2 : wav->samples;
        if (!consumed || fread(pair, 2, consumed, wav->file) != consumed)
            return -1;
        unsigned raw = le16(pair);
        int sample = raw >= 32768 ? (int)raw - 65536 : (int)raw;
        block[i] = (float)sample / 32768.0f;
        /* Keep input samples 0,2,4,... continuously. Full engine blocks
         * consume 1920 input samples, preserving decimation phase.
         */
        wav->samples -= consumed;
    }
    return 0;
}

static int jsc_file_read(void *context, uint32_t offset, void *dst, size_t bytes)
{
    FILE *file = context;
    return fseek(file, (long)offset, SEEK_SET) || fread(dst, 1, bytes, file) != bytes ? -1 : 0;
}

static int jsc_open(FILE **file, Js8JscDictionary *dict)
{
    const char *path = getenv("JS8_JSC_DICT");
    if (!path) path = JS8_JSC_DEFAULT_DICT;
    *file = fopen(path, "rb");
    if (!*file) return -1;
    if (fseek(*file, 0, SEEK_END) || ftell(*file) != 1918009L) return -1;
    return js8_jsc_dictionary_init(jsc_file_read, *file, 1918009, dict) == JS8_JSC_OK ? 0 : -1;
}

static int32_t candidate_millihz(const Js8MonitorRequirements *req,
                                  const Js8MonitorConfig *cfg, const Js8Candidate *candidate)
{
    return (int32_t)req->min_bin * 6250 + (int32_t)candidate->freq_offset * 6250 +
           (int32_t)candidate->freq_sub * (6250 / (int32_t)cfg->freq_osr);
}

static void print_message_result(uint32_t slot, Js8RxStatus status,
                                 const Js8RxDrops *drops, const Js8RxMessage *message)
{
    if (drops->expired || drops->replaced || drops->evicted)
        fprintf(stderr, "slot=%u reassembly_drop expired=0x%x replaced=0x%x evicted=0x%x\n",
                slot, drops->expired, drops->replaced, drops->evicted);
    const char *error = status == JS8_RX_ORPHAN ? "orphan" :
                        status == JS8_RX_DUPLICATE ? "duplicate" :
                        status == JS8_RX_GAP ? "gap" :
                        status == JS8_RX_OVERFLOW ? "overflow" :
                        status == JS8_RX_INVALID ? "invalid" : NULL;
    if (error) fprintf(stderr, "slot=%u reassembly=%s\n", slot, error);
    if (status != JS8_RX_COMPLETE) return;
    /* Format from the integer stream key, never the rounded PHY diagnostic. */
    int64_t magnitude = message->frequency_millihz;
    const char *sign = magnitude < 0 ? "-" : "";
    if (magnitude < 0) magnitude = -magnitude;
    printf("message from=%s to=%s first_slot=%u last_slot=%u hz=%s%u.%03u text=\"",
           message->from, message->to, message->first_slot, message->last_slot,
           sign, (unsigned)(magnitude / 1000), (unsigned)(magnitude % 1000));
    for (unsigned n = 0; n < message->text_len; ++n) {
        unsigned byte = (unsigned char)message->text[n];
        if (byte == '"' || byte == '\\') putchar('\\');
        if (byte == '\n') fputs("\\n", stdout);
        else if (byte == '\r') fputs("\\r", stdout);
        else if (byte == '\t') fputs("\\t", stdout);
        else if (byte < 32 || byte == 127) printf("\\u%04x", byte);
        else putchar((int)byte);
    }
    puts("\"");
}

static int build_activity(Js8ActivityLog *log, const Js8ActivityFields *facts,
                           const char *text, size_t length, Js8Activity *activity)
{
    if (!js8_activity_build(facts, text, length, activity)) return 1;
    if (!log->error) log->error = "event";
    return 0;
}

int main(int argc, char **argv)
{
    int all_slots = 0, messages = 0, bad_args = argc < 2;
    const char *log_path = NULL;
    Js8LogMetadata metadata = {0};
    int logging_options = 0;
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--log-jsonl") || !strcmp(argv[i], "--dial-hz") ||
            !strcmp(argv[i], "--start-utc")) logging_options = 1;
    if (!logging_options) {
        /* Preserve T062 argument handling and diagnostics without log options. */
        messages = argc == 4 && !strcmp(argv[1], "--all-slots") && !strcmp(argv[2], "--messages");
        all_slots = messages || (argc == 3 && !strcmp(argv[1], "--all-slots"));
        if ((!all_slots && argc != 2) ||
            (argc == 2 && (!strcmp(argv[1], "--all-slots") || !strcmp(argv[1], "--messages"))) ||
            (argc == 3 && all_slots && !strcmp(argv[2], "--messages"))) {
            fprintf(stderr, "usage: %s [--all-slots [--messages]] <12khz-mono-s16.wav>\n", argv[0]);
            return 2;
        }
    } else {
        for (int i = 1; i < argc-1 && !bad_args; ++i) {
            if (!strcmp(argv[i], "--all-slots") && !all_slots) all_slots = 1;
            else if (!strcmp(argv[i], "--messages") && all_slots && !messages) messages = 1;
            else if (!strcmp(argv[i], "--log-jsonl") && !log_path && i+1 < argc-1)
                log_path = argv[++i];
            else if (!strcmp(argv[i], "--dial-hz") && !metadata.have_dial && i+1 < argc-1) {
                bad_args = js8_log_parse_dial(argv[++i], &metadata.dial_hz) != 0;
                metadata.have_dial = 1;
            } else if (!strcmp(argv[i], "--start-utc") && !metadata.have_utc && i+1 < argc-1) {
                bad_args = js8_log_parse_utc(argv[++i], &metadata.start_seconds) != 0;
                metadata.have_utc = 1;
            } else bad_args = 1;
        }
        if (bad_args || (argc >= 2 && !strncmp(argv[argc-1], "--", 2)) ||
            (log_path && !all_slots) || ((metadata.have_dial || metadata.have_utc) && !log_path)) {
            fprintf(stderr, "usage: %s [--all-slots [--messages] [--log-jsonl path [--dial-hz hz] [--start-utc YYYYMMDDTHHMMSSZ]]] <12khz-mono-s16.wav>\n", argv[0]);
            return 2;
        }
    }
    HostWav wav = {0};
    const char *path = argv[argc-1];
    if (wav_open(path, &wav)) {
        fprintf(stderr, "invalid/unreadable 12 kHz mono S16 PCM WAV: %s\n", path);
        return 1;
    }
    int rc = 1;
    Js8ActivityLog log = {0};
    FILE *jsc_file = NULL;
    Js8JscDictionary jsc_dict = {0};
    int jsc_attempted = 0, jsc_ready = 0, jsc_error = 0;
    Js8RxReassembly reassembly = {0};
    Js8Monitor monitor = {0};
    Js8MonitorConfig cfg = js8_monitor_baseline_config();
    Js8MonitorRequirements req;
    void *memory = NULL;
    uint32_t slots = all_slots ? full_slots(wav.total_samples) : 1;
    if (!slots) {
        fprintf(stderr, "--all-slots requires at least one complete 15-second slot\n");
        goto cleanup;
    }
    uint32_t engine_samples = (wav.samples + 1u) / 2u;
    uint32_t blocks = engine_samples / JS8_MONITOR_BLOCK_SIZE;
    if (!blocks) {
        fprintf(stderr, "WAV must contain at least one complete Normal engine block\n");
        goto cleanup;
    }
    /* Container validation has already covered the entire file. Each monitor
     * window is bounded; default mode ignores everything beyond its first one.
     */
    if (blocks > JS8_MONITOR_LINEAR_BLOCKS) blocks = JS8_MONITOR_LINEAR_BLOCKS;
    if (js8_monitor_query_requirements(&cfg, &req) != JS8_MONITOR_OK ||
        !(memory = aligned_alloc(req.alignment, req.total_bytes)) ||
        js8_monitor_init(&monitor, &cfg, memory, req.total_bytes) != JS8_MONITOR_OK) {
        fprintf(stderr, "cannot initialize JS8 monitor workspace\n");
        goto cleanup;
    }
    if (log_path) js8_log_open(&log, log_path, &metadata);
    for (uint32_t slot = 0; slot < slots; ++slot) {
        if (all_slots) {
            if (seek_slot(&wav, slot)) {
                fprintf(stderr, "cannot seek WAV slot %u\n", slot);
                goto cleanup;
            }
            js8_monitor_reset_stream(&monitor);
        }
        for (uint32_t b = 0; b < blocks; ++b) {
            float samples[JS8_MONITOR_BLOCK_SIZE];
            if (read_block(&wav, samples) ||
                js8_monitor_process_block(&monitor, samples) != JS8_MONITOR_OK) {
                fprintf(stderr, "cannot process WAV block %u\n", b);
                goto cleanup;
            }
        }
        Js8WaterfallView wf;
        Js8Candidate candidates[JS8_DECODER_CANDIDATE_CAPACITY];
        uint8_t unique[JS8_DECODER_CANDIDATE_CAPACITY][JS8_PAYLOAD_BITS];
        size_t count = 0, unique_count = 0, valid = 0, ldpc_fail = 0, crc_fail = 0;
        if (js8_monitor_get_waterfall(&monitor, &wf) != JS8_MONITOR_OK ||
            js8_decoder_find_candidates(&wf, candidates, JS8_DECODER_CANDIDATE_CAPACITY,
                                        JS8_DECODER_MIN_SCORE, &count) != JS8_DECODER_OK)
            goto cleanup;
        for (size_t i = 0; i < count; ++i) {
            Js8DecodedPayload payload;
            Js8DecoderStatus status = js8_decoder_decode_candidate(&wf, &candidates[i], &payload);
            if (i < 5) {
                if (all_slots) fprintf(stderr, "slot=%u ", slot);
                fprintf(stderr, "candidate=%zu score=%d time=%d/%u freq=%d/%u status=%d\n",
                        i, candidates[i].score, candidates[i].time_offset, candidates[i].time_sub,
                        candidates[i].freq_offset, candidates[i].freq_sub, status);
            }
            if (status == JS8_DECODER_ERR_LDPC) { ++ldpc_fail; continue; }
            if (status == JS8_DECODER_ERR_CRC) { ++crc_fail; continue; }
            if (status != JS8_DECODER_OK)
                goto cleanup;
            ++valid;
            size_t j;
            for (j = 0; j < unique_count; ++j)
                if (!memcmp(unique[j], payload.payload_bits, JS8_PAYLOAD_BITS)) break;
            if (j < unique_count) continue;
            memcpy(unique[unique_count++], payload.payload_bits, JS8_PAYLOAD_BITS);
            Js8PhysicalFrame frame;
            Js8ProtocolEnvelope envelope;
            if (js8_frame_unpack(payload.payload_bits, &frame) ||
                js8_protocol_envelope_decode(payload.payload_bits, &envelope))
                goto cleanup;
            Js8RxFragment fragment = {0};
            fragment.slot_index = slot;
            fragment.frequency_millihz = candidate_millihz(&req, &cfg, &candidates[i]);
            fragment.tx_flags = envelope.tx_flags;
            Js8RxStatus rx_status = JS8_RX_IGNORED;
            Js8RxDrops drops = {0};
            Js8RxMessage message;
            Js8Activity activity;
            Js8ActivityFields facts = {0};
            facts.slot_index = slot;
            facts.frequency_millihz = fragment.frequency_millihz;
            facts.tx_flags = envelope.tx_flags;
            facts.score = candidates[i].score;
            facts.hard_errors = payload.ldpc_errors;
            int activity_ready = 0;
            if (all_slots) printf("slot=%u slot_s=%u ", slot, slot * 15u);
            fputs("payload=", stdout);
            for (unsigned b = 0; b < JS8_PAYLOAD_BITS; ++b)
                putchar('0' + payload.payload_bits[b]);
            double hz = req.min_bin * 6.25 + candidates[i].freq_offset * 6.25 +
                        candidates[i].freq_sub * (6.25 / cfg.freq_osr);
            /* type is retained as the legacy spelling of raw transmission flags. */
            printf(" type=%u frame=\"%s\" tx_raw=%u class=%s tx=%s score=%d time=%d/%u freq=%d/%u hz=%.3f hard_errors=%d",
                   frame.type, frame.text12, envelope.tx_flags,
                   js8_app_frame_class_name(envelope.app_class), js8_tx_flags_name(envelope.tx_flags),
                   candidates[i].score,
                   candidates[i].time_offset, candidates[i].time_sub,
                   candidates[i].freq_offset, candidates[i].freq_sub, hz, payload.ldpc_errors);
            if (envelope.app_class == JS8_APP_FRAME_HEARTBEAT) {
                Js8BeaconFrame beacon;
                if (js8_beacon_decode(payload.payload_bits, &beacon)) goto cleanup;
                printf(" call=%s beacon=\"%s\" grid=%s", beacon.callsign,
                       js8_beacon_name(beacon.is_cq, beacon.subtype), beacon.grid);
                if (log_path) {
                    facts.kind = beacon.is_cq ? JS8_ACTIVITY_CQ : JS8_ACTIVITY_HEARTBEAT;
                    memcpy(facts.call, beacon.callsign, sizeof(facts.call));
                    memcpy(facts.grid, beacon.grid, sizeof(facts.grid));
                    strcpy(facts.beacon, js8_beacon_name(beacon.is_cq, beacon.subtype));
                    facts.subtype = beacon.subtype;
                    activity_ready = build_activity(&log, &facts, NULL, 0, &activity);
                }
            } else if (envelope.app_class == JS8_APP_FRAME_COMPOUND) {
                Js8CompoundIdentity identity;
                if (js8_compound_identity_decode(payload.payload_bits, &identity)) goto cleanup;
                printf(" call=%s grid=%s", identity.callsign, identity.grid);
                if (log_path) {
                    facts.kind = JS8_ACTIVITY_COMPOUND;
                    memcpy(facts.call, identity.callsign, sizeof(facts.call));
                    memcpy(facts.grid, identity.grid, sizeof(facts.grid));
                    facts.extra = identity.extra16; facts.bits3 = identity.bits3;
                    activity_ready = build_activity(&log, &facts, NULL, 0, &activity);
                }
            } else if (envelope.app_class == JS8_APP_FRAME_COMPOUND_DIRECTED) {
                Js8CompoundFields fields;
                if (js8_compound_fields_decode(payload.payload_bits, &fields)) goto cleanup;
                printf(" call=%s extra=%u bits3=%u", fields.callsign, fields.extra16, fields.bits3);
                if (log_path) {
                    facts.kind = JS8_ACTIVITY_COMPOUND; facts.compound_directed = 1;
                    memcpy(facts.call, fields.callsign, sizeof(facts.call));
                    facts.extra = fields.extra16; facts.bits3 = fields.bits3;
                    activity_ready = build_activity(&log, &facts, NULL, 0, &activity);
                }
            }
            if (envelope.app_class == JS8_APP_FRAME_DIRECTED) {
                Js8DirectedFrame directed;
                if (js8_directed_decode(payload.payload_bits, &directed)) goto cleanup;
                printf(" from=%s to=%s cmd=\"%s\"", directed.from, directed.to,
                       js8_directed_command_name(directed.command_code));
                if (directed.has_number) printf(" num=%d", directed.number);
                if (directed.is_free_text) fputs(" free_text=1", stdout);
                if (directed.is_ack) fputs(" ack=1", stdout);
                if (directed.is_73) fputs(" end73=1", stdout);
                if (log_path) {
                    facts.kind = JS8_ACTIVITY_DIRECTED;
                    memcpy(facts.from, directed.from, sizeof(facts.from));
                    memcpy(facts.to, directed.to, sizeof(facts.to));
                    facts.command_code = directed.command_code;
                    strcpy(facts.command, js8_directed_command_name(directed.command_code));
                    facts.has_number = (uint8_t)directed.has_number; facts.number = directed.number;
                    facts.free_text = (uint8_t)directed.is_free_text;
                    facts.ack = (uint8_t)directed.is_ack; facts.end73 = (uint8_t)directed.is_73;
                    activity_ready = build_activity(&log, &facts, NULL, 0, &activity);
                }
                if (messages) {
                    fragment.kind = JS8_RX_FRAGMENT_DIRECTED;
                    memcpy(fragment.from, directed.from, sizeof(fragment.from));
                    memcpy(fragment.to, directed.to, sizeof(fragment.to));
                    fragment.command_code = directed.command_code;
                    rx_status = js8_rx_reassembly_feed(&reassembly, &fragment, &message, &drops);
                }
            }
            if (envelope.app_class == JS8_APP_FRAME_DATA) {
                Js8HuffmanData data;
                Js8HuffmanStatus huff_status = js8_huffman_data_decode(payload.payload_bits, &data);
                fputs(" codec=huffman", stdout);
                if (huff_status == JS8_HUFF_BAD_PADDING) {
                    fputs(" data_error=bad_padding", stdout);
                } else if (huff_status == JS8_HUFF_OK) {
                    fputs(" data=\"", stdout);
                    for (unsigned n = 0; n < data.text_len; ++n) {
                        if (data.text[n] == '"') putchar('\\');
                        putchar(data.text[n]);
                    }
                    putchar('"');
                    if (log_path) {
                        facts.kind = JS8_ACTIVITY_DATA;
                        facts.codec = JS8_ACTIVITY_CODEC_HUFFMAN;
                        activity_ready = build_activity(&log, &facts, data.text, data.text_len, &activity);
                    }
                    if (messages) {
                        fragment.kind = JS8_RX_FRAGMENT_DATA;
                        fragment.text = data.text;
                        fragment.text_len = data.text_len;
                        rx_status = js8_rx_reassembly_feed(&reassembly, &fragment, &message, &drops);
                    }
                } else {
                    goto cleanup;
                }
            }
            if (envelope.app_class == JS8_APP_FRAME_DATA_COMPRESSED) {
                if (!jsc_attempted) {
                    jsc_attempted = 1;
                    jsc_ready = jsc_open(&jsc_file, &jsc_dict) == 0;
                }
                Js8JscData data;
                Js8JscStatus jsc_status = jsc_ready ?
                    js8_jsc_data_decode(payload.payload_bits, &jsc_dict, &data) : JS8_JSC_BAD_RESOURCE;
                fputs(" codec=jsc", stdout);
                if (jsc_status != JS8_JSC_OK) {
                    fputs(jsc_status == JS8_JSC_BAD_PADDING ? " data_error=bad_padding" :
                          " data_error=resource", stdout);
                    jsc_error = 1;
                } else {
                    fputs(" data=\"", stdout);
                    for (unsigned n = 0; n < data.text_len; ++n) {
                        unsigned byte = (unsigned char)data.text[n];
                        if (byte == '"' || byte == '\\') putchar('\\');
                        if (byte == '\n') fputs("\\n", stdout);
                        else if (byte == '\r') fputs("\\r", stdout);
                        else if (byte == '\t') fputs("\\t", stdout);
                        else if (byte < 32 || byte == 127) printf("\\u%04x", byte);
                        else putchar((int)byte);
                    }
                    putchar('"');
                    if (log_path) {
                        facts.kind = JS8_ACTIVITY_DATA;
                        facts.codec = JS8_ACTIVITY_CODEC_JSC;
                        activity_ready = build_activity(&log, &facts, data.text, data.text_len, &activity);
                    }
                    if (messages) {
                        fragment.kind = JS8_RX_FRAGMENT_DATA;
                        fragment.text = data.text;
                        fragment.text_len = data.text_len;
                        rx_status = js8_rx_reassembly_feed(&reassembly, &fragment, &message, &drops);
                    }
                }
            }
            putchar('\n');
            if (messages) print_message_result(slot, rx_status, &drops, &message);
            if (activity_ready) js8_log_event(&log, &activity);
            if (log_path && rx_status == JS8_RX_COMPLETE) {
                facts.kind = JS8_ACTIVITY_MESSAGE;
                facts.frequency_millihz = message.frequency_millihz;
                memcpy(facts.from, message.from, sizeof(facts.from));
                memcpy(facts.to, message.to, sizeof(facts.to));
                facts.first_slot = message.first_slot; facts.last_slot = message.last_slot;
                if (build_activity(&log, &facts, message.text, message.text_len, &activity))
                    js8_log_event(&log, &activity);
            }
        }
        if (log_path) js8_log_flush(&log);
        if (all_slots) fprintf(stderr, "slot=%u ", slot);
        fprintf(stderr, "blocks=%u ignored_engine_samples=%u candidates=%zu "
                "ldpc_fail=%zu crc_fail=%zu valid=%zu unique=%zu\n", blocks,
                (all_slots ? HOST_SLOT_INPUT_SAMPLES / 2 : engine_samples) -
                blocks * JS8_MONITOR_BLOCK_SIZE, count, ldpc_fail, crc_fail, valid, unique_count);
    }
    if (all_slots)
        fprintf(stderr, "slots=%u trailing_input_samples=%u\n", slots,
                wav.total_samples % HOST_SLOT_INPUT_SAMPLES);
    rc = ferror(stdout) || jsc_error ? 1 : 0;
cleanup:
    js8_log_close(&log);
    if (log.error) { fprintf(stderr, "activity log error: %s\n", log.error); rc = 1; }
    js8_monitor_destroy(&monitor);
    free(memory);
    if (jsc_file) fclose(jsc_file);
    fclose(wav.file);
    return rc;
}
