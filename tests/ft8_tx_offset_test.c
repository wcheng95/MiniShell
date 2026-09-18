#include "tx_offset.h"
#include "tx_encoder.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void config_test(void)
{
    ConfigService config, roundtrip;
    assert(config_service_parse(&config,"callsign=AG6AQ\ngrid=CM97\n"));
    assert(config.offset_src==FT8_OFFSET_RANDOM && config.fixed_offset_hz==1500);
    const char *sources[]={"offset_src=0\n","offset_src=1\n","offset_src=2\n"};
    for (unsigned i=0;i<3;++i) {
        assert(config_service_parse(&config,sources[i]) && (unsigned)config.offset_src==i);
        char text[2048]; assert(config_service_serialize(&config,text,sizeof(text)));
        assert(strstr(text,sources[i]) && strstr(text,"offset=1500\n"));
        assert(config_service_parse(&roundtrip,text));
        assert(roundtrip.offset_src==config.offset_src && roundtrip.fixed_offset_hz==1500);
    }
    assert(config_service_parse(&config,"offset=300\n") && config.fixed_offset_hz==300);
    assert(config_service_parse(&config,"offset=2700\n") && config.fixed_offset_hz==2700);
    const char *invalid[]={"offset_src=-1","offset_src=3","offset_src=256","offset_src=1junk",
        "offset_src=","offset_src=x","offset=299","offset=2701","offset=-1500","offset=",
        "offset=1500x","offset=1500.0","offset=99999999999999999999999999999999"};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        ConfigService before=config;
        assert(!config_service_parse(&config,invalid[i]));
        assert(memcmp(&config,&before,sizeof(config))==0);
    }
}

static void random_test(void)
{
    const uint32_t sequence[]={270369,67634689,2647435461u,307599695,2398689233u,745495504};
    const int16_t offsets[]={734,1389,905,2473,988,1443};
    uint32_t state=1;
    for (unsigned i=0;i<6;++i) {
        assert(tx_offset_next(&state)==sequence[i]);
        assert(tx_offset_random_hz(state)==offsets[i]);
    }
    assert(tx_offset_seed(0,0,0)==0x9e3779b9u);
    assert(tx_offset_seed(0x9e3779b9u,0,0)!=0);
    assert(tx_offset_seed(UINT64_MAX,INT64_MIN,999999999)!=0);
    assert(tx_offset_seed(1,0,0)!=tx_offset_seed(0,0,0));
    assert(tx_offset_seed(0,1,0)!=tx_offset_seed(0,0,0));
    state=0; assert(tx_offset_next(&state)!=0);
    for (uint32_t i=0;i<2001;++i) {
        int16_t base=tx_offset_random_hz(i);
        assert(base==500+(int)i && tx_offset_random_hz(i+2001)==base);
        for (unsigned tone=0;tone<8;++tone) {
            float hz=base+tone*FT8_TX_TONE_SPACING_HZ;
            assert(hz>=500.0f && hz<=2543.75f);
        }
    }
    assert(tx_offset_random_hz(UINT32_MAX)==500+(int)(UINT32_MAX%2001u));
    for (unsigned i=0;i<100000;++i) {
        int16_t hz=tx_offset_random_hz(tx_offset_next(&state));
        assert(state!=0 && hz>=500 && hz<=2500);
    }
}

static void resolver_test(void)
{
    AutoSeqTxIntent intent={.type=AUTO_SEQ_TX_INTENT_QSO,.offset_hz=1234};
    int16_t hz=0;
    uint32_t state=1;
    assert(tx_offset_resolve(FT8_OFFSET_RX,1500,&intent,&state,&hz) && hz==1234 && state==1);
    assert(tx_offset_resolve(FT8_OFFSET_FIXED,1600,&intent,&state,&hz) && hz==1600 && state==1);
    assert(tx_offset_resolve(FT8_OFFSET_RANDOM,1500,&intent,&state,&hz) && hz==734 && state==270369);
    assert(intent.offset_hz==1234);
    for (unsigned type=AUTO_SEQ_TX_INTENT_QSO;type<=AUTO_SEQ_TX_INTENT_FREETEXT;++type) {
        intent.type=(AutoSeqTxIntentType)type;
        state=1; assert(tx_offset_resolve(FT8_OFFSET_RANDOM,1500,&intent,&state,&hz) && hz==734);
        state=1; assert(tx_offset_resolve(FT8_OFFSET_FIXED,300,&intent,&state,&hz) && hz==300 && state==1);
        assert(tx_offset_resolve(FT8_OFFSET_FIXED,2700,&intent,&state,&hz) && hz==2700 && state==1);
        assert(!tx_offset_resolve(FT8_OFFSET_FIXED,299,&intent,&state,&hz));
        assert(!tx_offset_resolve(FT8_OFFSET_FIXED,2701,&intent,&state,&hz));
        if (type!=AUTO_SEQ_TX_INTENT_QSO) {
            assert(tx_offset_resolve(FT8_OFFSET_RX,1500,&intent,&state,&hz) && hz==734);
        }
    }
    intent.type=AUTO_SEQ_TX_INTENT_QSO;
    const int16_t refs[]={0,299,300,2700,2701};
    for (unsigned i=0;i<5;++i) {
        intent.offset_hz=refs[i]; state=1;
        assert(tx_offset_resolve(FT8_OFFSET_RX,1500,&intent,&state,&hz));
        assert(hz==((i==2 || i==3) ? refs[i] : 734));
    }
    assert(!tx_offset_resolve((Ft8OffsetSource)3,1500,&intent,&state,&hz));
    intent.type=AUTO_SEQ_TX_INTENT_NONE;
    assert(!tx_offset_resolve(FT8_OFFSET_RANDOM,1500,&intent,&state,&hz));
}

int main(void)
{
    config_test(); random_test(); resolver_test();
    puts("TX offset: config, deterministic PRNG, inclusive range, source policy PASS");
    return 0;
}
