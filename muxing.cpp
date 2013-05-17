extern "C" 
{    
	#include <libavformat\avformat.h>
	#include <libavutil\mathematics.h>
	#include <libswscale\swscale.h>
}

#include <windows.h>
#include "Capture.h"
#include <time.h>  

#pragma comment(lib, "lib\\avcodec.lib")
#pragma comment(lib, "lib\\avformat.lib")
#pragma comment(lib, "lib\\swscale.lib")
#pragma comment(lib, "lib\\avutil.lib")

/* 5 seconds stream duration */
#define STREAM_DURATION   10.0
#define STREAM_FRAME_RATE 15 /* 25 images/s */
#define STREAM_NB_FRAMES  ((int)(STREAM_DURATION * STREAM_FRAME_RATE))
#define STREAM_PIX_FMT AV_PIX_FMT_YUV420P /* default pix_fmt */


static int sws_flags = SWS_BICUBIC;
static Capture capture = Capture(NULL);

/**************************************************************/
/* audio output */

static double t, tincr, tincr2;
static int16_t *samples;
static int audio_input_frame_size;

/* Add an output stream. */
static AVStream *add_stream(AVFormatContext *oc, AVCodec **codec,
                            enum AVCodecID codec_id)
{	
    AVCodecContext *c;
    AVStream *st;

    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        exit(1);
    }

    st = avformat_new_stream(oc, *codec);
    if (!st) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }
    st->id = oc->nb_streams-1;
    c = st->codec;

    switch ((*codec)->type) {
    case AVMEDIA_TYPE_AUDIO:
        st->id = 1;
        c->sample_fmt  = AV_SAMPLE_FMT_S16;
        c->bit_rate    = 64000;
        c->sample_rate = 44100;
        c->channels    = 2;
        break;

    case AVMEDIA_TYPE_VIDEO:
        c->codec_id = codec_id;

        c->bit_rate = 400000;
        /* Resolution must be a multiple of two. */
        c->width    = WIDTH;
        c->height   = HEIGHT;
        /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
        c->time_base.den = STREAM_FRAME_RATE;
        c->time_base.num = 1;
        c->gop_size      = 12; /* emit one intra frame every twelve frames at most */
        c->pix_fmt       = STREAM_PIX_FMT;
        if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            /* just for testing, we also add B frames */
            c->max_b_frames = 2;
        }
        if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
            /* Needed to avoid using macroblocks in which some coeffs overflow.
             * This does not happen with normal video, it just happens here as
             * the motion of the chroma plane does not match the luma plane. */
            c->mb_decision = 2;
        }
    break;

    default:
        break;
    }

    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    return st;
}

/**************************************************************/
/* audio output */


static void open_audio(AVFormatContext *oc, AVCodec *codec, AVStream *st)
{
    AVCodecContext *c;
    int ret;

    c = st->codec;

    /* open it */
    ret = avcodec_open2(c, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open audio codec: %s\n", av_err2str(ret));
        exit(1);
    }

    /* init signal generator */
    t     = 0;
    tincr = 2 * M_PI * 110.0 / c->sample_rate;
    /* increment frequency by 110 Hz per second */
    tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;

    if (c->codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE)
        audio_input_frame_size = 10000;
    else
        audio_input_frame_size = c->frame_size;
    samples = (int16_t*)av_malloc(audio_input_frame_size *
                        av_get_bytes_per_sample(c->sample_fmt) *
                        c->channels);
    if (!samples) {
        fprintf(stderr, "Could not allocate audio samples buffer\n");
        exit(1);
    }
}

/* Prepare a 16 bit dummy audio frame of 'frame_size' samples and
 * 'nb_channels' channels. */
static void get_audio_frame(int16_t *samples, int frame_size, int nb_channels)
{
    int j, i, v;
    int16_t *q;

    q = samples;
    for (j = 0; j < frame_size; j++) {
        v = (int)(sin(t) * 10000);
        for (i = 0; i < nb_channels; i++)
            *q++ = v;
        t     += tincr;
        tincr += tincr2;
    }
}

