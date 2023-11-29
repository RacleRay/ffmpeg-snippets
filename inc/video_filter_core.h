#pragma once

#include <cstdint>


int32_t init_video_filter(int32_t width, int32_t height, const char* filter_describe);
int32_t filter_video(int32_t frame_cnt);
void destroy_video_filter();
