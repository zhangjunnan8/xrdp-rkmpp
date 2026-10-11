/* SPDX-License-Identifier: Apache-2.0
 * RK3576 direct XRDP AVC420 encoder. No CodecBridge API or plugin loader.
 * XRDP callback / NV12 damage semantics: neutrinolabs/xrdp v0.10.6.1,
 * xrdp_encoder_x264.c, Copyright (C) Jay Sorg 2016-2024, Apache-2.0.
 */
#ifdef HAVE_CONFIG_H
#include <config_ac.h>
#endif
#define MODULE_TAG "xrdp_mpp"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/rk_venc_cfg.h>
#include "log.h"
#include "xrdp_encoder_x264.h"
#include "xrdp_encoder_mpp.h"

#define MPP_SESSIONS 16
#define MPP_WAIT_MS 250
struct mpp_surface
{
    MppCtx ctx;
    MppApi *api;
    MppEncCfg cfg;
    MppBufferGroup group;
    MppBuffer buffer;
    unsigned char *image; /* complete NV12 surface, including unchanged pixels */
    int width, height, stride, rows, software, failed, direct, hw_image_valid;
    size_t size;
    int64_t sequence;
    uint64_t frames, bytes, total_us, maximum_us, last_us;
    uint64_t sync_begin_us, upload_us, sync_end_us, submit_us, wait_us;
};
struct mpp_encoder
{
    struct mpp_surface surfaces[MPP_SESSIONS];
    void *x264;
    int qp, strict, direct;
    void (*input_ready)(void *);
    void *input_ready_arg;
};

static uint64_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000 + (uint64_t)t.tv_nsec / 1000;
}

static int setting(const char *name, int def, int low, int high)
{
    const char *value = getenv(name);
    char *end;
    long n;
    if (value == NULL) return def;
    errno = 0;
    n = strtol(value, &end, 10);
    if (errno || end == value || *end || n < low || n > high)
    {
        LOG(LOG_LEVEL_WARNING, "MPP: invalid %s; using %d", name, def);
        return def;
    }
    return (int)n;
}

static void close_hardware(struct mpp_surface *s)
{
    /* Destroy/join MPP before releasing memory which it may still reference. */
    if (s->ctx) mpp_destroy(s->ctx);
    if (s->cfg) mpp_enc_cfg_deinit(s->cfg);
    if (s->buffer) mpp_buffer_put(s->buffer);
    if (s->group) mpp_buffer_group_put(s->group);
    s->ctx = NULL; s->api = NULL; s->cfg = NULL;
    s->buffer = NULL; s->group = NULL;
}

static void close_surface(struct mpp_surface *s)
{
    if (s->frames)
    {
        LOG(LOG_LEVEL_INFO, "MPP: summary %dx%d frames=%" PRIu64
            " bytes=%" PRIu64 " encode_mean_us=%" PRIu64 " encode_max_us=%" PRIu64,
            s->width, s->height, s->frames, s->bytes,
            s->total_us / s->frames, s->maximum_us);
        LOG(LOG_LEVEL_INFO, "MPP: stages direct=%d sync_begin_us=%" PRIu64
            " upload_us=%" PRIu64 " sync_end_us=%" PRIu64
            " submit_us=%" PRIu64 " wait_us=%" PRIu64,
            s->direct, s->sync_begin_us/s->frames, s->upload_us/s->frames,
            s->sync_end_us/s->frames, s->submit_us/s->frames, s->wait_us/s->frames);
    }
    close_hardware(s);
    free(s->image);
    memset(s, 0, sizeof(*s));
}

static int open_hardware(struct mpp_surface *s, int qp)
{
    MPP_RET ret;
    RK_S64 timeout = MPP_WAIT_MS;
    MppEncHeaderMode header = MPP_ENC_HEADER_MODE_EACH_IDR;
    const char *operation = "geometry";
    if (s->stride > 4096 || s->rows > 4096 ||
            (size_t)s->stride * s->rows > 4096 * 2304) goto fail;
#define CALL(expr) do { operation = #expr; ret = (expr); if (ret != MPP_OK) \
    { LOG(LOG_LEVEL_ERROR, "MPP: %s failed (%d)", operation, ret); goto fail; } } while (0)
