extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
}

#include <iostream>

#include "demuxer_core.h"
#include "io_data.h"

static AVFormatContext *format_ctx = nullptr;
static AVCodecContext *video_dec_ctx = nullptr, *audio_dec_ctx = nullptr;

static int video_stream_index = -1, audio_stream_index = -1;

static AVStream *video_stream = nullptr, *audio_stream = nullptr;

static FILE *output_video_file = nullptr, *output_audio_file = nullptr;
static AVFrame *frame = nullptr;
static AVPacket pkt;

static int open_codec_context(
    int32_t *stream_idx,
    AVCodecContext **decode_ctx,
    AVFormatContext *fmt_ctx,
    enum AVMediaType type) {
    int result = 0;

    result = av_find_best_stream(fmt_ctx, type, -1, -1, nullptr, 0);
    if (result < 0) {
        std::cerr << "Error: Could not find "
                  << std::string(av_get_media_type_string(type))
                  << " stream in input file." << std::endl;
        return result;
    }

    int stream_index = result;
    AVStream *st = fmt_ctx->streams[stream_index];

    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        std::cerr << "Error: Failed to find codec:"
                  << std::string(av_get_media_type_string(type)) << std::endl;
        return -1;
    }

    *decode_ctx = avcodec_alloc_context3(codec);
    if (!*decode_ctx) {
        std::cerr << "Error: Failed to alloc codec context:"
                  << std::string(av_get_media_type_string(type)) << std::endl;
        return -1;
    }

    // Fill the codec context based on the values from the supplied codec
    // parameters
    if ((result = avcodec_parameters_to_context(*decode_ctx, st->codecpar))
        < 0) {
        std::cerr
            << "Error: Failed to copy codec parameters to decoder context."
            << std::endl;
        return result;
    }

    if ((result = avcodec_open2(*decode_ctx, codec, nullptr)) < 0) {
        std::cerr << "Error: Could not open "
                  << std::string(av_get_media_type_string(type)) << " codec."
                  << std::endl;
        return result;
    }

    *stream_idx = stream_index;

    return 0;
}

static int32_t write_frame_to_yuv1(AVFrame *frame) {
    uint8_t **pbuf = frame->data;
    int *pstride = frame->linesize;

    for (size_t i = 0; i < 3; ++i) {
        int32_t width = (i == 0 ? frame->width : frame->width / 2);
        int32_t height = (i == 0 ? frame->height : frame->height / 2);
        for (size_t j = 0; j < height; ++j) {
            fwrite(pbuf[i], 1, width, output_audio_file);
            pbuf[i] += pstride[i];
        }
    }

    return 0;
}

static int32_t
write_samples_to_pcm1(AVFrame *frame, AVCodecContext *codec_ctx) {
    int data_size = av_get_bytes_per_sample(codec_ctx->sample_fmt);
    if (data_size < 0) {
        std::cerr << "Failed to calculate data size" << std::endl;
        return -1;
    }

    for (int i = 0; i < frame->nb_samples; ++i) {
        for (int ch = 0; ch < codec_ctx->ch_layout.nb_channels; ch++) {
            fwrite(
                frame->data[ch] + data_size * i, 1, data_size,
                output_audio_file);
        }
    }

    return 0;
}

static int32_t decode_packet(AVCodecContext *decode_ctx, const AVPacket *pkt) {
    int32_t result = 0;

    result = avcodec_send_packet(decode_ctx, pkt);
    if (result < 0) {
        std::cerr << "Error: avcodec_send_packet failed." << std::endl;
        return result;
    }

    while (result >= 0) {
        result = avcodec_receive_frame(decode_ctx, frame);
        if (result < 0) {
            if (result == AVERROR_EOF || result == AVERROR(EAGAIN)) {
                return 0;
            }
            std::cerr << "Error:Error during decoding:"
                      << std::string(av_err2str(result)) << std::endl;
            return result;
        }

        if (decode_ctx->codec->type == AVMEDIA_TYPE_VIDEO) {
            write_frame_to_yuv1(frame);
            std::cout << "Write frame to yuv file" << std::endl;
        } else {
            write_samples_to_pcm1(frame, audio_dec_ctx);
            std::cout << "Write sample to pcm file" << std::endl;
        }

        av_frame_unref(frame);
    }

    return result;
}

static int
get_format_from_sample_fmt(const char **fmt, enum AVSampleFormat sample_fmt) {
    struct sample_fmt_entry {
        enum AVSampleFormat sample_fmt;
        const char *fmt_bigendian;
        const char *fmt_littleendian;
    } sample_fmt_entries[] = {
        {AV_SAMPLE_FMT_U8, "u8", "u8"},
        {AV_SAMPLE_FMT_S16, "s16be", "s16le"},
        {AV_SAMPLE_FMT_S32, "s32be", "s32le"},
        {AV_SAMPLE_FMT_FLT, "f32be", "f32le"},
        {AV_SAMPLE_FMT_DBL, "f64be", "f64le"},
    };

    *fmt = nullptr;

    for (size_t i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
        struct sample_fmt_entry *entry = &sample_fmt_entries[i];
        if (sample_fmt == entry->sample_fmt) {
            // 小端还是大端
            *fmt = AV_NE(entry->fmt_bigendian, entry->fmt_littleendian);
            return 0;
        }
    }

    std::cerr << "sample format %s is not supported as output format\n"
              << av_get_sample_fmt_name(sample_fmt) << std::endl;

    return -1;
}

