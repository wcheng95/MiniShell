#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft8_engine.h"

typedef struct {
    FILE *file;
    uint32_t sample_count;
} HostWav;

static int read_u16(FILE *f, uint16_t *out)
{
    uint8_t b[2];
    if (fread(b, 1, sizeof(b), f) != sizeof(b))
        return -1;
    *out = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return 0;
}

static int read_u32(FILE *f, uint32_t *out)
{
    uint8_t b[4];
    if (fread(b, 1, sizeof(b), f) != sizeof(b))
        return -1;
    *out = (uint32_t)b[0] |
           ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
    return 0;
}

static int skip_bytes(FILE *f, uint32_t bytes)
{
    return fseek(f, (long)bytes, SEEK_CUR) == 0 ? 0 : -1;
}

static int host_wav_open(const char *path, HostWav *wav)
{
    FILE *f;
    char id[4];
    uint32_t ignored32;
    uint16_t audio_format = 0u;
    uint16_t channels = 0u;
    uint16_t bits = 0u;
    uint32_t sample_rate = 0u;
    int have_fmt = 0;

    if (path == NULL || wav == NULL)
        return -1;

    memset(wav, 0, sizeof(*wav));
    f = fopen(path, "rb");
    if (f == NULL)
        return -1;

    if (fread(id, 1, sizeof(id), f) != sizeof(id) || memcmp(id, "RIFF", 4) != 0 ||
        read_u32(f, &ignored32) != 0 ||
        fread(id, 1, sizeof(id), f) != sizeof(id) || memcmp(id, "WAVE", 4) != 0) {
        fclose(f);
        return -1;
    }

    for (;;) {
        uint32_t chunk_size;

        if (fread(id, 1, sizeof(id), f) != sizeof(id) || read_u32(f, &chunk_size) != 0)
            break;

        if (memcmp(id, "fmt ", 4) == 0) {
            uint32_t byte_rate;
            uint16_t block_align;

            if (chunk_size < 16u ||
                read_u16(f, &audio_format) != 0 ||
                read_u16(f, &channels) != 0 ||
                read_u32(f, &sample_rate) != 0 ||
                read_u32(f, &byte_rate) != 0 ||
                read_u16(f, &block_align) != 0 ||
                read_u16(f, &bits) != 0 ||
                skip_bytes(f, chunk_size - 16u) != 0) {
                fclose(f);
                return -1;
            }
            have_fmt = 1;
        } else if (memcmp(id, "data", 4) == 0) {
            if (!have_fmt ||
                audio_format != 1u ||
                channels != 1u ||
                bits != 16u ||
                sample_rate != FT8_ENGINE_SAMPLE_RATE_HZ ||
                (chunk_size & 1u) != 0u) {
                fclose(f);
                return -1;
            }
            wav->file = f;
            wav->sample_count = chunk_size / 2u;
            return 0;
        } else if (skip_bytes(f, chunk_size + (chunk_size & 1u)) != 0) {
            break;
        }
    }

    fclose(f);
    return -1;
}

static void host_wav_close(HostWav *wav)
{
    if (wav == NULL)
        return;
    if (wav->file != NULL)
        fclose(wav->file);
    memset(wav, 0, sizeof(*wav));
}

static int read_engine_block(HostWav *wav, float block[FT8_ENGINE_BLOCK_SIZE])
{
    uint32_t i;

    for (i = 0u; i < FT8_ENGINE_BLOCK_SIZE; ++i) {
        uint16_t raw;
        if (read_u16(wav->file, &raw) != 0)
            return -1;
        block[i] = (float)(int16_t)raw / 32768.0f;
    }
    return 0;
}

static void print_payload_fallback(const Ft8ProtocolMessage *message)
{
    size_t i;

    fputs("payload=", stdout);
    for (i = 0u; i < FT8_PAYLOAD_BYTES; ++i)
        printf("%02X", message->payload[i]);
    printf(" type=%d parse=%d\n", (int)message->type, (int)message->parse_status);
}

int main(int argc, char **argv)
{
    Ft8EngineConfig config = ft8_engine_baseline_config();
    Ft8EngineRequirements req;
    Ft8Engine engine;
    Ft8ProtocolMessage messages[FT8_DECODER_CANDIDATE_CAPACITY];
    Ft8ProtocolSlot slot;
    HostWav wav;
    void *workspace = NULL;
    float block[FT8_ENGINE_BLOCK_SIZE];
    uint32_t full_blocks;
    uint32_t block_index;
    Ft8EngineStatus status;
    int engine_initialized = 0;
    int rc = 1;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <6khz-mono-s16.wav>\n", argv[0]);
        return 2;
    }

    if (host_wav_open(argv[1], &wav) != 0) {
        fprintf(stderr,
                "cannot open '%s' as 6 kHz mono 16-bit PCM WAV\n",
                argv[1]);
        return 1;
    }

    if (ft8_engine_query_requirements(&config, &req) != FT8_ENGINE_OK ||
        posix_memalign(&workspace, req.alignment, req.workspace_bytes) != 0 ||
        workspace == NULL) {
        fprintf(stderr, "cannot allocate FT8 engine workspace\n");
        goto cleanup;
    }

    if (ft8_engine_init(&engine, &config, workspace, req.workspace_bytes) != FT8_ENGINE_OK) {
        fprintf(stderr, "cannot initialize FT8 engine\n");
        goto cleanup;
    }
    engine_initialized = 1;

    if (ft8_engine_begin_window(&engine, 0) != FT8_ENGINE_OK) {
        fprintf(stderr, "cannot begin FT8 decode window\n");
        goto cleanup;
    }

    full_blocks = wav.sample_count / FT8_ENGINE_BLOCK_SIZE;
    if (full_blocks == 0u) {
        fprintf(stderr, "WAV does not contain one complete FT8 engine block\n");
        goto cleanup;
    }

    for (block_index = 0u; block_index < full_blocks; ++block_index) {
        if (read_engine_block(&wav, block) != 0) {
            fprintf(stderr, "truncated WAV data\n");
            goto cleanup;
        }

        status = ft8_engine_process_block(&engine, block);
        if (status == FT8_ENGINE_WATERFALL_FULL) {
            fprintf(stderr,
                    "WAV contains more than one RX-2 decode window; multi-slot WAVs belong to RX-4\n");
            goto cleanup;
        }
        if (status != FT8_ENGINE_OK) {
            fprintf(stderr, "FT8 engine rejected block %u (status %d)\n",
                    block_index, (int)status);
            goto cleanup;
        }
    }

    status = ft8_engine_finalize_window(&engine,
                                        messages,
                                        FT8_DECODER_CANDIDATE_CAPACITY,
                                        &slot);
    if (status != FT8_ENGINE_OK && status != FT8_ENGINE_NO_MESSAGES) {
        fprintf(stderr, "FT8 decode failed (status %d)\n", (int)status);
        goto cleanup;
    }

    if (status == FT8_ENGINE_NO_MESSAGES) {
        puts("No FT8 messages decoded.");
        rc = 0;
        goto cleanup;
    }

    for (size_t i = 0u; i < slot.message_count; ++i) {
        if (slot.messages[i].canonical_text[0] != '\0')
            puts(slot.messages[i].canonical_text);
        else
            print_payload_fallback(&slot.messages[i]);
    }

    rc = 0;

cleanup:
    if (engine_initialized)
        ft8_engine_destroy(&engine);
    host_wav_close(&wav);
    free(workspace);
    return rc;
}
