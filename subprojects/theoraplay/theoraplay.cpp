/**
 * TheoraPlay; multithreaded Ogg Theora/Ogg Vorbis decoding.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file was originally written by Ryan C. Gordon.
 */

// I wrote this with a lot of peeking at the Theora example code in
//  libtheora-1.1.1/examples/player_example.c, but this is all my own
//  code.

#include <cassert>
#include <cstring>

#include <mutex>
#include <thread>

#include "theoraplay.h"
#include "theora/theoradec.h"
#include "vorbis/codec.h"

#ifdef ANDROID
#include "../../src/android/fopen.hpp"
#endif

#define THEORAPLAY_INTERNAL 1

typedef THEORAPLAY_VideoFrame VideoFrame;
typedef THEORAPLAY_AudioPacket AudioPacket;

// !!! FIXME: these all count on the pixel format being TH_PF_420 for now.

typedef unsigned char *(*ConvertVideoFrameFn)(const th_info *tinfo,
                                              const th_ycbcr_buffer ycbcr);

static unsigned char *ConvertVideoFrame420ToYUVPlanar(
                            const th_info *tinfo, const th_ycbcr_buffer ycbcr,
                            const int p0, const int p1, const int p2)
{
    int i;
    const int w = tinfo->pic_width;
    const int h = tinfo->pic_height;
    const int yoff = (tinfo->pic_x & ~1) + ycbcr[0].stride * (tinfo->pic_y & ~1);
    const int uvoff = (tinfo->pic_x / 2) + (ycbcr[1].stride) * (tinfo->pic_y / 2);
    unsigned char *yuv = (unsigned char *) malloc(w * h * 2);
    const unsigned char *p0data = ycbcr[p0].data + yoff;
    const int p0stride = ycbcr[p0].stride;
    const unsigned char *p1data = ycbcr[p1].data + uvoff;
    const int p1stride = ycbcr[p1].stride;
    const unsigned char *p2data = ycbcr[p2].data + uvoff;
    const int p2stride = ycbcr[p2].stride;

    if (yuv)
    {
        unsigned char *dst = yuv;
		for (i = 0; i < h; i++, dst += w) {
			memcpy(dst, p0data + (p0stride * i), w);
		}
		for (i = 0; i < (h / 2); i++, dst += w / 2) {
			memcpy(dst, p1data + (p1stride * i), w / 2);
		}
		for (i = 0; i < (h / 2); i++, dst += w / 2) {
			memcpy(dst, p2data + (p2stride * i), w / 2);
		}
	}

    return yuv;
} // ConvertVideoFrame420ToYUVPlanar


static unsigned char *ConvertVideoFrame420ToYV12(const th_info *tinfo,
                                                 const th_ycbcr_buffer ycbcr)
{
    return ConvertVideoFrame420ToYUVPlanar(tinfo, ycbcr, 0, 2, 1);
} // ConvertVideoFrame420ToYV12


static unsigned char *ConvertVideoFrame420ToIYUV(const th_info *tinfo,
                                                 const th_ycbcr_buffer ycbcr)
{
    return ConvertVideoFrame420ToYUVPlanar(tinfo, ycbcr, 0, 1, 2);
} // ConvertVideoFrame420ToIYUV


// RGB
#define THEORAPLAY_CVT_FNNAME_420 ConvertVideoFrame420ToRGB
#define THEORAPLAY_CVT_RGB_ALPHA 0
#include "theoraplay_cvtrgb.h"
#undef THEORAPLAY_CVT_RGB_ALPHA
#undef THEORAPLAY_CVT_FNNAME_420

// RGBA
#define THEORAPLAY_CVT_FNNAME_420 ConvertVideoFrame420ToRGBA
#define THEORAPLAY_CVT_RGB_ALPHA 1
#include "theoraplay_cvtrgb.h"
#undef THEORAPLAY_CVT_RGB_ALPHA
#undef THEORAPLAY_CVT_FNNAME_420


struct TheoraDecoder {
    // Thread wrangling...
	int thread_created = 0;
    std::mutex lock;
	volatile int halt = 0;
	int thread_done = 0;
    std::thread worker;

