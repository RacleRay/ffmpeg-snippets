#pragma once

#include <cstdint>

int32_t init_audio_filter(char *volume_factor);
int32_t audio_filtering();
void destroy_audio_filter();
