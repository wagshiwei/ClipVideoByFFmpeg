
extern "C" {
#include <stdint.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <errno.h>
}

#include <vector>

class VClip {

	int64_t videoStartTime = -1;

	double vfrom;
	double vto;
	std::vector<AVPacket *>AudioPktList;

	int64_t currentVideoTime = -1;

	AVRational audioInTimeBase;
	AVRational videoInTimeBase;

	AVRational audioOutTimeBase;
	AVRational videoOutTimeBase;
	AVFormatContext * outFormat;
	AVFormatContext *inFormat;


	AVStream* inStream;
	AVStream* outStream;
	AVStream *inAudioStream;
	AVStream *outAudioStream;


	int videoindex = -1;
	int audioindex = -1;
	int outVideoIndex = -1;
	int outAudioIndex = -1;
	int64_t small_pts = -1;
	struct SwsContext *img_convert_ctx;
public:
	VClip() {
	
	}
	~VClip() {
		for (int i = 0; i < AudioPktList.size();i++) {
			AVPacket *pkt = AudioPktList[i];
			av_free_packet(pkt);
			delete pkt;
		}

		sws_freeContext(img_convert_ctx);
	}

	void writeAudio() {
		int loop = true;
		do {
			if (AudioPktList.size() <= 0) {
				loop = false;
			}
			else {
				AVPacket *pkt = AudioPktList[0];

				if (av_compare_ts(pkt->pts, audioInTimeBase, currentVideoTime, videoInTimeBase) < 0) {

					pkt->pts = av_rescale_q_rnd(pkt->pts - videoStartTime, audioInTimeBase, audioOutTimeBase, AV_ROUND_NEAR_INF);
					pkt->dts = av_rescale_q_rnd(pkt->dts - videoStartTime, audioInTimeBase, audioOutTimeBase, AV_ROUND_NEAR_INF);
					pkt->duration = av_rescale_q(pkt->duration, audioInTimeBase, audioOutTimeBase);
					int ret = av_interleaved_write_frame(outFormat, pkt);
					AudioPktList.erase(AudioPktList.begin());

					av_free_packet(pkt);
					delete pkt;
				}
				else {
					loop = false;
				}
			}
		} while (loop);
	}

	void write_packet( AVPacket &tmppkt) {

		int64_t pts = tmppkt.pts;
		int64_t dts = tmppkt.dts;
		tmppkt.pts = av_rescale_q(tmppkt.pts - videoStartTime, inStream->time_base, outStream->time_base);
		tmppkt.dts = av_rescale_q(tmppkt.dts - videoStartTime, inStream->time_base, outStream->time_base);
		tmppkt.duration = av_rescale_q(tmppkt.duration, inStream->time_base, outStream->time_base);

		currentVideoTime = pts;
		//int loop = false;
		/*do {
			AVPacket *pkt = AudioPktList[0];
				av_compare_ts(pkt->pts,);
		} while (loop);*/
		writeAudio();
		int ret = av_interleaved_write_frame(outFormat, &tmppkt);
		writeAudio();
		if (ret < 0) {
			if ((tmppkt.pts == AV_NOPTS_VALUE)) {
				printf("tmppkt.pts AV_NOPTS_VALUE\n");
			}
			printf("write_packet av_interleaved_write_frame ret<0\n");
		}

	}

	void FlushEncode( AVCodecContext* video_enc_ctx) {

		if (!(video_enc_ctx->codec->capabilities&CODEC_CAP_DELAY)) {
			return;
		}

		AVPacket tmppkt;// = new AVPacket
		av_init_packet(&tmppkt);
		int ret = 0;
		int got_pic;
		do {
			//av_init_packet(&tmppkt);
			tmppkt.data = NULL;
			tmppkt.size = 0;
			ret = avcodec_encode_video2(video_enc_ctx,
				&tmppkt, NULL, &got_pic);


			if (ret >= 0 && got_pic && (tmppkt.pts != AV_NOPTS_VALUE)) {
				write_packet( tmppkt);
				
				av_free_packet(&tmppkt);
			}
			if (!got_pic) {
				break;
			}
		} while (ret >= 0);
	}

