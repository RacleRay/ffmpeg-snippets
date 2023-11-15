#include <stdio.h>
#include <stdlib.h>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

#include "io_data.h"
#include "audio_encoder_core.h"

static const AVCodec *codec = nullptr;
static AVCodecContext *codec_ctx = nullptr;
static AVFrame *frame = nullptr;
static AVPacket *pkt = nullptr;

static enum AVCodecID audio_codec_id;


int32_t init_audio_encoder(const char *codec_name) {
    if (strcasecmp(codec_name, "MP3") == 0) {
        audio_codec_id = AV_CODEC_ID_MP3;
        std::cout << "Select codec id: MP3" << std::endl;
    } else if (strcasecmp(codec_name, "AAC") == 0) {
        audio_codec_id = AV_CODEC_ID_AAC;
        std::cout << "Select codec id: AAC" << std::endl;
    } else {
        std::cerr << "Error invalid audio format." << std::endl;
        return -1;
    }

    codec = avcodec_find_encoder(audio_codec_id);
    if (!codec) {
        std::cerr << "Error: could not find codec." << std::endl;
        return -1;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "Error: could not allocate codec context." << std::endl;
        return -1;
    }

    codec_ctx->bit_rate = 128000;
    codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;  // fltp 采样格式
    codec_ctx->sample_rate = 44100;   // 44.1kHz
    codec_ctx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;  // 声道数立体声双声道
    codec_ctx->ch_layout.nb_channels = 2;

    int32_t result = avcodec_open2(codec_ctx, codec, nullptr);
    if (result < 0) {
        std::cerr << "Error: could not open codec." << std::endl;
        return -1;
    }

    frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "Error: could not allocate frame." << std::endl;
        return -1;
    }

    frame->nb_samples = codec_ctx->frame_size;
    frame->format = codec_ctx->sample_fmt;
    frame->ch_layout = codec_ctx->ch_layout;
    result = av_frame_get_buffer(frame, 0);
    if (result < 0) {
        std::cerr << "Error: could not get frame buffer." << std::endl;
        return -1;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        std::cerr << "Error: could not allocate packet." << std::endl;
        return -1;
    }

    return 0;
}


static int32_t encode_frame(bool flushing) {
    int32_t result = 0;

    result = avcodec_send_frame(codec_ctx, flushing ? nullptr : frame);
    if (result < 0) {
        std::cerr << "Error: could not avcodec_send_frame." << std::endl;
        return -result;
    }

    while (result >= 0) {
        result = avcodec_receive_packet(codec_ctx, pkt);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            return 1;
        } else if (result < 0) {
            std::cerr << "Error: could not avcodec_receive_packet." << std::endl;
            return result;
        }
        std::cout << "Audio packet size: " << pkt->size << std::endl;
        write_pkt_to_file(pkt);
    }

    return 0;
}


int32_t audio_encoding() {
    int32_t result = 0;
    while (!end_of_input_file()) {
        result = read_pcm_to_frame(frame, codec_ctx);
        if (result < 0) {
            std::cerr << "Error: read_pcm_to_frame failed." << std::endl;
            return result;
        }

        result = encode_frame(false);
        if (result < 0) {
            std::cerr << "Error: encode_frame failed." << std::endl;
            return result;
        }
    }

    result = encode_frame(true);
    if (result < 0) {
        std::cerr << "Error: flushing failed." << std::endl;
        return result;
    }

    return 0;
}


void destroy_audio_encoder() {
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);
}