int32_t init_demuxer(
    const char *input_name,
    const char *video_output,
    const char *audio_output) {
    int32_t result =
        avformat_open_input(&format_ctx, input_name, nullptr, nullptr);
    if (result < 0) {
        std::cerr << "Error: avformat_open_input failed." << std::endl;
        return result;
    }

    result = avformat_find_stream_info(format_ctx, nullptr);
    if (result < 0) {
        std::cerr << "Error: avformat_find_stream_info failed." << std::endl;
        return result;
    }

    // video context
    result = open_codec_context(
        &video_stream_index, &video_dec_ctx, format_ctx, AVMEDIA_TYPE_VIDEO);
    if (result < 0) {
        std::cerr << "Error: open_codec_context failed." << std::endl;
        return result;
    }

    video_stream = format_ctx->streams[video_stream_index];
    output_video_file = fopen(video_output, "wb");
    if (!output_video_file) {
        std::cerr << "Error: failed to open video output file." << std::endl;
        return -1;
    }

    // audio context
    result = open_codec_context(
        &audio_stream_index, &audio_dec_ctx, format_ctx, AVMEDIA_TYPE_AUDIO);
    if (result < 0) {
        std::cerr << "Error: open_codec_context failed" << std::endl;
        return -1;
    }

    audio_stream = format_ctx->streams[audio_stream_index];
    output_audio_file = fopen(audio_output, "wb");
    if (!output_audio_file) {
        std::cerr << "Error: failed to open audio output file." << std::endl;
        return -1;
    }

    // print input file information
    av_dump_format(format_ctx, 0, input_name, 0);

    if (!video_stream && !audio_stream) {
        std::cerr
            << "Error: Could not find audio or video stream in the input, aborting "
            << std::endl;
        return -1;
    }

    av_init_packet(&pkt);
    pkt.data = nullptr;
    pkt.size = 0;

    frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "Error: Failed to alloc frame." << std::endl;
        return -1;
    }

    if (video_stream) {
        std::cout << "Demuxing video from file " << std::string(input_name)
                  << " into " << std::string(video_output) << std::endl;
    }
    if (audio_stream) {
        std::cout << "Demuxing audio from file " << std::string(input_name)
                  << " into " << std::string(audio_output) << std::endl;
    }

    return 0;
}

int32_t demuxing(const char *video_output_name, const char *audio_output_name) {
    int32_t result = 0;

    while (av_read_frame(format_ctx, &pkt) >= 0) {
        std::cout << "Read packet, pts:" << pkt.pts
                  << ", stream:" << pkt.stream_index << ", size:" << pkt.size
                  << std::endl;
        if (pkt.stream_index == audio_stream_index) {
            result = decode_packet(audio_dec_ctx, &pkt);
        } else if (pkt.stream_index == video_stream_index) {
            result = decode_packet(video_dec_ctx, &pkt);
        }
        av_packet_unref(&pkt);
        if (result < 0) { break; }
    }

    if (video_dec_ctx) decode_packet(video_dec_ctx, nullptr);
    if (audio_dec_ctx) decode_packet(audio_dec_ctx, nullptr);

    std::cout << "Demuxing succeeded." << std::endl;
    if (video_dec_ctx) {
        std::cout << "Play the output video file with the command:" << std::endl
                  << "   ffplay -f rawvideo -pix_fmt "
                  << std::string(av_get_pix_fmt_name(video_dec_ctx->pix_fmt))
                  << " -video_size " << video_dec_ctx->width << "x"
                  << video_dec_ctx->height << " "
                  << std::string(video_output_name) << std::endl;
    }
    if (audio_dec_ctx) {
        enum AVSampleFormat sfmt = audio_dec_ctx->sample_fmt;
        int n_channels = audio_dec_ctx->ch_layout.nb_channels;
        const char *fmt;

        if (av_sample_fmt_is_planar(sfmt)) {
            const char *packed = av_get_sample_fmt_name(sfmt);
            sfmt = av_get_packed_sample_fmt(sfmt);
            n_channels = 1;
        }
        result = get_format_from_sample_fmt(&fmt, sfmt);
        if (result < 0) { return -1; }
        std::cout << "Play the output video file with the command:" << std::endl
                  << "    ffplay -f " << std::string(fmt) << " -ac "
                  << n_channels << " -ar " << audio_dec_ctx->sample_rate << " "
                  << std::string(audio_output_name) << std::endl;
    }
    return 0;
}

void destroy_demuxer() {
    avcodec_free_context(&video_dec_ctx);
    avcodec_free_context(&audio_dec_ctx);
    avformat_close_input(&format_ctx);
    if (output_video_file != nullptr) {
        fclose(output_video_file);
        output_video_file = nullptr;
    }
    if (output_audio_file != nullptr) {
        fclose(output_audio_file);
        output_audio_file = nullptr;
    }
}