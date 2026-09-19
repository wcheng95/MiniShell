#include "app_rx_order.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void mixed(void)
{
    RxMessage messages[6] = {
        {.snr_db=-5}, {.is_cq=true,.snr_db=-10}, {.is_to_me=true,.snr_db=-18},
        {.is_cq=true,.snr_db=2}, {.is_to_me=true,.snr_db=-3}, {.snr_db=10}
    };
    RxMessage original[6]; memcpy(original,messages,sizeof(messages));
    size_t order[6], expected[]={4,2,3,1,5,0};
    assert(app_rx_order_build(messages,6,order,6)==6);
    assert(memcmp(order,expected,sizeof(order))==0);
    assert(memcmp(messages,original,sizeof(messages))==0);
    messages[2].is_cq=true; /* to-me wins even when both flags are set */
    assert(app_rx_order_build(messages,6,order,6)==6);
    assert(memcmp(order,expected,sizeof(order))==0);
    for (size_t i=0;i<6;++i) messages[i].snr_db=0;
    size_t ties[]={2,4,1,3,0,5};
    assert(app_rx_order_build(messages,6,order,6)==6);
    assert(memcmp(order,ties,sizeof(order))==0);
}

int main(void)
{
    mixed();
    RxMessage messages[50]={0}, original[50];
    struct { size_t order[50]; size_t guard; } output={.guard=123};
    assert(app_rx_order_build(NULL,0,output.order,50)==0);
    assert(app_rx_order_build(messages,0,output.order,50)==0);
    assert(app_rx_order_build(messages,50,output.order,0)==0);
    assert(app_rx_order_build(messages,1,output.order,50)==1 && output.order[0]==0);
    for (size_t i=0;i<50;++i) {
        messages[i].is_to_me=i%3==0; messages[i].is_cq=i%3==1;
        messages[i].snr_db=(int8_t)((int)i/3-8);
        /* Non-key fields deliberately disagree with group/strength. */
        strcpy(messages[i].canonical_text,"CQ NOT A CLASSIFICATION SOURCE");
        messages[i].has_unresolved_hash=i%2;
        messages[i].offset_hz=(int16_t)(50-i);
    }
    memcpy(original,messages,sizeof(messages));
    assert(app_rx_order_build(messages,50,output.order,50)==50);
    size_t at=0;
    for (int group=0;group<3;++group)
        for (int i=49;i>=0;--i)
            if (i%3==group) assert(output.order[at++]==(size_t)i);
    assert(at==50 && output.guard==123 && memcmp(original,messages,sizeof(messages))==0);
    assert(app_rx_order_build(messages,50,output.order,2)==2);
    assert(output.order[0]==0 && output.order[1]==1 && output.guard==123);
    puts("RX order: groups, descending SNR, explicit ties, bounds and immutable input PASS");
    return 0;
}
