extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

#include <iostream>
#include <string>
#include "io_data.h"
#include "video_encoder_core.h"

#define LATENCY_TEST

static const AVCodec *codec = nullptr;  // 编码 AVFrame未编码压缩的图像 得到 AVPacket压缩码流
static AVCodecContext *codec_context = nullptr;
static AVFrame *frame = nullptr;   // 未编码压缩的图像
static AVPacket *packet = nullptr;  // 压缩的视频码流

int32_t init_video_encoder(const char* codec_name) {
    if (strlen(codec_name) == 0) {
        std::cerr << "Error: empty codec name." << std::endl;
        return -1;
    }

    codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec) {
        std::cerr << "Error: could not find codec with codec name:"
                << std::string(codec_name) << std::endl;
        return -1;
    }

    codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        std::cerr << "Error: could not allocate codec context." << std::endl;
        return -1;
    }

    codec_context->profile = FF_PROFILE_H264_HIGH;
    codec_context->bit_rate = 2000000;  // 2Mbps
    codec_context->width = 1280;
    codec_context->height = 720;
    codec_context->gop_size = 10;  // I-frame interval
    codec_context->max_b_frames = 3;  // number of B-frames
    codec_context->time_base = (AVRational){1, 25};  // 25 FPS (timebase should be 1/framerate)
    codec_context->framerate = (AVRational){25, 1};  // 25 FPS (signal the CFR（Constant Frame Rate）)
    codec_context->pix_fmt = AV_PIX_FMT_YUV420P;  // YUV 4:2:0, 12bpp, (1 Cr & Cb sample per 2x2 Y samples)

#ifdef LATENCY_TEST
    if (codec->id == AV_CODEC_ID_H264) {
        // "preset"选项是用于设置编码速度和质量之间的权衡的参数: ultrafast,superfast,veryfast,faster,fast,medium,slow,slower,veryslow
        // "tune"选项是用于设置编码速度和质量之间的权衡的参数: zerolatency,cbr,psnr,ssim
        // ultrafast: 编码速度快，输出质量差
        // zerolatency: 可以禁用 B-frames，帧级多线程编码，前瞻码率控制等特性，但是设置 max_b_frames 时，B-frame 不会被禁用
        av_opt_set(codec_context->priv_data, "preset", "ultrafast", 0);
        av_opt_set(codec_context->priv_data, "tune", "zerolatency", 0);
    }
#else
    // "preset"选项是用于设置编码速度和质量之间的权衡的参数。
    // 不同的预设（preset）值提供了不同的编码速度和输出质量。
    // 通常，较慢的预设（如"slow"）可以提供更高的压缩效率和更好的输出质量，但需要更长的编码时间。
    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set(codec_context->priv_data, "preset", "slow", 0);
    }
#endif

    // Open the codec
    if (avcodec_open2(codec_context, codec, nullptr) < 0) {
        std::cerr << "Error: could not open codec." << std::endl;
        return -1;
    }

    // Allocate the frame and the packet
    frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "Error: could not allocate frame." << std::endl;
        return -1;
    }
    frame->width = codec_context->width;
    frame->height = codec_context->height;
    frame->format = codec_context->pix_fmt;

    packet = av_packet_alloc();
    if (!packet) {
        std::cerr << "Error: could not allocate packet." << std::endl;
        return -1;
    }

    // set frame buffer: 分配用于存储frame图像数据的空间
    if (av_frame_get_buffer(frame, 0) < 0) {
        std::cerr << "Error: could not get frame buffer." << std::endl;
        return -1;
    }

    return 0;
}


// encode 1 frame 的图像
static int32_t encode_frame(bool flushing) {
    int32_t result = 0;
    if (!flushing) {
        std::cout << "Send frame to encoder with pts: " << frame->pts << std::endl;
    }

    // nullptr 表示输入结束，将缓冲区内容输出
    // 图像送入编码器
    result = avcodec_send_frame(codec_context, flushing ? nullptr : frame);
    if (result < 0) {
        std::cerr << "Error: avcodec_send_frame could not send frame to encoder." << std::endl;
        return result;
    }

    while (result >= 0) {
        // 从编码器中获取视频码流
        result = avcodec_receive_packet(codec_context, packet);
        // EAGAIN: 一帧的编码未完成，需要继续 avcodec_send_frame，AVERROR_EOF 编码完成，且已输出内部缓存的码流
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            return 1;
        } else if (result < 0) {
            std::cerr << "Error: avcodec_receive_packet could not receive packet from encoder." << std::endl;
            return result;
        }

        if (flushing) {
            std::cout << "Flushing encoder." << std::endl;
        }
        std::cout << "Got encoded package with dts:" << packet->dts
                << ", pts:" << packet->pts << ", " << std::endl;
        write_pkt_to_file(packet);
    }

    return 0;
}


int32_t encoding(int32_t n_frame_to_encode) {
    int result = 0;
    for (size_t i = 0; i < n_frame_to_encode; i++) {
        result = av_frame_make_writable(frame);
        if (result < 0) {
            std::cerr << "Error: av_frame_make_writable could not make frame writable." << std::endl;
            return result;
        }

        // 从 input_file 中读取一帧的数据
        result = read_yuv_to_frame(frame);
        if (result < 0) {
            std::cerr << "Error: read_yuv_to_frame could not read frame from input file." << std::endl;
            return result;
        }

        frame->pts = i;  // 当前显示时间戳； dts 解码时间戳
        result = encode_frame(false);
        if (result < 0) {
            std::cerr << "Error: encode_frame could not encode frame." << std::endl;
            return result;
        }
    }

    result = encode_frame(true);
    if (result < 0) {
        std::cerr << "Error: encode_frame could not flush frame." << std::endl;
        return result;
    }

    return 0;
}


void destroy_video_encoder() {
    avcodec_free_context(&codec_context);
    av_frame_free(&frame);
    av_packet_free(&packet);
}