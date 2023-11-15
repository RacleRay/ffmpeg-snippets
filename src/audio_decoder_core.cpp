#include <cmath>
#include <cstdint>
extern "C" {
#include <libavcodec/avcodec.h>
}

#include <iostream>

#include "audio_decoder_core.h"
#include "io_data.h"

#define AUDIO_INBUF_SIZE    20480
#define AUDIO_REFILL_THRESH 4096

static const AVCodec *codec = nullptr;
static AVCodecContext *codec_ctx = nullptr;
static AVCodecParserContext *parser = nullptr;

static AVFrame *frame = nullptr;
static AVPacket *packet = nullptr;
static enum AVCodecID codec_id;

int32_t init_audio_decoder(const char *audio_codec) {
    if (strcasecmp(audio_codec, "MP3") == 0) {
        codec_id = AV_CODEC_ID_MP3;
        std::cout << "Select codec id: MP3" << std::endl;
    } else if (strcasecmp(audio_codec, "AAC") == 0) {
        codec_id = AV_CODEC_ID_AAC;
        std::cout << "Select codec id: AAC" << std::endl;
    } else {
        std::cerr << "Error invalid audio format." << std::endl;
        return -1;
    }

    codec = avcodec_find_decoder(codec_id);
    if (!codec) {
        std::cerr << "Error: cannot find codec." << std::endl;
        return -1;
    }

    parser = av_parser_init(codec->id);
    if (!parser) {
        std::cerr << "Error: cannot find parser." << std::endl;
        return -1;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "Error: cannot allocate codec context." << std::endl;
        return -1;
    }

    int32_t result = avcodec_open2(codec_ctx, codec, nullptr);
    if (result < 0) {
        std::cerr << "Error: cannot open codec." << std::endl;
        return -1;
    }

    frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "Error: cannot allocate frame." << std::endl;
        return -1;
    }

    packet = av_packet_alloc();
    if (!packet) {
        std::cerr << "Error: cannot allocate packet." << std::endl;
        return -1;
    }

    return 0;
}


static int32_t decode_packet(bool flushing) {
    int32_t result = 0;
    result = avcodec_send_packet(codec_ctx, packet);
    if (result < 0) {
        std::cerr << "Error: cannot send packet. result : " << result << std::endl;
        return -1;
    }

    while (result >= 0) {
        result = avcodec_receive_frame(codec_ctx, frame);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) { // empty or end
            return 1;
        }
        if (result < 0) {
            std::cerr << "Error: cannot receive frame. result : " << result << std::endl;
            return -1;
        }

        if (flushing) {
            std::cout << "Flushing audio data." << std::endl;
        }

        write_samples_to_pcm(frame, codec_ctx);
        std::cout << "frame->nb_samples:" << frame->nb_samples
                << ", frame->channels:" << frame->ch_layout.nb_channels << std::endl;
    }

    return result;
}


static int get_format_from_sample_fmt(const char** fmt, enum AVSampleFormat sample_fmt) {
    int i;
    struct sample_fmt_entry {
        enum AVSampleFormat sample_fmt;
        const char *fmt_be, *fmt_le;
    };

    // big endian or little endian
    struct sample_fmt_entry sample_fmt_entries[] = {
        { AV_SAMPLE_FMT_U8, "u8", "s8" },
        { AV_SAMPLE_FMT_S16, "s16be", "s16le" },
        { AV_SAMPLE_FMT_S32, "s32be", "s32le" },
        { AV_SAMPLE_FMT_FLT, "f32be", "f32le" },
        { AV_SAMPLE_FMT_DBL, "f64be", "f64le" },
    };
    *fmt = nullptr;

    for (int i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); ++i) {
        struct sample_fmt_entry *e = &sample_fmt_entries[i];
        if (e->sample_fmt == sample_fmt) {
            // AV_HAVE_BIGENDIAN
            *fmt = AV_NE(e->fmt_be, e->fmt_le);
            return 0;
        }
    }

    std::cerr << "sample format %s is not supported as output format\n"
            << av_get_sample_fmt_name(sample_fmt) << std::endl;
    return -1;
}


static int32_t get_audio_format(AVCodecContext *codec_ctx) {
    int ret = 0;
    const char *fmt = nullptr;
    enum AVSampleFormat sample_fmt = codec_ctx->sample_fmt;

    if (av_sample_fmt_is_planar(sample_fmt)) {
        const char *fmt_name = av_get_sample_fmt_name(sample_fmt);
        std::cout << "Warning: the sample format the decoder produced is planar "
            << std::string(fmt_name)
            << ", This example will output the first channel only."
            << std::endl;
        sample_fmt = av_get_packed_sample_fmt(sample_fmt);
    }

    int n_channels = codec_ctx->ch_layout.nb_channels;
    ret = get_format_from_sample_fmt(&fmt, sample_fmt);
    if (ret < 0) {
        return -1;
    }

    std::cout << "Play command: ffpay -f " << std::string(fmt) << " -ac "
            << n_channels << " -ar " << codec_ctx->sample_rate << " output.pcm"
            << std::endl;

    return 0;
}


int32_t audio_decoding() {
    uint8_t inbuf[AUDIO_INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE] = {0};
    uint8_t *data = nullptr;
    int32_t result = 0;
    int32_t data_size = 0;

    while (!end_of_input_file()) {
        result = read_data_to_buf(inbuf, AUDIO_INBUF_SIZE, data_size);
        if (result < 0) {
            std::cerr << "Error: cannot read_data_to_buf." << std::endl;
            return -1;
        }

        data = inbuf;
        while (data_size > 0) {
            result = av_parser_parse2(parser, codec_ctx, &packet->data, &packet->size, data,
                                data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (result < 0) {
                std::cerr << "Error: av_parser_parse2 failed." << std::endl;
                return -1;
            }

            data += result;
            data_size -= result;
            if (packet->size) {
                std::cout << "Parsed packet size:" << packet->size << std::endl;
                decode_packet(false);
            }
        }
    }

    decode_packet(true);

    get_audio_format(codec_ctx);
    return 0;
}


void destroy_audio_decoder() {
    av_parser_close(parser);
    avcodec_free_context(&codec_ctx);
    av_frame_free(&frame);
    av_packet_free(&packet);
}