    // API state...
	THEORAPLAY_Io* io = nullptr;
	unsigned int maxframes = 0; // Max video frames to buffer.
	volatile unsigned int prepped = 0;
	volatile unsigned int videocount = 0; // currently buffered frames.
	volatile unsigned int audioms = 0;    // currently buffered audio samples.
	volatile int hasvideo = 0;
	volatile int hasaudio = 0;
	volatile int decode_error = 0;

    THEORAPLAY_VideoFormat vidfmt;
    ConvertVideoFrameFn vidcvt;

	VideoFrame* videolist = nullptr;
	VideoFrame* videolisttail = nullptr;

	AudioPacket* audiolist = nullptr;
	AudioPacket* audiolisttail = nullptr;
};

static int FeedMoreOggData(THEORAPLAY_Io *io, ogg_sync_state *sync)
{
    long buflen = 4096;
    char *buffer = ogg_sync_buffer(sync, buflen);
	if (buffer == nullptr) {
		return -1;
	}
	buflen = io->read(io, buffer, buflen);
	if (buflen <= 0) {
		return 0;
	}
	return (ogg_sync_wrote(sync, buflen) == 0) ? 1 : -1;
} // FeedMoreOggData


// This massive function is where all the effort happens.
static void WorkerThread(TheoraDecoder *ctx)
{
    // make sure we initialized the stream before using pagein, but the stream
    //  will know to ignore pages that aren't meant for it, so pass to both.
    #define queue_ogg_page(ctx) do { \
        if (tpackets) ogg_stream_pagein(&tstream, &page); \
        if (vpackets) ogg_stream_pagein(&vstream, &page); \
    } while (0)

    unsigned long audioframes = 0;
    double fps = 0.0;
    int was_error = 1;  // resets to 0 at the end.
    int eos = 0;  // end of stream flag.

    // Too much Ogg/Vorbis/Theora state...
    ogg_packet packet;
    ogg_sync_state sync;
    ogg_page page;
    int vpackets = 0;
    vorbis_info vinfo;
    vorbis_comment vcomment;
    ogg_stream_state vstream;
    int vdsp_init = 0;
    vorbis_dsp_state vdsp;
    int tpackets = 0;
    th_info tinfo;
    th_comment tcomment;
    ogg_stream_state tstream;
    int vblock_init = 0;
    vorbis_block vblock;
    th_dec_ctx *tdec = nullptr;
    th_setup_info *tsetup = nullptr;

    ogg_sync_init(&sync);
    vorbis_info_init(&vinfo);
    vorbis_comment_init(&vcomment);
    th_comment_init(&tcomment);
    th_info_init(&tinfo);

    int bos = 1;
    while (!ctx->halt && bos)
    {
		if (FeedMoreOggData(ctx->io, &sync) <= 0) {
			goto cleanup;
		}
		// parse out the initial header.
        while ( (!ctx->halt) && (ogg_sync_pageout(&sync, &page) > 0) )
        {
            ogg_stream_state test;

            if (!ogg_page_bos(&page))  // not a header.
            {
                queue_ogg_page(ctx);
                bos = 0;
                break;
            } // if

            ogg_stream_init(&test, ogg_page_serialno(&page));
            ogg_stream_pagein(&test, &page);
            ogg_stream_packetout(&test, &packet);

            if (!tpackets && (th_decode_headerin(&tinfo, &tcomment, &tsetup, &packet) >= 0))
            {
                memcpy(&tstream, &test, sizeof (test));
                tpackets = 1;
            } // if
            else if (!vpackets && (vorbis_synthesis_headerin(&vinfo, &vcomment, &packet) >= 0))
            {
                memcpy(&vstream, &test, sizeof (test));
                vpackets = 1;
            } // else if
            else
            {
                // whatever it is, we don't care about it
                ogg_stream_clear(&test);
            } // else
        } // while
    } // while

    // no audio OR video?
	if (ctx->halt || (!vpackets && !tpackets)) {
		goto cleanup;
	}
	// apparently there are two more theora and two more vorbis headers next.
    while ((!ctx->halt) && ((tpackets && (tpackets < 3)) || (vpackets && (vpackets < 3))))
    {
        while (!ctx->halt && tpackets && (tpackets < 3))
        {
			if (ogg_stream_packetout(&tstream, &packet) != 1) {
				break; // get more data?
			}
			if (!th_decode_headerin(&tinfo, &tcomment, &tsetup, &packet)) {
				goto cleanup;
			}
			tpackets++;
        } // while

        while (!ctx->halt && vpackets && (vpackets < 3))
        {
			if (ogg_stream_packetout(&vstream, &packet) != 1) {
				break; // get more data?
			}
			if (vorbis_synthesis_headerin(&vinfo, &vcomment, &packet)) {
				goto cleanup;
			}
			vpackets++;
        } // while

        // get another page, try again?
		if (ogg_sync_pageout(&sync, &page) > 0) {
			queue_ogg_page(ctx);
		} else if (FeedMoreOggData(ctx->io, &sync) <= 0) {
			goto cleanup;
		}
	} // while

    // okay, now we have our streams, ready to set up decoding.
    if (!ctx->halt && tpackets)
    {
        // th_decode_alloc() docs say to check for insanely large frames yourself.
		if ((tinfo.frame_width > 99999) || (tinfo.frame_height > 99999)) {
			goto cleanup;
		}
		// We treat "unspecified" as NTSC. *shrug*
        if ( (tinfo.colorspace != TH_CS_UNSPECIFIED) &&
             (tinfo.colorspace != TH_CS_ITU_REC_470M) &&
             (tinfo.colorspace != TH_CS_ITU_REC_470BG) )
        {
            assert(0 && "Unsupported colorspace.");  // !!! FIXME
            goto cleanup;
        } // if

        if (tinfo.pixel_fmt != TH_PF_420) { assert(0); goto cleanup; } // !!! FIXME

		if (tinfo.fps_denominator != 0) {
			fps = ((double)tinfo.fps_numerator) / ((double)tinfo.fps_denominator);
		}
		tdec = th_decode_alloc(&tinfo, tsetup);
        if (!tdec) goto cleanup;

        // Set decoder to maximum post-processing level.
        //  Theoretically we could try dropping this level if we're not keeping up.
        int pp_level_max = 0;
        // !!! FIXME: maybe an API to set this?
        //th_decode_ctl(tdec, TH_DECCTL_GET_PPLEVEL_MAX, &pp_level_max, sizeof(pp_level_max));
        th_decode_ctl(tdec, TH_DECCTL_SET_PPLEVEL, &pp_level_max, sizeof(pp_level_max));
    } // if

    // Done with this now.
    if (tsetup != nullptr)
    {
        th_setup_free(tsetup);
        tsetup = nullptr;
    } // if

    if (!ctx->halt && vpackets)
    {
        vdsp_init = (vorbis_synthesis_init(&vdsp, &vinfo) == 0);
		if (!vdsp_init) {
			goto cleanup;
		}
		vblock_init = (vorbis_block_init(&vdsp, &vblock) == 0);
		if (!vblock_init) {
			goto cleanup;
		}
	} // if

    // Now we can start the actual decoding!
    // Note that audio and video don't _HAVE_ to start simultaneously.

    ctx->lock.lock();
    ctx->prepped = 1;
    ctx->hasvideo = (tpackets != 0);
    ctx->hasaudio = (vpackets != 0);
    ctx->lock.unlock();

    while (!ctx->halt && !eos)
    {
        int need_pages = 0;  // need more Ogg pages?
        int saw_video_frame = 0;

        // Try to read as much audio as we can at once. We limit the outer
        //  loop to one video frame and as much audio as we can eat.
        while (!ctx->halt && vpackets)
        {
            float **pcm = nullptr;
            const int frames = vorbis_synthesis_pcmout(&vdsp, &pcm);
            if (frames > 0)
            {
                const int channels = vinfo.channels;
                int chanidx, frameidx;
                float *samples;
                AudioPacket *item = (AudioPacket *) malloc(sizeof (AudioPacket));
                if (item == nullptr) goto cleanup;
                item->playms = (unsigned long) ((((double) audioframes) / ((double) vinfo.rate)) * 1000.0);
                item->channels = channels;
                item->freq = vinfo.rate;
                item->frames = frames;
                item->samples = (float *) malloc(sizeof (float) * frames * channels);
                item->next = nullptr;

                if (item->samples == nullptr)
                {
                    free(item);
                    goto cleanup;
                } // if

                // I bet this beats the crap out of the CPU cache...
                samples = item->samples;
                for (frameidx = 0; frameidx < frames; frameidx++)
                {
					for (chanidx = 0; chanidx < channels; chanidx++) {
						*(samples++) = pcm[chanidx][frameidx];
					}
				}

                vorbis_synthesis_read(&vdsp, frames);  // we ate everything.
                audioframes += frames;

                //printf("Decoded %d frames of audio.\n", (int) frames);
                ctx->lock.lock();
                ctx->audioms += item->playms;
                if (ctx->audiolisttail)
                {
                    assert(ctx->audiolist);
                    ctx->audiolisttail->next = item;
                } // if
                else
                {
                    assert(!ctx->audiolist);
                    ctx->audiolist = item;
                } // else
                ctx->audiolisttail = item;
                ctx->lock.unlock();
            } // if

            else  // no audio available left in current packet?
            {
                // try to feed another packet to the Vorbis stream...
                if (ogg_stream_packetout(&vstream, &packet) <= 0)
                {
					if (!tpackets) {
						need_pages = 1; // no video, get more pages now.
					}
					break;  // we'll get more pages when the video catches up.
                }
				if (vorbis_synthesis(&vblock, &packet) == 0) {
					vorbis_synthesis_blockin(&vdsp, &vblock);
				}
			}
        } // while

        if (!ctx->halt && tpackets)
        {
            // Theora, according to example_player.c, is
            //  "one [packet] in, one [frame] out."
			if (ogg_stream_packetout(&tstream, &packet) <= 0) {
				need_pages = 1;
			} else {
				ogg_int64_t granulepos = 0;

                // you have to guide the Theora decoder to get meaningful timestamps, apparently.  :/
				if (packet.granulepos >= 0) {
					th_decode_ctl(tdec, TH_DECCTL_SET_GRANPOS, &packet.granulepos,
					              sizeof(packet.granulepos));
				}
				if (th_decode_packetin(tdec, &packet, &granulepos) == 0)  // new frame!
                {
                    th_ycbcr_buffer ycbcr;
                    if (th_decode_ycbcr_out(tdec, ycbcr) == 0)
                    {
                        const double videotime = th_granule_time(tdec, granulepos);
                        VideoFrame *item = (VideoFrame *) malloc(sizeof (VideoFrame));
                        if (item == nullptr) { goto cleanup; }
                        item->playms = (unsigned int) (videotime * 1000.0);
                        item->fps = fps;
                        item->width = tinfo.pic_width;
                        item->height = tinfo.pic_height;
                        item->format = ctx->vidfmt;
                        item->pixels = ctx->vidcvt(&tinfo, ycbcr);
                        item->next = nullptr;

                        if (item->pixels == nullptr)
                        {
                            free(item);
                            goto cleanup;
                        } // if

                        //printf("Decoded another video frame.\n");
                        ctx->lock.lock();
                        if (ctx->videolisttail)
                        {
                            assert(ctx->videolist);
                            ctx->videolisttail->next = item;
                        } // if
                        else
                        {
                            assert(!ctx->videolist);
                            ctx->videolist = item;
                        } // else
                        ctx->videolisttail = item;
                        ctx->videocount++;
                        ctx->lock.unlock();

                        saw_video_frame = 1;
                    } // if
                } // if
			}
		}

        if (!ctx->halt && need_pages)
        {
            const int rc = FeedMoreOggData(ctx->io, &sync);
			if (rc == 0) {
				eos = 1; // end of stream
			} else if (rc < 0) {
				goto cleanup; // i/o error, etc.
			} else {
				while (!ctx->halt && (ogg_sync_pageout(&sync, &page) > 0)) {
					queue_ogg_page(ctx);
				}
			}
		}

        // Sleep the process until we have space for more frames.
        if (saw_video_frame)
        {
            int go_on = !ctx->halt;
            //printf("Sleeping.\n");
            while (go_on)
            {
                // !!! FIXME: This is stupid. I should use a semaphore for this.
                ctx->lock.lock();
                go_on = !ctx->halt && (ctx->videocount >= ctx->maxframes);
                ctx->lock.unlock();
				if (go_on) {
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
			}
            //printf("Awake!\n");
        } // if
    } // while

    was_error = 0;

cleanup:
    ctx->decode_error = (!ctx->halt && was_error);
    if (tdec != nullptr) { th_decode_free(tdec); }
    if (tsetup != nullptr) { th_setup_free(tsetup); }
    if (vblock_init) vorbis_block_clear(&vblock);
    if (vdsp_init) vorbis_dsp_clear(&vdsp);
    if (tpackets) ogg_stream_clear(&tstream);
    if (vpackets) ogg_stream_clear(&vstream);
    th_info_clear(&tinfo);
    th_comment_clear(&tcomment);
    vorbis_comment_clear(&vcomment);
    vorbis_info_clear(&vinfo);
    ogg_sync_clear(&sync);
    ctx->io->close(ctx->io);
    ctx->thread_done = 1;
} // WorkerThread


