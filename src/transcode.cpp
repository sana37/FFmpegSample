extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>

#include <stdio.h>
#include <wchar.h>
}

typedef struct SwsContext SwsContext;

void setupRS(const char *, int, int);
void releaseRS(void);
void filterFrame(AVFrame *);

int onFrameDecoded(int);
int encodeFrame(AVFrame *);
int setupDecoding(const char *);
int setupEncoding(const char *);
int transcodeTest(char *, char *);

AVIOContext     *ioCtxEnc;

AVFormatContext *fmtCtxDec;
AVFormatContext *fmtCtxEnc;

AVCodecContext  *codecCtxDec;
AVCodecContext  *codecCtxEnc;

AVStream        *videoStrmDec;
AVStream        *videoStrmEnc;

SwsContext      *swsCtxDec;
SwsContext      *swsCtxEnc;

AVFrame         *frameDec;
AVFrame         *frameRGBA;
AVFrame         *frameEnc;


void filterFrame(AVFrame *frame) {
  // do something
}

int onFrameDecoded(int frameIndex) {
  printf("sws_scale to RGBA %d", frameIndex);
  // Convert the image from its native format to RGBA
  sws_scale
  (
    swsCtxDec,
    (uint8_t const * const *) frameDec->data,
    frameDec->linesize,
    0,
    frameDec->height,
    frameRGBA->data,
    frameRGBA->linesize
  );

  printf("filtering frame %d", frameIndex);
  filterFrame(frameRGBA);

  printf("sws_scale from RGBA %d", frameIndex);
  // Convert the image from RGBA to YUV420p?.
  sws_scale
  (
    swsCtxEnc,
    (uint8_t const * const *) frameRGBA->data,
    frameRGBA->linesize,
    0,
    frameRGBA->height,
    frameEnc->data,
    frameEnc->linesize
  );
  /**
   * Encode
   */
  printf("encode the frame %d", frameIndex);
  int64_t pts = av_frame_get_best_effort_timestamp(frameDec);
  frameEnc->pts = av_rescale_q(pts, videoStrmDec->time_base, codecCtxEnc->time_base);
  frameEnc->key_frame = 0;
  frameEnc->pict_type = AV_PICTURE_TYPE_NONE;

  if (encodeFrame(frameEnc) != 0) {
    fprintf(stderr, "encodeFrame failed...");
    return -1;
  }
  printf("encoding frame %d has done.", frameIndex);

  return 0;
}

/**
 * if frame is null, this function flush encoder
 */
int encodeFrame(AVFrame *frame) {
  if (avcodec_send_frame(codecCtxEnc, frame) != 0) {
    fprintf(stderr, "avcodec_send_frame failed");
    return -1;
  }

  AVPacket pktEnc;
  av_init_packet(&pktEnc);
  pktEnc.data = NULL;
  pktEnc.size = 0;
  while (1) {
    int result = avcodec_receive_packet(codecCtxEnc, &pktEnc);
    if (result == AVERROR(EAGAIN)) {
      printf("タリナイ．．．モット．．．ふれえむヲ．．．");
      break;
    } else if (result == AVERROR_EOF) {
      printf("the encoder has been fully flushed.");
      break;
    } else if (result < 0) {
      fprintf(stderr, "error during encoding.  receive_packet failed.");
      return -1;
    }
    printf("食事中です。話しかけないでください");
    pktEnc.stream_index = videoStrmEnc->index;
    av_packet_rescale_ts(&pktEnc, codecCtxEnc->time_base, videoStrmEnc->time_base);

    if (av_interleaved_write_frame(fmtCtxEnc, &pktEnc) != 0) {
      fprintf(stderr, "av_interleaved_write_frame failed\n");
      return -1;
    }
  }

  return 0;
}

