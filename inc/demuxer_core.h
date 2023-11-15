#pragma once

#include <cstdint>

int32_t init_demuxer(const char *input_name, const char *video_output, const char *audio_output);
int32_t demuxing(const char *video_output_name, const char *audio_output_name);
void destroy_demuxer();