static void *WorkerThreadEntry(void *_this)
{
    TheoraDecoder *ctx = (TheoraDecoder *) _this;
    WorkerThread(ctx);
    //printf("Worker thread is done.\n");
    return nullptr;
} // WorkerThreadEntry


static long IoFopenRead(THEORAPLAY_Io *io, void *buf, long buflen)
{
    FILE *f = (FILE *) io->userdata;
    const size_t br = fread(buf, 1, buflen, f);
	if ((br == 0) && ferror(f)) {
		return -1;
	}
    return (long) br;
} // IoFopenRead


static void IoFopenClose(THEORAPLAY_Io *io)
{
    FILE *f = (FILE *) io->userdata;
    fclose(f);
    free(io);
} // IoFopenClose


THEORAPLAY_Decoder *THEORAPLAY_startDecodeFile(const char *fname,
                                               const unsigned int maxframes,
                                               THEORAPLAY_VideoFormat vidfmt)
{
    THEORAPLAY_Io *io = (THEORAPLAY_Io *) malloc(sizeof (THEORAPLAY_Io));
	if (io == nullptr) {
		return nullptr;
	}
	FILE *f = fopen(fname, "rb");
    if (f == nullptr)
    {
        free(io);
        return nullptr;
    } // if

    io->read = IoFopenRead;
    io->close = IoFopenClose;
    io->userdata = f;
    return THEORAPLAY_startDecode(io, maxframes, vidfmt);
} // THEORAPLAY_startDecodeFile