	void FlushDecode( AVFrame*pFrame) {
		AVPacket tmppkt;// = new AVPacket
		av_init_packet(&tmppkt);
		int ret = 0;
		int got_pic;
		AVPacket outpkt;
		av_init_packet(&outpkt);
		int outret = 0;

		AVCodecContext*video_dec_ctx = inStream->codec;
		AVCodecContext* video_enc_ctx = outStream->codec;
		do {
			tmppkt.data = nullptr;
			tmppkt.size = 0;

			ret = avcodec_decode_video2(video_dec_ctx, pFrame, &got_pic, &tmppkt);
			if (ret >= 0 && got_pic) {
				//pFrame->pts = pFrame->pkt_pts;// av_rescale_q(pFrame->pkt_pts, inStream->time_base, outStream->time_base);
				pFrame->pts = pFrame->pkt_pts;// -videoStartTime;
				if (pFrame->pkt_pts*av_q2d(inStream->time_base) < vfrom || pFrame->pkt_pts*av_q2d(inStream->time_base) > vto) {
					return;
				}
				//av_init_packet(&outpkt);
				outpkt.data = nullptr;
				outpkt.size = 0;
				outret = avcodec_encode_video2(video_enc_ctx, &outpkt, pFrame, &got_pic);
				if (outret >= 0 && got_pic && (tmppkt.pts != AV_NOPTS_VALUE)) {

					write_packet( outpkt);
					
					av_free_packet(&outpkt);
				}
			}
			if (!got_pic) {
				break;
			}
		} while (ret >= 0);

		FlushEncode( video_enc_ctx);
	}

	

	void encodeAframe( AVFrame*pFrame, AVPacket *pkt) {

		AVCodecContext*video_dec_ctx = inStream->codec;
		AVCodecContext* video_enc_ctx = outStream->codec;

		int got_picture = 0, ret = 0;
		int size = video_enc_ctx->width*video_enc_ctx->height * 3 / 2;
		AVPacket tmppkt;// = new AVPacket;
		av_init_packet(&tmppkt);

		ret = avcodec_decode_video2(video_dec_ctx, pFrame, &got_picture, pkt);
		if (ret < 0)
		{
			//delete pkt;
			//return 0;
		}
		pFrame->pts = pFrame->pkt_pts;//ts++;

		if ((*pkt).pts*av_q2d(inStream->time_base) < vfrom) {
			return;
		}

		if (got_picture)
		{
			double sec = pFrame->pkt_pts*av_q2d(inStream->time_base);
			if (pFrame->pkt_pts*av_q2d(inStream->time_base) < vfrom || pFrame->pkt_pts*av_q2d(inStream->time_base) > vto) {
				return;
			}
			if (videoStartTime < 0) {
				videoStartTime = pFrame->pkt_pts - 100;
			}
			pFrame->pts = pFrame->pkt_pts;// -videoStartTime;
			//AVPicture pic;
			//pFrame->pkt_dts = 0;
			//memset(tmppkt->data, 0, size);
			//tmppkt->size = size;
			av_init_packet(&tmppkt);
			tmppkt.data = nullptr;
			tmppkt.size = 0;
			tmppkt.dts = 0;
			tmppkt.pts = 0;
			if (pFrame->pict_type == AV_PICTURE_TYPE_B) {
				pFrame->pict_type = AV_PICTURE_TYPE_I;
			}
			if (small_pts<0 || small_pts> pFrame->pkt_pts) {
				small_pts = pFrame->pkt_pts;
			}
			printf("avcodec_encode_video2  %#x  %#x\n", pFrame->pkt_pts, pFrame->pkt_dts);
			ret = avcodec_encode_video2(video_enc_ctx, &tmppkt, pFrame, &got_picture); //AV_PICTURE_TYPE_B
			if (tmppkt.flags&AV_PKT_FLAG_KEY) {
				tmppkt.dts = tmppkt.pts;
			}
			if (ret < 0)
			{
				//avio_close(pOFormat->pb);
				printf("avcodec_encode_video2 err");
				//return 0;
			}

			if ((tmppkt.pts == AV_NOPTS_VALUE)) {
				printf("tmppkt.pts AV_NOPTS_VALUE\n");
			}

			if (ret >= 0 && (!(tmppkt.pts == AV_NOPTS_VALUE)))
			{
				if (got_picture) {
					write_packet( tmppkt);
				}
				else {
					printf("got_picture null %ld\n", tmppkt.pts);
				}
				
				av_free_packet(&tmppkt);

			}
		}
	}