static void write_audio_frame(AVFormatContext *oc, AVStream *st)
{
    AVCodecContext *c;
    AVPacket pkt = { 0 }; // data and size must be 0;
    AVFrame *frame = avcodec_alloc_frame();
    int got_packet, ret;

    av_init_packet(&pkt);
    c = st->codec;

    get_audio_frame(samples, audio_input_frame_size, c->channels);
    frame->nb_samples = audio_input_frame_size;
    avcodec_fill_audio_frame(frame, c->channels, c->sample_fmt,
                             (uint8_t *)samples,
                             audio_input_frame_size *
                             av_get_bytes_per_sample(c->sample_fmt) *
                             c->channels, 1);

    ret = avcodec_encode_audio2(c, &pkt, frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "Error encoding audio frame: %s\n", av_err2str(ret));
        exit(1);
    }

    if (!got_packet)
        return;

    pkt.stream_index = st->index;

    /* Write the compressed frame to the media file. */
    ret = av_interleaved_write_frame(oc, &pkt);
    if (ret != 0) {
        fprintf(stderr, "Error while writing audio frame: %s\n",
                av_err2str(ret));
        exit(1);
    }
    avcodec_free_frame(&frame);
}

static void close_audio(AVFormatContext *oc, AVStream *st)
{
    avcodec_close(st->codec);

    av_free(samples);
}

/**************************************************************/
/* video output */

static AVFrame *frame;
static AVPicture src_picture, dst_picture, outputAVPicture;
static int frame_count;
static LPBYTE screenBuffer; //our screen's buffer

//initializtion of screen image buffer and dst_picture
static bool InitBuffers(unsigned int bufferSize)
{
	//screenBuffer = (LPBYTE)GlobalAlloc(GMEM_FIXED, bufferSize*16);
	screenBuffer = (LPBYTE)malloc(bufferSize);
	avpicture_alloc(&outputAVPicture, STREAM_PIX_FMT, WIDTH, HEIGHT);
	if (!screenBuffer) 
	{
		return false;
	}
	return true;
}

static void open_video(AVFormatContext *oc, AVCodec *codec, AVStream *st)
{
    int ret;
    AVCodecContext *c = st->codec;

    /* open the codec */
    ret = avcodec_open2(c, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
        exit(1);
    }

    /* allocate and init a re-usable frame */
    frame = avcodec_alloc_frame();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    /* Allocate the encoded raw picture. */
    ret = avpicture_alloc(&dst_picture, c->pix_fmt, c->width, c->height);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate picture: %s\n", av_err2str(ret));
        exit(1);
    }

    /* If the output format is not YUV420P, then a temporary YUV420P
     * picture is needed too. It is then converted to the required
     * output format. */
    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        ret = avpicture_alloc(&src_picture, AV_PIX_FMT_YUV420P, c->width, c->height);
        if (ret < 0) {
            fprintf(stderr, "Could not allocate temporary picture: %s\n",
                    av_err2str(ret));
            exit(1);
        }
    }

    /* copy data and linesize picture pointers to frame */
    *((AVPicture *)frame) = dst_picture;
}

static void fill_yuv_image(AVPicture *pict, int frame_index, int width, int height)
{
	BITMAP bitmap;
	PBITMAPINFO pBitmapInfo; 
	WORD cClrBits; 
	
	capture.TakePic(0,0, HEIGHT, WIDTH);
	GetObject(capture.hbmScreen, sizeof(BITMAP), (LPVOID)&bitmap);	
	int in_width = bitmap.bmWidth;
	int in_height = bitmap.bmHeight;
	int out_width = bitmap.bmWidth;
	int out_height =  bitmap.bmHeight;
	cClrBits = (WORD)(bitmap.bmPlanes * bitmap.bmBitsPixel); 
	if (cClrBits == 1) 
		cClrBits = 1; 
	else if (cClrBits <= 4) 
		cClrBits = 4; 
	else if (cClrBits <= 8) 
		cClrBits = 8; 
	else if (cClrBits <= 16) 
		cClrBits = 16; 
	else if (cClrBits <= 24) 
		cClrBits = 24; 
	else cClrBits = 32; 
	// Allocate memory for the BITMAPINFO structure.
	if (cClrBits != 24) 
		pBitmapInfo = (PBITMAPINFO) LocalAlloc(LPTR, sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * (1<< cClrBits)); 
	else 
		pBitmapInfo = (PBITMAPINFO) LocalAlloc(LPTR, sizeof(BITMAPINFOHEADER)); 

	// Initialize the fields in the BITMAPINFO structure. 
	pBitmapInfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER); 
	pBitmapInfo->bmiHeader.biWidth = bitmap.bmWidth; 
	pBitmapInfo->bmiHeader.biHeight = bitmap.bmHeight; 
	pBitmapInfo->bmiHeader.biPlanes = bitmap.bmPlanes; 
	pBitmapInfo->bmiHeader.biBitCount = bitmap.bmBitsPixel;
	 
	if (cClrBits < 24) 
		pBitmapInfo->bmiHeader.biClrUsed = (1<<cClrBits); 
