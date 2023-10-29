#ifndef __VIDEO_ENCODER_CORE_H
#define __VIDEO_ENCODER_CORE_H

#include <cstdint>

int32_t init_video_encoder(const char* codec_name);
void destroy_video_encoder();

int32_t encoding(int32_t frame_cnt);

#endif //__VIDEO_ENCODER_CORE_H