	void prepareDec(AVCodecContext*video_dec_ctx) {
		//*video_dec_ctx = inFormat->streams[videoindex]->codec;
		AVCodec* video_dec = avcodec_find_decoder(video_dec_ctx->codec_id);
		if (avcodec_open2(video_dec_ctx, video_dec, NULL) < 0)
		{
			printf("video_dec_ctx ´error\n");
			//return 0;
		}
	}

	void prepareDecEnc(AVCodecContext*video_dec_ctx, AVCodecContext *video_enc_ctx) {
		prepareDec(video_dec_ctx);

		//AVCodec* video_enc =  avcodec_find_encoder(AV_CODEC_ID_H264);//video_enc_ctx->codec_id);
		//video_enc_ctx->codec_id = video_enc->id;
		//video_enc_ctx->codec = video_enc;
		video_enc_ctx->profile = FF_PROFILE_H264_HIGH;// video_dec_ctx->profile;
		video_enc_ctx->width = video_dec_ctx->width;
		video_enc_ctx->height = video_dec_ctx->height;
		video_enc_ctx->pix_fmt = PIX_FMT_YUV420P;// video_dec_ctx->pix_fmt;// PIX_FMT_YUV420P;
		video_enc_ctx->time_base = video_dec_ctx->time_base;
		//video_enc_ctx->time_base.den = 25;
		video_enc_ctx->bit_rate = video_dec_ctx->bit_rate;
		/*video_enc_ctx->gop_size = 250;
		video_enc_ctx->max_b_frames = 10;*/
		video_enc_ctx->gop_size = 250;// video_dec_ctx->gop_size;
		video_enc_ctx->max_b_frames = 10;
		//H264
		video_enc_ctx->me_range = 16;// video_dec_ctx->me_range;// 16;
		video_enc_ctx->max_qdiff = video_dec_ctx->max_qdiff;// 4;

		video_enc_ctx->qmin = 5;// video_dec_ctx->qmin;// 10;
		video_enc_ctx->qmax = 30;// video_dec_ctx->qmax;// 51;
		/*video_enc_ctx->qmin = 0;
		video_enc_ctx->qmax = 5*/;

		av_opt_set(video_enc_ctx->priv_data, "preset", "slow", 0); //ultrafast,superfast, veryfast, faster, fast, medium, slow, slower, veryslow,placebo
		av_opt_set(video_enc_ctx->priv_data, "tune", "zerolatency", 0); //film,animation,grain,stillimage,psnr,ssim,fastdecode,zerolatency。
		av_opt_set(video_enc_ctx->priv_data, "profile", "main", 0);

		int ret = 0;
		if ((ret = avcodec_open2(video_enc_ctx, video_enc_ctx->codec, NULL)) < 0)
		{
			//char buff[128];
			//av_strerror(ret, buff, 128);
			printf("video_enc_ctx error\n");
			//return 0;
		}
	}

	void fill_apicture(AVPicture *frame, int w, int h)
	{
		//pFrameRGB = avcodec_alloc_frame();
		uint8_t *buffer;
		int numBytes;
		numBytes = avpicture_get_size(PIX_FMT_RGB32, w, h);
		buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
		avpicture_fill((AVPicture *)frame, buffer, PIX_FMT_RGB32, w, h);
	}

