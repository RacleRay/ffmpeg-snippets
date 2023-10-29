#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/types.h>

#include "io_data.h"


static FILE* input_file = nullptr;
static FILE* output_file = nullptr;


int32_t open_input_output_files(const char *input_name, const char *output_name) {
    if (strlen(input_name) == 0 || strlen(output_name) == 0) {
        std::cerr << "Error: empty input or output file." << std::endl;
        return -1;
    }

    // 全局指针，保证之前指向的资源被释放
    close_input_output_files();

    input_file = fopen(input_name, "rb");
    if (input_file == nullptr) {
        std::cerr << "Error: cannot open input file." << std::endl;
        return -1;
    }

    output_file = fopen(output_name, "wb");
    if (output_file == nullptr) {
        std::cerr << "Error: cannot open output file." << std::endl;
        return -1;
    }

    return 0;
}


void close_input_output_files() {
    if (input_file != nullptr) {
        fclose(input_file);
        input_file = nullptr;
    }
    if (output_file != nullptr) {
        fclose(output_file);
        output_file = nullptr;
    }
}


int32_t end_of_input_file() {
    return feof(input_file);
}


int32_t read_data_to_buf(uint8_t* buf, int32_t size, int32_t& out_size) {
    int32_t read_size = fread(buf, 1, size, input_file);
    if (read_size == 0) {
        std::cerr << "Error: cannot read data from input file." << std::endl;
        return -1;
    }
    out_size = read_size;
    return 0;
}


// YUV 格式为 4:2:0 (4:1:1)
int32_t write_frame_to_yuv(AVFrame *frame) {
    uint8_t** p_buf = frame->data;
    int* p_stride = frame->linesize;

    for (int i = 0; i < 3; i++) {
        // Y frame is double sized to UV frame
        int32_t width = (i == 0 ? frame->width : frame->width /2);
        int32_t height = (i == 0 ? frame->height : frame->height /2);

        for (size_t j = 0; j < height; j++) {
            fwrite(p_buf[i], 1, width, output_file);
            p_buf[i] += p_stride[i];
        }
    }

    return 0;
}


// 从 input_file 中读取一帧 YUV 格式的数据，并转换为 AVFrame
// YUV 格式为 4:2:0 (4:1:1)
int32_t read_yuv_to_frame(AVFrame *frame) {
    int32_t frame_width = frame->width;  // 数据保存时的宽度，可能有padding
    int32_t frame_height = frame->height;
    int32_t luma_stride = frame->linesize[0];  // 亮度，实际数据每行大小
    int32_t chroma_stride = frame->linesize[1];  // 色度
    int32_t frame_size = frame_height * frame_width * 3 / 2;  // UV frame 的大小只有 1/4 的 Y frame 大
    int32_t read_size = 0;

    if (frame_width == luma_stride) {
        // 不存在 padding , 数据全是有效内容
        read_size += fread(frame->data[0], 1, frame_width * frame_height, input_file);
        read_size += fread(frame->data[1], 1, frame_width * frame_height / 4, input_file);
        read_size += fread(frame->data[2], 1, frame_width * frame_height / 4, input_file);
    } else {
        for (size_t i = 0; i < frame_height; ++i) {
            read_size += fread(frame->data[0] + i * luma_stride, 1, frame_width, input_file);
        }

        for (size_t uv = 1; uv < 2; ++uv) {
            for (size_t i = 0; i < frame_height / 2; i ++) {
                read_size += fread(frame->data[uv] + i * chroma_stride, 1, frame_width / 2, input_file);
            }
        }
    }

    if (read_size != frame_size) {
        std::cerr << "Error: read size is not right, frame_size" << frame_size << ", read_size" << read_size << std::endl;
        return -1;
    }

    return 0;
}


void write_pkt_to_file(AVPacket *pkt) {
    fwrite(pkt->data, 1, pkt->size, output_file);
}