THEORAPLAY_Decoder *THEORAPLAY_startDecode(THEORAPLAY_Io *io,
                                           const unsigned int maxframes,
                                           THEORAPLAY_VideoFormat vidfmt)
{
    TheoraDecoder *ctx = nullptr;
    ConvertVideoFrameFn vidcvt = nullptr;

    switch (vidfmt)
    {
        // !!! FIXME: current expects TH_PF_420.
        #define VIDCVT(t) case THEORAPLAY_VIDFMT_##t: vidcvt = ConvertVideoFrame420To##t; break;
        VIDCVT(YV12)
        VIDCVT(IYUV)
        VIDCVT(RGB)
        VIDCVT(RGBA)
        #undef VIDCVT
        default: goto startdecode_failed;  // invalid/unsupported format.
    } // switch

    ctx = new TheoraDecoder;

    ctx->maxframes = maxframes;
    ctx->vidfmt = vidfmt;
    ctx->vidcvt = vidcvt;
    ctx->io = io;

    try {
        ctx->worker = std::thread(WorkerThreadEntry, ctx);
        ctx->thread_created = true;
    } catch (std::system_error&) {
        goto startdecode_failed;
    }
    return (THEORAPLAY_Decoder *) ctx;

startdecode_failed:
    io->close(io);
    delete ctx;
    return nullptr;
} // THEORAPLAY_startDecode


