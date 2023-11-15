#pragma once

#include "audio_decoder_core.h"
#include <stdint.h>

int32_t init_audio_encoder(const char* codec_name);
int32_t audio_encoding();
void destroy_audio_encoder();
