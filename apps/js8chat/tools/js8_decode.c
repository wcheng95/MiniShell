#include "js8_decoder.h"
#include "js8_frame.h"
#include "js8_protocol_frame.h"
#include "js8_compound.h"
#include "js8_directed.h"
#include "js8_huffman.h"
#include "js8_jsc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FILE *file;
    uint32_t samples;
} HostWav;

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
    wav->samples = data_bytes / 2u;
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

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <12khz-mono-s16.wav>\n", argv[0]);
        return 2;
    }
    HostWav wav = {0};
    if (wav_open(argv[1], &wav)) {
        fprintf(stderr, "invalid/unreadable 12 kHz mono S16 PCM WAV: %s\n", argv[1]);
        return 1;
    }
    int rc = 1;
    FILE *jsc_file = NULL;
    Js8JscDictionary jsc_dict = {0};
    int jsc_attempted = 0, jsc_ready = 0, jsc_error = 0;
    Js8Monitor monitor = {0};
    Js8MonitorConfig cfg = js8_monitor_baseline_config();
    Js8MonitorRequirements req;
    void *memory = NULL;
    uint32_t engine_samples = (wav.samples + 1u) / 2u;
    uint32_t blocks = engine_samples / JS8_MONITOR_BLOCK_SIZE;
    if (!blocks) {
        fprintf(stderr, "WAV must contain at least one complete Normal engine block\n");
        goto cleanup;
    }
    /* Container validation has already covered the entire file. Only the
     * first window feeds DSP; later complete blocks and partial tail are ignored.
     */
    if (blocks > JS8_MONITOR_LINEAR_BLOCKS) blocks = JS8_MONITOR_LINEAR_BLOCKS;
    if (js8_monitor_query_requirements(&cfg, &req) != JS8_MONITOR_OK ||
        !(memory = aligned_alloc(req.alignment, req.total_bytes)) ||
        js8_monitor_init(&monitor, &cfg, memory, req.total_bytes) != JS8_MONITOR_OK) {
        fprintf(stderr, "cannot initialize JS8 monitor workspace\n");
        goto cleanup;
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
        if (i < 5)
            fprintf(stderr, "candidate=%zu score=%d time=%d/%u freq=%d/%u status=%d\n",
                    i, candidates[i].score, candidates[i].time_offset, candidates[i].time_sub,
                    candidates[i].freq_offset, candidates[i].freq_sub, status);
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
        } else if (envelope.app_class == JS8_APP_FRAME_COMPOUND) {
            Js8CompoundIdentity identity;
            if (js8_compound_identity_decode(payload.payload_bits, &identity)) goto cleanup;
            printf(" call=%s grid=%s", identity.callsign, identity.grid);
        } else if (envelope.app_class == JS8_APP_FRAME_COMPOUND_DIRECTED) {
            Js8CompoundFields fields;
            if (js8_compound_fields_decode(payload.payload_bits, &fields)) goto cleanup;
            printf(" call=%s extra=%u bits3=%u", fields.callsign, fields.extra16, fields.bits3);
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
            }
        }
        putchar('\n');
    }
    fprintf(stderr, "blocks=%u ignored_engine_samples=%u candidates=%zu "
            "ldpc_fail=%zu crc_fail=%zu valid=%zu unique=%zu\n", blocks,
            engine_samples - blocks * JS8_MONITOR_BLOCK_SIZE, count, ldpc_fail, crc_fail, valid, unique_count);
    rc = ferror(stdout) || jsc_error ? 1 : 0;
cleanup:
    js8_monitor_destroy(&monitor);
    free(memory);
    if (jsc_file) fclose(jsc_file);
    fclose(wav.file);
    return rc;
}