void THEORAPLAY_stopDecode(THEORAPLAY_Decoder *decoder)
{
    TheoraDecoder *ctx = (TheoraDecoder *) decoder;
	if (!ctx) {
		return;
	}
    if (ctx->thread_created)
    {
        ctx->halt = 1;
        ctx->worker.join();
    } // if

    VideoFrame *videolist = ctx->videolist;
    while (videolist)
    {
        VideoFrame *next = videolist->next;
        free(videolist->pixels);
        free(videolist);
        videolist = next;
    } // while

    AudioPacket *audiolist = ctx->audiolist;
    while (audiolist)
    {
        AudioPacket *next = audiolist->next;
        free(audiolist->samples);
        free(audiolist);
        audiolist = next;
    } // while

    delete ctx;
} // THEORAPLAY_stopDecode


int THEORAPLAY_isDecoding(THEORAPLAY_Decoder *decoder)
{
    TheoraDecoder *ctx = (TheoraDecoder *) decoder;
    int retval = 0;
    if (ctx)
    {
        ctx->lock.lock();
        retval = ( ctx && (ctx->audiolist || ctx->videolist ||
                   (ctx->thread_created && !ctx->thread_done)) );
        ctx->lock.unlock();
    } // if
    return retval;
} // THEORAPLAY_isDecoding

