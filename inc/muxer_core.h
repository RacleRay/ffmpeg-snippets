#pragma once

#include <stdint.h>

int32_t init_muxer(const char *video_input_file, const char *audio_input_file, const char *output_file);
int32_t muxing();
void destroy_muxer();
