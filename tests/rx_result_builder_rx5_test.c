#include <stdio.h>
#include <string.h>

#include "rx_result_builder.h"

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

static void init_slot(Ft8ProtocolSlot *slot,
                      Ft8ProtocolMessage *messages,
                      size_t count)
{
    memset(messages, 0, sizeof(*messages) * count);
    ft8_protocol_slot_init(slot, 77, messages, count);
    slot->message_count = count;
    slot->status = count == 0u ? FT8_PROTOCOL_SLOT_EMPTY : FT8_PROTOCOL_SLOT_OK;
}

int main(void)
{
    RxResultBuilderConfig config = rx_result_builder_default_config();
    RxResultBuilder builder;
    Ft8ProtocolMessage protocol[3];
    Ft8ProtocolSlot slot;
    RxMessage output[3];
    RxBatch batch;

    strcpy(config.local_callsign, "ag6aq");
    CHECK(rx_result_builder_init(&builder, &config) == RX_RESULT_OK);
    CHECK(strcmp(builder.config.local_callsign, "AG6AQ") == 0);

    init_slot(&slot, protocol, 3u);

    protocol[0].type = FT8_PROTOCOL_STANDARD;
    protocol[0].parse_status = FT8_PROTOCOL_PARSE_OK;
    protocol[0].snr_db = -17;
    protocol[0].offset_hz = 1425;
    strcpy(protocol[0].canonical_text, "CQ W1XYZ FN42");
    strcpy(protocol[0].data.standard.call_to, "CQ");
    protocol[0].data.standard.call_to_kind = FT8_PROTOCOL_FIELD_TOKEN;
    strcpy(protocol[0].data.standard.call_de, "W1XYZ");
    strcpy(protocol[0].data.standard.extra, "FN42");

    protocol[1].type = FT8_PROTOCOL_STANDARD;
    protocol[1].parse_status = FT8_PROTOCOL_PARSE_OK;
    protocol[1].snr_db = -9;
    protocol[1].offset_hz = 975;
    strcpy(protocol[1].canonical_text, "<AG6AQ> W6ABC -10");
    strcpy(protocol[1].data.standard.call_to, "<AG6AQ>");
    protocol[1].data.standard.call_to_kind = FT8_PROTOCOL_FIELD_CALL;
    strcpy(protocol[1].data.standard.call_de, "W6ABC");
    strcpy(protocol[1].data.standard.extra, "-10");

    protocol[2].type = FT8_PROTOCOL_FREE_TEXT;
    protocol[2].parse_status = FT8_PROTOCOL_PARSE_OK;
    strcpy(protocol[2].canonical_text, "CQ POTA K7XYZ");
    strcpy(protocol[2].data.free_text.text, "CQ POTA K7XYZ");

    CHECK(rx_result_builder_build(&builder, &slot,
                                  output, 3u, &batch) == RX_RESULT_OK);
    CHECK(batch.slot_id == 77);
    CHECK(batch.message_count == 3u);
    CHECK(output[0].is_cq);
    CHECK(!output[0].is_to_me);
    CHECK(strcmp(output[0].call_de, "W1XYZ") == 0);
    CHECK(strcmp(output[0].extra, "FN42") == 0);
    CHECK(output[0].snr_db == -17);
    CHECK(output[0].offset_hz == 1425);

    CHECK(!output[1].is_cq);
    CHECK(output[1].is_to_me);
    CHECK(strcmp(output[1].call_to, "<AG6AQ>") == 0);
    CHECK(output[1].snr_db == -9);
    CHECK(output[1].offset_hz == 975);

    CHECK(output[2].protocol_type == FT8_PROTOCOL_FREE_TEXT);
    CHECK(output[2].is_cq);
    CHECK(strcmp(output[2].call_de, "K7XYZ") == 0);

    strcpy(protocol[2].canonical_text, "CQ HELLO WORLD");
    CHECK(rx_result_builder_build(&builder, &slot,
                                  output, 3u, &batch) == RX_RESULT_OK);
    CHECK(!output[2].is_cq);

    CHECK(rx_result_builder_build(&builder, &slot,
                                  output, 2u, &batch) == RX_RESULT_ERR_OUTPUT_FULL);

    rx_result_builder_destroy(&builder);
    CHECK(rx_result_builder_build(&builder, &slot,
                                  output, 3u, &batch) == RX_RESULT_ERR_NOT_INITIALIZED);

    puts("rx_result_builder_rx5_test: PASS");
    return 0;
}