//If the bitmap is not compressed, set the BI_RGB flag. 
	pBitmapInfo->bmiHeader.biCompression = BI_RGB; 
	// Compute the number of bytes in the array of color 
	// indices and store the result in biSizeImage. 
	//pBitmapInfo->bmiHeader.biSizeImage = (pBitmapInfo->bmiHeader.biWidth + 7) /8 * pBitmapInfo->bmiHeader.biHeight * cClrBits; 
	// Set biClrImportant to 0, indicating that all of the 
	// device colors are important. 
	pBitmapInfo->bmiHeader.biClrImportant = 0; 
	// now open file and save the data
	
	
	if (!GetDIBits(capture.hdcScreen, HBITMAP(capture.hbmScreen), 0, (WORD)in_height, screenBuffer, pBitmapInfo, 
		DIB_RGB_COLORS))
	{
			return;
	}		

	
	//SwsContext* fooContext = sws_getContext(in_width, in_height, AV_PIX_FMT_RGB32, out_width, out_height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL,  sws_getDefaultFilter 	(10, 10, 10 , 10, 10, 10, 10), NULL);
	SwsContext* fooContext = sws_getContext(in_width, in_height, AV_PIX_FMT_RGB32, out_width, out_height, AV_PIX_FMT_YUV420P,SWS_FAST_BILINEAR , NULL,  NULL, NULL);
	
	uint8_t *input = reinterpret_cast<uint8_t *>(screenBuffer);
	
	int linesize[] = {(int)bitmap.bmWidthBytes, 0,0 ,0 ,0 ,0 ,0 ,0};
	sws_scale(fooContext, &input, linesize, 0, out_height, outputAVPicture.data, outputAVPicture.linesize);	
	
	int x, y;
	for (y = 0; y <= height; y++)
		for (x = 0; x <= width; x++)
			pict->data[0][y * pict->linesize[0] + x] = outputAVPicture.data[0][(height - y) * outputAVPicture.linesize[0] + x];    
	for (y = 0; y <= height / 2; y++) {
		for (x = 0; x <= width / 2; x++) {
			pict->data[1][y * pict->linesize[1] + x] =  outputAVPicture.data[1][(height/2 - y) * outputAVPicture.linesize[1] + x];
			pict->data[2][y * pict->linesize[2] + x] =  outputAVPicture.data[2][(height/2 - y) * outputAVPicture.linesize[2] + x];
		}
	}	

	


}



static void write_video_frame(AVFormatContext *oc, AVStream *st)
{
    int ret;
    static struct SwsContext *sws_ctx;
    AVCodecContext *c = st->codec;	
    if (frame_count >= STREAM_NB_FRAMES) {
        /* No more frames to compress. The codec has a latency of a few
         * frames if using B-frames, so we get the last frames by
         * passing the same picture again. */
    } else {
        if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
            /* as we only generate a YUV420P picture, we must convert it
             * to the codec pixel format if needed */
            if (!sws_ctx) {
                sws_ctx = sws_getContext(c->width, c->height, AV_PIX_FMT_YUV420P,
                                         c->width, c->height, c->pix_fmt,
                                         sws_flags, NULL, NULL, NULL);
                if (!sws_ctx) {
                    fprintf(stderr,
                            "Could not initialize the conversion context\n");
                    exit(1);
                }
            }
            fill_yuv_image(&src_picture, frame_count, c->width, c->height);
            sws_scale(sws_ctx,
                      (const uint8_t * const *)src_picture.data, src_picture.linesize,
                      0, c->height, dst_picture.data, dst_picture.linesize);
        } else {
            fill_yuv_image(&dst_picture, frame_count, c->width, c->height);
        }
    }

    if (oc->oformat->flags & AVFMT_RAWPICTURE) {
        /* Raw video case - directly store the picture in the packet */
        AVPacket pkt;
        av_init_packet(&pkt);

        pkt.flags        |= AV_PKT_FLAG_KEY;
        pkt.stream_index  = st->index;
        pkt.data          = dst_picture.data[0];
        pkt.size          = sizeof(AVPicture);

        ret = av_interleaved_write_frame(oc, &pkt);
    } else {
        AVPacket pkt = { 0 };
        int got_packet;
        av_init_packet(&pkt);

        /* encode the image */
        ret = avcodec_encode_video2(c, &pkt, frame, &got_packet);
        if (ret < 0) {
            fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
            exit(1);
        }
        /* If size is zero, it means the image was buffered. */

        if (!ret && got_packet && pkt.size) {
            pkt.stream_index = st->index;

            /* Write the compressed frame to the media file. */
            ret = av_interleaved_write_frame(oc, &pkt);
        } else {
            ret = 0;
        }
    }
    if (ret != 0) {
        fprintf(stderr, "Error while writing video frame: %s\n", av_err2str(ret));
        exit(1);
    }
    frame_count++;
}