#define CFG(key, val) CALL(mpp_enc_cfg_set_s32(s->cfg, key, val))
    CALL(mpp_create(&s->ctx, &s->api));
    CALL(s->api->control(s->ctx, MPP_SET_INPUT_TIMEOUT, &timeout));
    CALL(s->api->control(s->ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout));
    CALL(mpp_init(s->ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC));
    CALL(mpp_enc_cfg_init(&s->cfg));
    CALL(s->api->control(s->ctx, MPP_ENC_GET_CFG, s->cfg));
    CFG("prep:width", s->stride);
    CFG("prep:height", s->rows);
    CFG("prep:hor_stride", s->stride);
    CFG("prep:ver_stride", s->rows);
    CFG("prep:format", MPP_FMT_YUV420SP);
    /* xorgxrdp AVC420 capture uses BT.709 full-range NV12. */
    CFG("prep:range", MPP_FRAME_RANGE_JPEG);
    CFG("prep:colorspace", MPP_FRAME_SPC_BT709);
    CFG("prep:colorprim", MPP_FRAME_PRI_BT709);
    CFG("prep:colortrc", MPP_FRAME_TRC_BT709);
    CFG("rc:mode", MPP_ENC_RC_MODE_FIXQP);
    CFG("rc:fps_in_flex", 0); CFG("rc:fps_in_num", 60); CFG("rc:fps_in_denorm", 1);
    CFG("rc:fps_out_flex", 0); CFG("rc:fps_out_num", 60); CFG("rc:fps_out_denorm", 1);
    CFG("rc:gop", 120);
    CFG("rc:drop_mode", MPP_ENC_RC_DROP_FRM_DISABLED);
    CFG("rc:qp_init", qp); CFG("rc:qp_min", qp); CFG("rc:qp_max", qp);
    CFG("rc:qp_min_i", qp); CFG("rc:qp_max_i", qp); CFG("rc:qp_ip", 0);
    CFG("codec:type", MPP_VIDEO_CodingAVC);
    CFG("h264:profile", 66); /* baseline: no B pictures, broad AVC420 compatibility */
    CFG("h264:level", s->stride * s->rows > 1920 * 1088 ? 52 : 42);
    CFG("h264:cabac_en", 0); CFG("h264:trans8x8", 0);
    CFG("split:mode", 0); /* one complete access unit per receive */
    CALL(s->api->control(s->ctx, MPP_ENC_SET_CFG, s->cfg));
    CALL(s->api->control(s->ctx, MPP_ENC_SET_HEADER_MODE, &header));
    CALL(mpp_buffer_group_get_internal(&s->group,
         MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE));
    CALL(mpp_buffer_get(s->group, &s->buffer, s->size));
    LOG(LOG_LEVEL_INFO, "MPP: opened RK3576 %dx%d coded=%dx%d QP=%d"
        " BT709-full no-B one-frame-in-flight", s->width, s->height,
        s->stride, s->rows, qp);
    return 0;
fail:
    LOG(LOG_LEVEL_ERROR, "MPP: hardware initialization failed at %s", operation);
    close_hardware(s);
    return 1;
#undef CFG
#undef CALL
}

void *xrdp_encoder_mpp_create(void)
{
    struct mpp_encoder *e = calloc(1, sizeof(*e));
    if (e)
    {
        e->qp = setting("XRDP_MPP_QP", 18, 10, 40);
        e->strict = setting("XRDP_MPP_STRICT", 0, 0, 1);
        e->direct = setting("XRDP_MPP_DIRECT", 0, 0, 1);
    }
    return e;
}

int xrdp_encoder_mpp_delete(void *handle)
{
    struct mpp_encoder *e = handle;
    int i;
    if (!e) return 0;
    for (i = 0; i < MPP_SESSIONS; ++i) close_surface(&e->surfaces[i]);
    if (e->x264) xrdp_encoder_x264_delete(e->x264);
    free(e);
    return 0;
}

