#pragma once

#include <cstdint>

int32_t init_audio_decoder(const char *codec_name);
int32_t audio_decoding();
void destroy_audio_decoder();