static void close_video(AVFormatContext *oc, AVStream *st)
{
    avcodec_close(st->codec);
    av_free(src_picture.data[0]);
    av_free(dst_picture.data[0]);
    av_free(frame);
}

/**************************************************************/
/* media file output */

int main(int argc, char **argv)
{
    const char *filename;
    AVOutputFormat *fmt;
    AVFormatContext *oc;
    AVStream *audio_st, *video_st;
    AVCodec *audio_codec, *video_codec;
    double audio_pts, video_pts;
    int ret;

	time_t startTime, finishTime;
	time(&startTime);

	InitBuffers(WIDTH*HEIGHT*RGB_PLANES);
    /* Initialize libavcodec, and register all codecs and formats. */
    av_register_all();   

    filename = "test.mp4";

    /* allocate the output media context */
    avformat_alloc_output_context2(&oc, NULL, NULL, filename);
    if (!oc) {
        printf("Could not deduce output format from file extension: using MPEG.\n");
        avformat_alloc_output_context2(&oc, NULL, "mpeg", filename);
    }
    if (!oc) {
        return 1;
    }
    fmt = oc->oformat;

    /* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    video_st = NULL;
    audio_st = NULL;

    if (fmt->video_codec != AV_CODEC_ID_NONE) {
        video_st = add_stream(oc, &video_codec, fmt->video_codec);
    }
    if (fmt->audio_codec != AV_CODEC_ID_NONE) {
        audio_st = add_stream(oc, &audio_codec, fmt->audio_codec);
    }

    /* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
    if (video_st)
        open_video(oc, video_codec, video_st);
    if (audio_st)
        open_audio(oc, audio_codec, audio_st);

    av_dump_format(oc, 0, filename, 1);

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", filename,
                    av_err2str(ret));
            return 1;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(oc, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n",
                av_err2str(ret));
        return 1;
    }

    if (frame)
        frame->pts = 0;
    for (;;) {
        /* Compute current audio and video time. */
        if (audio_st)
            audio_pts = (double)audio_st->pts.val * audio_st->time_base.num / audio_st->time_base.den;
        else
            audio_pts = 0.0;

        if (video_st)
            video_pts = (double)video_st->pts.val * video_st->time_base.num /
                        video_st->time_base.den;
        else
            video_pts = 0.0;

        if ((!audio_st || audio_pts >= STREAM_DURATION) &&
            (!video_st || video_pts >= STREAM_DURATION))
            break;

        /* write interleaved audio and video frames */
        if (!video_st || (video_st && audio_st && audio_pts < video_pts)) {
            write_audio_frame(oc, audio_st);
        } else {
            write_video_frame(oc, video_st);
            frame->pts += av_rescale_q(1, video_st->codec->time_base, video_st->time_base);
		//	Sleep(55);
        }
    }

    /* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(oc);

    /* Close each codec. */
    if (video_st)
        close_video(oc, video_st);
    if (audio_st)
        close_audio(oc, audio_st);

    if (!(fmt->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_close(oc->pb);

    /* free the stream */
    avformat_free_context(oc);	
	time(&finishTime);
	printf ("\n\n\nTotal time: %.f seconds\n", difftime(finishTime,startTime));
    return 0;
}