void xrdp_encoder_mpp_set_input_ready(void *handle, void (*ready)(void *), void *arg)
{
    struct mpp_encoder *e = handle;
    if (e) { e->input_ready = ready; e->input_ready_arg = arg; }
}

static void input_ready(struct mpp_encoder *e)
{
    void (*ready)(void *) = e->input_ready;
    void *arg = e->input_ready_arg;
    e->input_ready = NULL; e->input_ready_arg = NULL;
    if (ready) ready(arg);
}

/* All rectangles are checked before modifying the persistent surface.
 * Chroma coverage is rounded out to complete 2x2 NV12 samples. */
static int validate_rects(struct mpp_surface *s, int left, int top,
                        int twidth, int theight, const unsigned char *src,
                        const short *rects, int count)
{
    int i;
    (void)src;
    for (i = 0; i < count; ++i)
    {
        int x = rects[4*i], t = rects[4*i+1];
        int w = rects[4*i+2], h = rects[4*i+3];
        if (x < left || t < top || w <= 0 || h <= 0 ||
                x > twidth-w || t > theight-h ||
                x-left > s->width-w || t-top > s->height-h) return 1;
    }
    return 0;
}

static void update_image(struct mpp_surface *s, unsigned char *dst,
                         int left, int top, int twidth, int theight,
                         const unsigned char *src, const short *rects, int count)
{
    int i, y;
    for (i = 0; i < count; ++i)
    {
        int x = rects[4*i], t = rects[4*i+1];
        int w = rects[4*i+2], h = rects[4*i+3];
        int cx = x & ~1, ct = t & ~1;
        int cw = ((x+w+1)&~1)-cx, ch = ((t+h+1)&~1)-ct;
        for (y = 0; y < h; ++y)
            memcpy(dst + (size_t)(t-top+y)*s->stride+x-left,
                   src + (size_t)(t+y)*twidth+x, w);
        for (y = 0; y < ch/2; ++y)
            memcpy(dst + (size_t)s->stride*s->rows +
                       (size_t)((ct-top)/2+y)*s->stride+cx-left,
                   src + (size_t)twidth*theight + (size_t)(ct/2+y)*twidth+cx, cw);
    }
}

/* Recover the complete persistent hardware input before releasing its DMA
 * buffer. MPP only reads this image; join it before changing ownership.
 * A cache/ownership failure is fatal to this surface, never a corrupt fallback.
 */
static int preserve_input(struct mpp_surface *s)
{
    void *ptr;
    if (!s->hw_image_valid) return 0;
    if (s->ctx) mpp_destroy(s->ctx);
    s->ctx = NULL; s->api = NULL;
    ptr = mpp_buffer_get_ptr(s->buffer);
    if (!ptr || mpp_buffer_sync_ro_begin(s->buffer) != MPP_OK) return 1;
    memcpy(s->image, ptr, s->size);
    if (mpp_buffer_sync_ro_end(s->buffer) != MPP_OK) return 1;
    s->hw_image_valid = 0;
    return 0;
}

