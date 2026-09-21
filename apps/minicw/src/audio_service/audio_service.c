/* Silent T042 seam. Preserve finite/hold busy state; no audio resource is opened. */
#include "audio_service.h"
#include "minicw_port.h"
#include "minicw_ascii.h"
#include <stddef.h>
typedef struct { char ch; const char *pattern; } morse_entry_t;
static const morse_entry_t MORSE_TABLE[] = {
    {'A', ".-"},
    {'B', "-..."},
    {'C', "-.-."},
    {'D', "-.."},
    {'E', "."},
    {'F', "..-."},
    {'G', "--."},
    {'H', "...."},
    {'I', ".."},
    {'J', ".---"},
    {'K', "-.-"},
    {'L', ".-.."},
    {'M', "--"},
    {'N', "-."},
    {'O', "---"},
    {'P', ".--."},
    {'Q', "--.-"},
    {'R', ".-."},
    {'S', "..."},
    {'T', "-"},
    {'U', "..-"},
    {'V', "...-"},
    {'W', ".--"},
    {'X', "-..-"},
    {'Y', "-.--"},
    {'Z', "--.."},
    {'0', "-----"},
    {'1', ".----"},
    {'2', "..---"},
    {'3', "...--"},
    {'4', "....-"},
    {'5', "....."},
    {'6', "-...."},
    {'7', "--..."},
    {'8', "---.."},
    {'9', "----."},
    {'.', ".-.-.-"},
    {',', "--..--"},
    {'?', "..--.."},
    {'/', "-..-."},
    {'=', "-...-"},
};


static uint8_t volume;
static uint16_t pitch;
static uint32_t due;
static bool finite, hold;
void audio_service_init(void) { volume = 80; pitch = 700; finite = hold = false; due = 0; }
void audio_service_set_volume(uint8_t v) { volume = v > 100 ? 100 : v; }
uint8_t audio_service_get_volume(void) { return volume; }
void audio_service_set_tone_hz(uint16_t hz) { pitch = hz < 300 ? 300 : hz > 999 ? 999 : hz; }
uint16_t audio_service_get_tone_hz(void) { return pitch; }
void audio_service_play_feedback_beep(void) { }
void audio_service_tone_on(void) { hold = true; }
void audio_service_tone_off(void) { hold = false; }
void audio_service_stop_all(void) { hold = finite = false; }
bool audio_service_is_busy(void)
{
    if (finite && (int32_t)(minicw_port_now_ms() - due) >= 0) finite = false;
    return finite || hold;
}
void audio_service_play_dit(uint16_t ms)
{
    if (!ms) return;
    if (!audio_service_is_busy() || !finite) due = minicw_port_now_ms();
    due += ms;
    finite = true;
}
void audio_service_play_dah(uint16_t ms) { audio_service_play_dit((uint16_t)(3U * ms)); }
const char *audio_service_get_cw_pattern(char ch)
{
    char normalized = (char)minicw_upper((unsigned char)ch);
    for (size_t i = 0; i < sizeof(MORSE_TABLE) / sizeof(MORSE_TABLE[0]); ++i)
        if (MORSE_TABLE[i].ch == normalized) return MORSE_TABLE[i].pattern;
    return NULL;
}
