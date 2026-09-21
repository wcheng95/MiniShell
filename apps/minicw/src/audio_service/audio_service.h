#pragma once
#include <stdbool.h>
#include <stdint.h>
void audio_service_init(void);
void audio_service_set_volume(uint8_t volume);
uint8_t audio_service_get_volume(void);
void audio_service_set_tone_hz(uint16_t hz);
uint16_t audio_service_get_tone_hz(void);
void audio_service_play_feedback_beep(void);
void audio_service_tone_on(void);
void audio_service_tone_off(void);
void audio_service_stop_all(void);
bool audio_service_is_busy(void);
void audio_service_play_dit(uint16_t ms);
void audio_service_play_dah(uint16_t ms);
const char *audio_service_get_cw_pattern(char ch);