static int encode_hardware(struct mpp_encoder *e, struct mpp_surface *s, char *out, int *length,
                           const short *rects, int count, int left, int top,
                           int twidth, int theight, const unsigned char *src, int *copied)
{
    MppFrame frame = NULL;
    MppPacket packet = NULL;
    MPP_RET ret;
    uint64_t start = now_us(), elapsed, mark;
    size_t bytes;
    unsigned char *ptr = mpp_buffer_get_ptr(s->buffer);
    if (!ptr) return 1;
    if (mpp_buffer_sync_begin(s->buffer) != MPP_OK) return 1;
    mark = now_us(); s->sync_begin_us += mark-start;
    if (s->direct)
    {
        if (!s->hw_image_valid) memcpy(ptr, s->image, s->size);
        update_image(s, ptr, left, top, twidth, theight, src, rects, count);
        s->hw_image_valid = 1;
    }
    else if (s->sequence == 0) memcpy(ptr, s->image, s->size);
    else
    {
        /* The previous encode has completed. Reuse its full input surface
         * and transfer only damaged pixels; padded/unchanged pixels persist. */
        int i, y;
        for (i = 0; i < count; ++i)
        {
            int x = rects[4*i]-left, t = rects[4*i+1]-top;
            int w = rects[4*i+2], h = rects[4*i+3];
            int cx = x&~1, ct = t&~1;
            int cw = ((x+w+1)&~1)-cx, ch = ((t+h+1)&~1)-ct;
            for (y = 0; y < h; ++y)
            {
                size_t offset = (size_t)(t+y)*s->stride+x;
                memcpy(ptr+offset, s->image+offset, w);
            }
            for (y = 0; y < ch/2; ++y)
            {
                size_t offset = (size_t)s->stride*s->rows +
                                (size_t)(ct/2+y)*s->stride+cx;
                memcpy(ptr+offset, s->image+offset, cw);
            }
        }
    }
    *copied = 1;
    elapsed = now_us(); s->upload_us += elapsed-mark; mark = elapsed;
    if (mpp_buffer_sync_end(s->buffer) != MPP_OK) return 1;
    elapsed = now_us(); s->sync_end_us += elapsed-mark;
    input_ready(e); /* From here on, never read src again, including recovery. */
    if (mpp_frame_init(&frame) != MPP_OK) return 1;
    mpp_frame_set_width(frame, s->stride);
    mpp_frame_set_height(frame, s->rows);
    mpp_frame_set_hor_stride(frame, s->stride);
    mpp_frame_set_ver_stride(frame, s->rows);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
    mpp_frame_set_buffer(frame, s->buffer);
    mpp_frame_set_pts(frame, s->sequence);
    /* After a long idle, refresh reference state promptly for the client. */
    if (s->last_us && start-s->last_us > 1000000 &&
            s->api->control(s->ctx, MPP_ENC_SET_IDR_FRAME, NULL) != MPP_OK)
    {
        mpp_frame_deinit(&frame);
        return 1;
    }
    mark = now_us();
    ret = s->api->encode_put_frame(s->ctx, frame);
    mpp_frame_deinit(&frame);
    elapsed = now_us(); s->submit_us += elapsed-mark;
    if (ret != MPP_OK)
    {
        LOG(LOG_LEVEL_ERROR, "MPP: submit failed (%d)", ret);
        return 1;
    }
    mark = now_us();
    ret = s->api->encode_get_packet(s->ctx, &packet);
    s->wait_us += now_us()-mark;
    if (ret != MPP_OK || !packet)
    {
        LOG(LOG_LEVEL_ERROR, "MPP: receive failed/timeout (%d)", ret);
        if (packet) mpp_packet_deinit(&packet);
        return 1;
    }
    bytes = mpp_packet_get_length(packet);
    if (!bytes || bytes > (size_t)*length ||
            mpp_packet_get_pts(packet) != s->sequence ||
            (mpp_packet_is_partition(packet) && !mpp_packet_is_eoi(packet)))
    {
        LOG(LOG_LEVEL_ERROR, "MPP: invalid/oversized output bytes=%zu capacity=%d", bytes, *length);
        mpp_packet_deinit(&packet);
        return 1;
    }
    memcpy(out, mpp_packet_get_pos(packet), bytes);
    mpp_packet_deinit(&packet);
    *length = (int)bytes;
    elapsed = now_us()-start;
    s->last_us = start; s->sequence++; s->frames++; s->bytes += bytes;
    s->total_us += elapsed;
    if (elapsed > s->maximum_us) s->maximum_us = elapsed;
    if (s->frames == 1)
        LOG(LOG_LEVEL_INFO, "MPP: first hardware frame complete bytes=%zu encode_us=%" PRIu64,
            bytes, elapsed);
    return 0;
}

