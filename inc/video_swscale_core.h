#pragma once

#include <cstdint>

int32_t init_video_swscale(char *src_size, char *src_fmt, char *dst_size, char *dst_fmt);
int32_t transform(int32_t frame_cnt);
void destroy_video_swscale();
