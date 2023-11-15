extern "C" {
#include <libavcodec/avcodec.h>
}
#include <iostream>

#include "io_data.h"
#include "video_decoder_core.h"

#define INPUT_BUF_SIZE 4096

static const AVCodec *codec = nullptr;
static AVCodecContext *codec_context = nullptr;
static AVFrame *frame = nullptr;
static AVPacket *packet = nullptr;
// 从二进制数据流中，解析出符合指定编码的码流包
static AVCodecParserContext *parser = nullptr;


int32_t init_video_decoder() {
    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (codec == nullptr) {
        std::cerr << "Error: could not find codec." << std::endl;
        return -1;
    }

    parser = av_parser_init(codec->id);
    if (parser == nullptr) {
        std::cerr << "Error: could not init parser." << std::endl;
        return -1;
    }

    codec_context = avcodec_alloc_context3(codec);
    if (codec_context == nullptr) {
        std::cerr << "Error: could not alloc codec context." << std::endl;
        return -1;
    }

    int32_t result = avcodec_open2(codec_context, codec, nullptr);
    if (result < 0) {
        std::cerr << "Error: could not open codec." << std::endl;
        return -1;
    }

    frame = av_frame_alloc();
    if (frame == nullptr) {
        std::cerr << "Error: could not alloc frame." << std::endl;
        return -1;
    }

    packet = av_packet_alloc();
    if (packet == nullptr) {
        std::cerr << "Error: could not alloc packet." << std::endl;
        return -1;
    }

    return 0;
}


static int32_t decode_packet(bool flushing) {
    int32_t result = avcodec_send_packet(codec_context,  flushing ? nullptr: packet);
    if (result < 0) {
        std::cerr << "Error: faile to send packet, result:" << result << std::endl;
        return -1;
    }

    while (result >= 0) {
        result = avcodec_receive_frame(codec_context, frame);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            return 1;
        } else if (result < 0) {
            std::cerr << "Error: faile to receive frame, result:" << result << std::endl;
            return -1;
        }
        // 非以上情况，会继续执行以下逻辑

        if (flushing) {
            std::cout << "Flushing frame." << std::endl;
        }
        std::cout << "Write frame pic_num:" << frame->coded_picture_number << std::endl;
        write_frame_to_yuv(frame);  // write frame to output_file
    }

    return 0;
}


int32_t decoding() {
    uint8_t read_buf[INPUT_BUF_SIZE] = {0};
    int32_t result = 0;
    uint8_t* data = nullptr;
    int32_t data_size = 0;

    while (!end_of_input_file()) {
        result = read_data_to_buf(read_buf, INPUT_BUF_SIZE, data_size);
        if (result < 0) {
            std::cerr << "Error: read_data_to_buf failed." << std::endl;
            return -1;
        }

        data = read_buf;
        while (data_size > 0) {
            // av_parser_parse2: 解析出符合指定编码的码流包。解码的另一种方式是通过 avformat_open_input，直接以指定编码格式打开文件，从其返回的 AVFormatContext 中的 AVPacket 中获得码流包
            result = av_parser_parse2(parser, codec_context, &packet->data,
                &packet->size, data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (result < 0) {
                std::cerr << "Error: av_parser_parse2 failed." << std::endl;
                return -1;
            }

            data += result;
            data_size -= result;

            if (packet->size > 0) {
                std::cout << "Parsed packet size:" << packet->size << std::endl;
                // 如果没有读完 packet，decode 时会返回 1，并且等待下次循环继续读取数据
                result = decode_packet(false);
                if (result < 0) {
                    break;
                }
            }
        }
    }

    result = decode_packet(true);
    if (result < 0) {
        return result;
    }

    return 0;
}


void destroy_video_decoder() {
    av_parser_close(parser);
    avcodec_free_context(&codec_context);
    av_frame_free(&frame);
    av_packet_free(&packet);
}