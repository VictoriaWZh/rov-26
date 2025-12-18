#include "../include/frame_recorder.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <stdlib.h>
#include <string.h>

struct frame_recorder {
    AVFormatContext* fmt_ctx;
    AVStream* stream;
    AVCodecContext* codec_ctx;
    AVPacket* pkt;
    uint64_t base_ts;
    int base_ts_set;
    AVRational time_base;
};

frame_recorder_t* frame_recorder_create(const char* filename,
                                         uint32_t width, uint32_t height,
                                         uint32_t fps_num, uint32_t fps_den) {
    frame_recorder_t* rec = calloc(1, sizeof(*rec));
    if (!rec) return NULL;
    
    rec->time_base = (AVRational){fps_num, fps_den};
    
    if (avformat_alloc_output_context2(&rec->fmt_ctx, NULL, "matroska", NULL) < 0) {
        free(rec);
        return NULL;
    }
    
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec) {
        avformat_free_context(rec->fmt_ctx);
        free(rec);
        return NULL;
    }
    
    rec->stream = avformat_new_stream(rec->fmt_ctx, codec);
    if (!rec->stream) {
        avformat_free_context(rec->fmt_ctx);
        free(rec);
        return NULL;
    }
    
    rec->stream->codecpar->codec_id = AV_CODEC_ID_MJPEG;
    rec->stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    rec->stream->codecpar->format = AV_PIX_FMT_YUVJ420P;
    rec->stream->codecpar->width = width;
    rec->stream->codecpar->height = height;
    rec->stream->time_base = rec->time_base;
    
    rec->codec_ctx = avcodec_alloc_context3(codec);
    if (!rec->codec_ctx) {
        avformat_free_context(rec->fmt_ctx);
        free(rec);
        return NULL;
    }
    
    rec->codec_ctx->time_base = rec->time_base;
    avcodec_parameters_to_context(rec->codec_ctx, rec->stream->codecpar);
    
    if (avcodec_open2(rec->codec_ctx, codec, NULL) < 0) {
        avcodec_free_context(&rec->codec_ctx);
        avformat_free_context(rec->fmt_ctx);
        free(rec);
        return NULL;
    }
    
    rec->pkt = av_packet_alloc();
    if (!rec->pkt) {
        avcodec_free_context(&rec->codec_ctx);
        avformat_free_context(rec->fmt_ctx);
        free(rec);
        return NULL;
    }
    
    if (!(rec->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&rec->fmt_ctx->pb, filename, AVIO_FLAG_WRITE) < 0) {
            av_packet_free(&rec->pkt);
            avcodec_free_context(&rec->codec_ctx);
            avformat_free_context(rec->fmt_ctx);
            free(rec);
            return NULL;
        }
    }
    
    if (avformat_write_header(rec->fmt_ctx, NULL) < 0) {
        avio_closep(&rec->fmt_ctx->pb);
        av_packet_free(&rec->pkt);
        avcodec_free_context(&rec->codec_ctx);
        avformat_free_context(rec->fmt_ctx);
        free(rec);
        return NULL;
    }
    
    return rec;
}

int frame_recorder_write(frame_recorder_t* rec, uint64_t timestamp_us,
                         const void* jpeg_data, size_t jpeg_len) {
    if (!rec || !jpeg_data || jpeg_len == 0) return -1;
    
    if (!rec->base_ts_set) {
        rec->base_ts = timestamp_us;
        rec->base_ts_set = 1;
    }
    
    uint64_t rel_ts = timestamp_us - rec->base_ts;
    
    rec->pkt->data = (uint8_t*)jpeg_data;
    rec->pkt->size = jpeg_len;
    rec->pkt->stream_index = rec->stream->index;
    rec->pkt->pts = av_rescale_q(rel_ts, (AVRational){1, 1000000}, rec->stream->time_base);
    rec->pkt->dts = rec->pkt->pts;
    rec->pkt->flags |= AV_PKT_FLAG_KEY;
    
    int ret = av_interleaved_write_frame(rec->fmt_ctx, rec->pkt);
    av_packet_unref(rec->pkt);
    
    return (ret >= 0) ? 0 : -1;
}

void frame_recorder_destroy(frame_recorder_t* rec) {
    if (!rec) return;
    
    av_write_trailer(rec->fmt_ctx);
    
    if (rec->codec_ctx) {
        avcodec_close(rec->codec_ctx);
        avcodec_free_context(&rec->codec_ctx);
    }
    
    if (rec->pkt) av_packet_free(&rec->pkt);
    
    if (rec->fmt_ctx) {
        if (!(rec->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&rec->fmt_ctx->pb);
        }
        avformat_free_context(rec->fmt_ctx);
    }
    
    free(rec);
}