int setupDecoding(const char *srcFileName) {
  // Open decode file
  fmtCtxDec = NULL;
  if (avformat_open_input(&fmtCtxDec, srcFileName, NULL, NULL) != 0)
    return -1; // Couldn't open file

  // Retrieve stream information
  if (avformat_find_stream_info(fmtCtxDec, NULL) < 0)
    return -1; // Couldn't find stream information

  // Dump information about file onto standard error
  av_dump_format(fmtCtxDec, 0, srcFileName, 0);

  // Find the first video stream
  videoStrmDec = NULL;
  int i;
  for (i = 0; i < fmtCtxDec->nb_streams; i++) {
    if (fmtCtxDec->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      videoStrmDec = fmtCtxDec->streams[i];
      break;
    }
  }
  if (videoStrmDec == NULL)
    return -1; // Didn't find a video stream

  // Find the decoder for the video stream
  AVCodec *codecDec = avcodec_find_decoder(videoStrmDec->codecpar->codec_id);
  if (codecDec == NULL) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1; // Codec not found
  }

  // Alloc codec context
  codecCtxDec = avcodec_alloc_context3(codecDec);
  if (codecCtxDec == NULL) {
    fprintf(stderr, "avcodec_alloc_context3 failed\n");
    return -1;
  }

  // Open codec
  if (avcodec_parameters_to_context(codecCtxDec, videoStrmDec->codecpar) < 0) {
    fprintf(stderr, "avcodec_parameters_to_context failed\n");
    return -1;
  }
  AVDictionary *dictDec = NULL;
  if (avcodec_open2(codecCtxDec, codecDec, &dictDec)<0)
    return -1; // Could not open codec

  //get the scaling context
  swsCtxDec = sws_getContext
    (
        codecCtxDec->width,
        codecCtxDec->height,
        codecCtxDec->pix_fmt,
        codecCtxDec->width,
        codecCtxDec->height,
        AV_PIX_FMT_RGBA,
        SWS_BICUBIC,
        NULL,
        NULL,
        NULL
    );
  if (swsCtxDec == NULL) {
    fprintf(stderr, "sws_getContext failed...");
    return -1;
  }

  return 0;
}