int xrdp_encoder_mpp_encode(void *handle, int session, int left, int top,
                           int width, int height, int twidth, int theight,
                           int format, const char *data, short *crects, int num_crects,
                           char *cdata, int *cdata_bytes, int connection_type, int *flags_ptr)
{
    struct mpp_encoder *e = handle;
    struct mpp_surface *s;
    int flags = 0, result, copied = 0;
    short full[4];
    (void)format; /* XRDP AVC420 callback explicitly supplies NV12 */
    if (flags_ptr) *flags_ptr = 0;
    if (!e || session < 0 || session >= MPP_SESSIONS || !data || !cdata ||
            !cdata_bytes || *cdata_bytes <= 0 || width < 2 || height < 2 ||
            width > 8192 || height > 8192 || left < 0 || top < 0 ||
            (left&1) || (top&1) || twidth < width || theight < height ||
            twidth > 16384 || theight > 16384 || (twidth&1) || (theight&1) ||
            left > twidth-width || top > theight-height ||
            num_crects < 0 || (num_crects && !crects)) return 1;
    s = &e->surfaces[session];
    if (s->width != width || s->height != height)
    {
        flags = s->image ? 3 : 1;
        close_surface(s);
        s->width = width; s->height = height;
        s->stride = (width+15)&~15; s->rows = (height+15)&~15;
        s->size = (size_t)s->stride*s->rows*3/2;
        s->direct = e->direct;
        s->image = calloc(1, s->size);
        if (!s->image) { close_surface(s); return 1; }
        memset(s->image+(size_t)s->stride*s->rows, 128, s->size/3);
        if (open_hardware(s, e->qp)) s->failed = 1;
    }
    if (validate_rects(s, left, top, twidth, theight,
                      (const unsigned char *)data, crects, num_crects)) return 1;
    if (s->failed == 2) return 1;
    if (!s->direct || s->software || s->failed)
    {
        update_image(s, s->image, left, top, twidth, theight,
                     (const unsigned char *)data, crects, num_crects);
        copied = 1;
    }
    if (!s->software && !s->failed)
    {
        if (encode_hardware(e, s, cdata, cdata_bytes, crects, num_crects, left, top,
                            twidth, theight, (const unsigned char *)data, &copied) == 0)
        {
            if (flags_ptr) *flags_ptr = flags;
            return 0;
        }
        s->failed = 1;
    }
    if (s->failed && !s->software)
    {
        if (s->direct)
        {
            if (preserve_input(s))
            {
                LOG(LOG_LEVEL_ERROR, "MPP: cannot preserve complete DMA input; refusing corrupt recovery");
                s->failed = 2; close_hardware(s); return 1;
            }
            /* Also applies damage if submission failed before upload began. */
            if (!copied)
                update_image(s, s->image, left, top, twidth, theight,
                             (const unsigned char *)data, crects, num_crects);
        }
        close_hardware(s);
        if (e->strict)
        {
            LOG(LOG_LEVEL_ERROR, "MPP: strict hardware mode; refusing software fallback");
            return 1;
        }
        /* Recreate all software contexts on a transition to guarantee an IDR,
         * even after a hardware resize/recovery on this same surface. */
        if (e->x264) xrdp_encoder_x264_delete(e->x264);
        e->x264 = xrdp_encoder_x264_create();
        if (!e->x264) return 1;
        s->software = 1;
        flags |= 3;
        LOG(LOG_LEVEL_WARNING, "MPP: hardware failed; switching to x264 with full-frame IDR");
    }
    full[0] = full[1] = 0; full[2] = (short)width; full[3] = (short)height;
    if (!e->x264)
    {
        e->x264 = xrdp_encoder_x264_create();
        if (!e->x264) return 1;
        flags |= 3;
    }
    input_ready(e);
    result = xrdp_encoder_x264_encode(e->x264, session, 0, 0, width, height,
                                     s->stride, s->rows, 0, (const char *)s->image,
                                     full, 1, cdata, cdata_bytes, connection_type, NULL);
    if (result != 0)
    {
        /* x264 may have consumed a frame whose packet did not fit. Never
         * send P pictures referring to output which the client did not get. */
        xrdp_encoder_x264_delete(e->x264);
        e->x264 = NULL;
        LOG(LOG_LEVEL_WARNING, "MPP: software recovery output failed; next frame restarts with IDR");
    }
    if (flags_ptr) *flags_ptr = flags;
    return result;
}