	AVPicture * convert(AVFrame *frame,int screen_width,int screen_height) {
		
		img_convert_ctx = sws_getCachedContext(img_convert_ctx,frame->width, frame->height, AV_PIX_FMT_YUV420P/*(AVPixelFormat)frame->format*/, screen_width, screen_height,
			PIX_FMT_RGB32, SWS_BICUBIC/*sws_flags*/, NULL, NULL, NULL);
		if (!img_convert_ctx) {
			av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
			//exit(1);
			return nullptr;
		}

		AVPicture *pic = (AVPicture*)av_malloc(sizeof(AVPicture));
		memset(pic, 0, sizeof(AVPicture));
		fill_apicture(pic, screen_width, screen_height);
		sws_scale(img_convert_ctx, frame->data, frame->linesize,0, frame->height, pic->data, pic->linesize);

		return pic;
	}

	int clipvs(const char* SRC_FILE, const char* OUT_FILE, double from, double to) {
		return clipvs(SRC_FILE,  OUT_FILE,  from,  to,false);
	}

	void getPictures(const char* SRC_FILE, double from,int w,int h,int count,std::vector<AVPicture *>& pics) {
		int videoIndex;
		av_register_all();
		
		if (avformat_open_input(&inFormat, SRC_FILE, NULL, NULL) < 0)
		{
			printf("avformat_open_input error");
			return ;
		}

		if (avformat_find_stream_info(inFormat, NULL) < 0)
		{
			printf("avformat_find_stream_info error");
			return ;
		}

		for (int i = 0; i < inFormat->nb_streams; i++) {
			if (inFormat->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
				videoIndex = i;
				//hasVideo = true;
				//break;
			}

			if (inFormat->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
				//audioindex = i;
				//hasAudio = true;
			}
		}
		av_dump_format(inFormat, 0, SRC_FILE, 0);
		AVStream *videoStream = inFormat->streams[videoIndex];
		AVCodecContext * video_dec_ctx = videoStream->codec;
		prepareDec(video_dec_ctx);

		double pos = (from / av_q2d(videoStream->time_base)); //AV_TIME_BASE;//
		AVFrame *pFrame = av_frame_alloc();
		av_seek_frame(inFormat, videoIndex, pos, AVSEEK_FLAG_BACKWARD);

		AVPacket *pkt = new AVPacket();
		av_init_packet(pkt);

		int cc = 0;
		while (1)
		{
			
			if (av_read_frame(inFormat, pkt) < 0)
			{
				//delete pkt;
				break;
			}

			if (pkt->stream_index == videoIndex) {
				double sec = pkt->pts*av_q2d(videoStream->time_base);
				printf("video %f\n", sec);
				int got_pic;
				int ret = avcodec_decode_video2(video_dec_ctx, pFrame, &got_pic, pkt);
				if (ret < 0)
				{
					//delete pkt;
					//return 0;
				}
				if (got_pic)
				{
					AVPicture *pic = convert(pFrame,w,h);
					if (pic!=nullptr) {
						pics.push_back(pic);
						if (++cc >= count&& pFrame->key_frame) {
							//avcodec_de
							//avcodec_flush_buffers();
							break;
						}
					}
				}
			
			}
		}
		av_free_packet(pkt);
		delete pkt;
		avformat_close_input(&inFormat);
	}