#define GET_SYNCED_VALUE(typ, defval, decoder, member)                                             \
	TheoraDecoder* ctx = (TheoraDecoder*)(decoder);                                                \
	typ retval = defval;                                                                           \
	if (ctx) {                                                                                     \
		ctx->lock.lock();                                                                          \
		retval = ctx->member;                                                                      \
		ctx->lock.unlock();                                                                        \
	}                                                                                              \
	return retval;

int THEORAPLAY_isInitialized(THEORAPLAY_Decoder *decoder)
{
    GET_SYNCED_VALUE(int, 0, decoder, prepped);
} // THEORAPLAY_isInitialized


int THEORAPLAY_hasVideoStream(THEORAPLAY_Decoder *decoder)
{
    GET_SYNCED_VALUE(int, 0, decoder, hasvideo);
} // THEORAPLAY_hasVideoStream


int THEORAPLAY_hasAudioStream(THEORAPLAY_Decoder *decoder)
{
    GET_SYNCED_VALUE(int, 0, decoder, hasaudio);
} // THEORAPLAY_hasAudioStream


unsigned int THEORAPLAY_availableVideo(THEORAPLAY_Decoder *decoder)
{
    GET_SYNCED_VALUE(unsigned int, 0, decoder, videocount);
} // THEORAPLAY_hasAudioStream


unsigned int THEORAPLAY_availableAudio(THEORAPLAY_Decoder *decoder)
{
    GET_SYNCED_VALUE(unsigned int, 0, decoder, audioms);
} // THEORAPLAY_hasAudioStream


int THEORAPLAY_decodingError(THEORAPLAY_Decoder *decoder)
{
    GET_SYNCED_VALUE(int, 0, decoder, decode_error);
} // THEORAPLAY_decodingError


const THEORAPLAY_AudioPacket *THEORAPLAY_getAudio(THEORAPLAY_Decoder *decoder)
{
    TheoraDecoder *ctx = (TheoraDecoder *) decoder;
    AudioPacket *retval;

    ctx->lock.lock();
    retval = ctx->audiolist;
    if (retval)
    {
        ctx->audioms -= retval->playms;
        ctx->audiolist = retval->next;
        retval->next = nullptr;
		if (ctx->audiolist == nullptr) {
			ctx->audiolisttail = nullptr;
		}
	}
    ctx->lock.unlock();

    return retval;
} // THEORAPLAY_getAudio


void THEORAPLAY_freeAudio(const THEORAPLAY_AudioPacket *_item)
{
    THEORAPLAY_AudioPacket *item = (THEORAPLAY_AudioPacket *) _item;
    if (item != nullptr)
    {
        assert(item->next == nullptr);
        free(item->samples);
        free(item);
    } // if
} // THEORAPLAY_freeAudio


const THEORAPLAY_VideoFrame *THEORAPLAY_getVideo(THEORAPLAY_Decoder *decoder)
{
    TheoraDecoder *ctx = (TheoraDecoder *) decoder;
    VideoFrame *retval;

    ctx->lock.lock();
    retval = ctx->videolist;
    if (retval)
    {
        ctx->videolist = retval->next;
        retval->next = nullptr;
		if (ctx->videolist == nullptr) {
			ctx->videolisttail = nullptr;
		}
		assert(ctx->videocount > 0);
        ctx->videocount--;
    } // if
    ctx->lock.unlock();

    return retval;
} // THEORAPLAY_getVideo


void THEORAPLAY_freeVideo(const THEORAPLAY_VideoFrame *_item)
{
    THEORAPLAY_VideoFrame *item = (THEORAPLAY_VideoFrame *) _item;
	if (item != nullptr) {
		assert(item->next == nullptr);
		free(item->pixels);
        free(item);
	}
} // THEORAPLAY_freeVideo

// end of theoraplay.cpp ...