int setupEncoding(const char *dstFileName) {
  // Open encode file
  ioCtxEnc = NULL;
  if (avio_open(&ioCtxEnc, dstFileName, AVIO_FLAG_WRITE) < 0) {
    fprintf(stderr, "encoded file open failed\n");
    return -1;
  }

  fmtCtxEnc = NULL;
  if (avformat_alloc_output_context2(&fmtCtxEnc, NULL, "mp4", NULL) < 0) {
    fprintf(stderr, "avformat_alloc_output_context2 failed\n");
    return -1;
  }

  fmtCtxEnc->pb = ioCtxEnc;

  AVCodec *codecEnc = avcodec_find_encoder(AV_CODEC_ID_H264);
  if (codecEnc == NULL) {
    fprintf(stderr, "encoder not found ...\n");
    return -1;
  }

  codecCtxEnc = avcodec_alloc_context3(codecEnc);
  if (codecCtxEnc == NULL) {
    fprintf(stderr, "avcodec_alloc_context3 failed\n");
    return -1;
  }

  if (codecEnc->pix_fmts)
    codecCtxEnc->pix_fmt = codecEnc->pix_fmts[0];
  else
    codecCtxEnc->pix_fmt = codecCtxDec->pix_fmt;
  printf("pix fmt: %d, %d", codecCtxDec->pix_fmt, codecCtxEnc->pix_fmt);

  // set picture properties
  codecCtxEnc->width = codecCtxDec->width;
  codecCtxEnc->height = codecCtxDec->height;
  codecCtxEnc->field_order = AV_FIELD_PROGRESSIVE;
  codecCtxEnc->color_range = codecCtxDec->color_range;
  codecCtxEnc->color_primaries = codecCtxDec->color_primaries;
  codecCtxEnc->color_trc = codecCtxDec->color_trc;
  codecCtxEnc->colorspace = codecCtxDec->colorspace;
  codecCtxEnc->chroma_sample_location = codecCtxDec->chroma_sample_location;
  codecCtxEnc->sample_aspect_ratio = codecCtxDec->sample_aspect_ratio;
  codecCtxEnc->profile = FF_PROFILE_H264_BASELINE;

  // set timebase
  codecCtxEnc->time_base = videoStrmDec->time_base;

  // generate global header when the format require it
  if (fmtCtxEnc->oformat->flags & AVFMT_GLOBALHEADER) {
    codecCtxEnc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  // make codec options
  AVDictionary *dictEnc = NULL;
  av_dict_set(&dictEnc, "preset", "medium", 0);
  av_dict_set(&dictEnc, "crf", "22", 0);
  av_dict_set(&dictEnc, "profile", "baseline", 0);
  av_dict_set(&dictEnc, "level", "4.0", 0);

  if (avcodec_open2(codecCtxEnc, codecCtxEnc->codec, &dictEnc) != 0) {
    fprintf(stderr, "avcodec_open2 failed\n");
    return -1;
  }

  int i;
  for (i = 0; i < fmtCtxDec->nb_streams; i++) {
    AVStream *strmDec = fmtCtxDec->streams[i];

    if (i == videoStrmDec->index) {
      // create video stream
      videoStrmEnc = avformat_new_stream(fmtCtxEnc, codecEnc);
      if (videoStrmEnc == NULL) {
        fprintf(stderr, "avformat_new_stream failed");
        return -1;
      }

      videoStrmEnc->sample_aspect_ratio = codecCtxEnc->sample_aspect_ratio;
      videoStrmEnc->time_base = codecCtxEnc->time_base;

      if (avcodec_parameters_from_context(videoStrmEnc->codecpar, codecCtxEnc) < 0) {
        fprintf(stderr, "avcodec_parameters_from_context failed");
        return -1;
      }

      AVDictionary *videoDictDec = videoStrmDec->metadata;
      AVDictionary *videoDictEnc = NULL;
      av_dict_copy(&videoDictEnc, videoDictDec, 0);
      videoStrmEnc->metadata = videoDictEnc;

    } else if (strmDec->codecpar->codec_type == AVMEDIA_TYPE_UNKNOWN) {
      fprintf(stderr, "Elementary stream %d is of unknown type, cannot proceed.", i);
      return -1;
    } else {
      // create audio stream
      AVStream *strmEnc = avformat_new_stream(fmtCtxEnc, NULL);
      if (strmEnc == NULL) {
        fprintf(stderr, "avformat_new_stream failed");
        return -1;
      }
      if (avcodec_parameters_copy(strmEnc->codecpar, strmDec->codecpar) < 0) {
        fprintf(stderr, "Copying parameters for stream %d failed.", i);
        return -1;
      }
      strmEnc->time_base = strmDec->time_base;
    }
  }

  if (avformat_write_header(fmtCtxEnc, NULL) < 0) {
    fprintf(stderr, "avformat_write_header failed\n");
    return -1;
  }

  swsCtxEnc = sws_getContext
    (
        codecCtxEnc->width,
        codecCtxEnc->height,
        AV_PIX_FMT_RGBA,
        codecCtxEnc->width,
        codecCtxEnc->height,
        codecCtxEnc->pix_fmt,
        SWS_BICUBIC,
        NULL,
        NULL,
        NULL
    );
  if (swsCtxEnc == NULL) {
    fprintf(stderr, "sws_getContext failed...");
    return -1;
  }

  printf("setup encoding has done.");

  return 0;
}

int setupAVFrames(void) {
  // Allocate video frame
  frameDec = av_frame_alloc();

  // Initialize frame
  frameRGBA = av_frame_alloc();
  if (frameRGBA == NULL) {
    fprintf(stderr, "av_frame_alloc failed....");
    return -1;
  }

  frameRGBA->format = AV_PIX_FMT_RGBA;
  frameRGBA->width  = codecCtxDec->width;
  frameRGBA->height = codecCtxDec->height;

  if (av_frame_get_buffer(frameRGBA, 0) < 0) {
    fprintf(stderr, "av_frame_get_buffer failed...");
    return -1;
  }

  // Initialize frame
  frameEnc = av_frame_alloc();
  if (frameEnc == NULL) {
    fprintf(stderr, "av_frame_alloc failed....");
    return -1;
  }

  frameEnc->format = codecCtxEnc->pix_fmt;
  frameEnc->width  = codecCtxEnc->width;
  frameEnc->height = codecCtxEnc->height;

  if (av_frame_get_buffer(frameEnc, 0) < 0) {
    fprintf(stderr, "av_frame_get_buffer failed...");
    return -1;
  }

}

/**
 * main function
 */
int transcodeTest(char *srcFileName, char *dstFileName) {

  printf("start decoding and encoding\n");

  // Register all formats and codecs
  av_register_all();

  /**
   * setup
   */
  if (setupDecoding(srcFileName) != 0) {
    fprintf(stderr, "setup decoding failed...");
    return -1;
  }
  if (setupEncoding(dstFileName) != 0) {
    fprintf(stderr, "setup encoding failed...");
    return -1;
  }
  if (setupAVFrames() != 0) {
    fprintf(stderr, "setup AVFrames failed...");
    return -1;
  }

  /**
   * convert
   */
  printf("converting starts.");

  int frameIndex = 0;
  AVPacket pktDec;
  while (av_read_frame(fmtCtxDec, &pktDec) >= 0) {
    int strmIdx = pktDec.stream_index;
    // Is this a packet from the video stream?
    if (strmIdx == videoStrmDec->index) {
      // Decode video frame
      if (avcodec_send_packet(codecCtxDec, &pktDec) != 0) {
        fprintf(stderr, "avcodec_send_packet failed\n");
        return -1;
      }
      while (avcodec_receive_frame(codecCtxDec, frameDec) == 0) {
        if (onFrameDecoded(frameIndex) != 0)
          return -1;
        frameIndex++;
      }
    } else {
      // Remux this frame without reencoding
      av_packet_rescale_ts(&pktDec, fmtCtxDec->streams[strmIdx]->time_base, fmtCtxEnc->streams[strmIdx]->time_base);
      if (av_interleaved_write_frame(fmtCtxEnc, &pktDec) < 0) {
        fprintf(stderr, "av_interleaved_write_frame without reencoding failed...");
        return -1;
      }
    }
    // Free the packet that was allocated by av_read_frame
    av_packet_unref(&pktDec);
  }

  printf("converting finished.");

  // flush decoder
  printf("flush decoder");
  if (avcodec_send_packet(codecCtxDec, NULL) != 0) {
    fprintf(stderr, "flush avcodec_send_packet failed\n");
    return -1;
  }
  while (avcodec_receive_frame(codecCtxDec, frameDec) == 0) {
    if (onFrameDecoded(frameIndex) != 0)
      return -1;
    frameIndex++;
  }

  // flush encoder
  printf("flush encoder");
  if (encodeFrame(NULL) != 0) {
    fprintf(stderr, "flush encoder failed...");
    return -1;
  }

  printf("write trailer");
  if (av_write_trailer(fmtCtxEnc) != 0) {
    fprintf(stderr, "av_write_trailer failed\n");
    return -1;
  }


  /**
   * Release resource about decoding
   */

  // Free the RGB image
  av_frame_free(&frameRGBA);

  // Free the YUV frame
  av_frame_free(&frameDec);

  // Close the codec
  avcodec_free_context(&codecCtxDec);

  // Close the video file
  avformat_close_input(&fmtCtxDec);


  /**
   * Release resource about encoding
   */
  av_frame_free(&frameEnc);
  avcodec_close(codecCtxEnc);
  avcodec_free_context(&codecCtxEnc);
  avformat_free_context(fmtCtxEnc);
  avio_closep(&ioCtxEnc);

  printf("closed resources");

  return 0;
}