	int clipvs(const char* SRC_FILE, const char* OUT_FILE, double from, double to,bool isKeep)
	{
		vfrom = from;
		vto = to;
		if (from >= to) {
			printf("from>=to error");
			return 0;
		}
		av_register_all();
		bool hasAudio = false;
		bool hasVideo = false;
		//AVFormatContext* inFormat = NULL;
		

		if (avformat_open_input(&inFormat, SRC_FILE, NULL, NULL) < 0)
		{
			printf("avformat_open_input error");
			return 0;
		}

		if (avformat_find_stream_info(inFormat, NULL) < 0)
		{
			printf("avformat_find_stream_info error");
			return 0;
		}

		for (int i = 0; i < inFormat->nb_streams; i++) {
			if (inFormat->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
				videoindex = i;
				hasVideo = true;
				//break;
			}

			if (inFormat->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
				audioindex = i;
				hasAudio = true;
			}
		}
		av_dump_format(inFormat, 0, SRC_FILE, 0);

		AVCodecContext * video_dec_ctx = inFormat->streams[videoindex]->codec;


		//AVFormatContext* outFormat = NULL;
		//AVOutputFormat* ofmt = NULL;
		if (avformat_alloc_output_context2(&outFormat, NULL, NULL, OUT_FILE) < 0)
		{
			printf("avformat_alloc_output_context2 error");
			return 0;
		}

		

		AVCodec* video_enc = avcodec_find_encoder(AV_CODEC_ID_H264);
		std::vector<int> indexs;
		for (int i = 0; i < inFormat->nb_streams; i++) {
			if (inFormat->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
				indexs.insert(indexs.begin(), i);
			}
			else {
				indexs.push_back(i);
			}
		}

		for (int i = 0; i < inFormat->nb_streams; i++) {
			AVStream *in_stream = inFormat->streams[indexs[i]];
			if (in_stream->codec->codec_type != AVMEDIA_TYPE_VIDEO 
				&& in_stream->codec->codec_type != AVMEDIA_TYPE_AUDIO) {
				continue;
			}

			/*AVStream *out_stream = nullptr;*/
			const AVCodec *codec = in_stream->codec->codec;
			if (in_stream->codec->codec_type == AVMEDIA_TYPE_VIDEO&&!isKeep) {
				codec = video_enc;
				//outVideoIndex = i;
			}
			else if (in_stream->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
				//outAudioIndex = i;
			}

			AVStream *out_stream = avformat_new_stream(outFormat, codec);
			if (!out_stream) {
				printf("avformat_new_stream outFormat error");
				return 0;
			}

			int ret = 0;
			ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
			if (ret < 0) {
				fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
				return 0;
			}

			if (in_stream->codec->codec_type == AVMEDIA_TYPE_VIDEO && !isKeep) {
				out_stream->codec->codec = video_enc;
				out_stream->codec->codec_id = video_enc->id;
			}

			out_stream->codec->codec_tag = 0;
			if (outFormat->oformat->flags & AVFMT_GLOBALHEADER) {
				out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
			}
		}



		for (int i = 0; i < outFormat->nb_streams; i++) {
			AVStream*stream = outFormat->streams[i];
			if (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
				outVideoIndex = i;
			}
			else if (stream->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
				outAudioIndex = i;
			}
		}

		AVCodecContext *video_enc_ctx = outFormat->streams[outVideoIndex]->codec;

		if (!isKeep) {
			prepareDecEnc(video_dec_ctx, video_enc_ctx);
		}
		else {
			prepareDec(video_dec_ctx);
		}

		inStream = inFormat->streams[videoindex];
		inAudioStream = inFormat->streams[audioindex];

		audioInTimeBase = inAudioStream->time_base;
		videoInTimeBase = inStream->time_base;

		outStream = outFormat->streams[outVideoIndex];
		outAudioStream = outFormat->streams[outAudioIndex];

		double pos = (from / av_q2d(inStream->time_base)); //AV_TIME_BASE;//
		if (videoindex == -1) {
			pos = from / av_q2d(AVRational{ 1, AV_TIME_BASE });
		}
		printf("start_time %ld %ld\n", outFormat->start_time, inFormat->start_time);
		printf("start_time %ld %ld %ld\n", inStream->start_time, inAudioStream->start_time, outStream->start_time);
		printf("duration %ld %ld %ld\n", inStream->duration, inAudioStream->duration, outStream->duration);
		printf("duration %ld %ld %ld\n", inStream->time_base, inAudioStream->time_base, outStream->time_base);
		
		//outFormat->bit_rate = inFormat->bit_rate;
		av_dump_format(outFormat, 0, OUT_FILE, 1);

		if (!(outFormat->oformat->flags & AVFMT_NOFILE)) {
			int ret = avio_open(&outFormat->pb, OUT_FILE, AVIO_FLAG_WRITE);
			if (ret < 0) {
				printf("Could not open output URL '%s'", OUT_FILE);
				//goto end;
				return 0;
			}
		}

		if (avformat_write_header(outFormat, NULL) < 0)
		{
			printf("avformat_write_header error");
			return 0;
		}
		av_dump_format(outFormat, 0, OUT_FILE, 1);

		audioOutTimeBase = outAudioStream->time_base;
		videoOutTimeBase = outStream->time_base;

		bool isVideoOver = !hasVideo;
		bool isAudioOver = !hasAudio;
		

		videoStartTime = -1;
		int64_t audioStartTime = -1;

		
		AVFrame *pFrame = av_frame_alloc(); //avcodec_alloc_frame();

		int size = video_enc_ctx->width*video_enc_ctx->height * 3 / 2;
		int size0= avpicture_get_size((video_enc_ctx->pix_fmt), video_enc_ctx->width,video_enc_ctx->height);

		

		av_seek_frame(inFormat, videoindex, pos, AVSEEK_FLAG_BACKWARD);
		while (1)
		{
			AVPacket *pkt = new AVPacket();
			av_init_packet(pkt);
			if (av_read_frame(inFormat, pkt) < 0)
			{
				delete pkt;
				break;
			}
			
			if (pkt->stream_index == videoindex) {
				double sec = pkt->pts*av_q2d(inStream->time_base);
				printf("video %f\n", sec);
				if (sec > to && (pkt->flags&AV_PKT_FLAG_KEY)) {
					isVideoOver = true;
				}
				else {
					
					if (!isKeep) {
						encodeAframe(pFrame, pkt);
					}
					else {
						if (videoStartTime<0) {
							videoStartTime = pkt->pts;
							//audioStartTime = av_rescale_q(videoStartTime, inStream->time_base, inAudioStream->time_base);
						}
						pkt->stream_index = outVideoIndex;
						write_packet(*pkt);
					}
				}
				
				av_free_packet(pkt);
				delete pkt;
			}
			else if (pkt->stream_index == audioindex) {
				double sec = pkt->pts*av_q2d(inAudioStream->time_base);
				printf("audio %f\n", sec);
				if (sec > to) {
					isAudioOver = true;
				}
				else {
					if (sec >= from) {
						if (audioStartTime < 0) {
							audioStartTime = pkt->pts;
						}
						AudioPktList.push_back(pkt);
						pkt->stream_index = outAudioIndex;
						writeAudio();
					}
				}
			}
			else {
				av_free_packet(pkt);
				delete pkt;
			}

			if (isVideoOver&&isAudioOver) {
				break;
			}
		}
		if (!isKeep) {
			FlushDecode(pFrame);
		}
		av_write_trailer(outFormat);
		close();
		
		/*av_free_packet(pkt0);
		delete pkt0;*/
		return 0;
	}

	void close() {
		avio_close(outFormat->pb);
		avformat_close_input(&inFormat);
		//av_close_input_file(inFormat);
		avformat_free_context(outFormat);
	}
};

static void CppClip(const char* SRC_FILE, const char* OUT_FILE, double from, double to) {
	VClip vclip;
	vclip.clipvs(SRC_FILE, OUT_FILE,from,to);
}

static void CppClipKeep(const char* SRC_FILE, const char* OUT_FILE, double from, double to) {
	VClip vclip;
	vclip.clipvs(SRC_FILE, OUT_FILE, from, to,true);
}