#include "codecbridge/backend.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <x264.h>

struct encoder {
    x264_t *codec;
    x264_picture_t picture;
    cb_config config;
    cb_packet pending;
    int64_t sequence;
    int picture_allocated, force_idr, draining;
};

static cb_status error(cb_error *e, cb_status code, const char *message)
{
    if (e) {
        e->code = code;
        snprintf(e->message, sizeof(e->message), "%s", message);
    }
    return code;
}

static cb_status probe(const char *device, cb_caps *caps, cb_error *e)
{
    if (device && *device)
        return error(e, CB_ERR_UNSUPPORTED, "x264 software backend has no device selector");
    if (!caps || caps->struct_size < sizeof(*caps))
        return error(e, CB_ERR_INVALID, "invalid caps structure");
    *caps = (cb_caps) {
        .struct_size = sizeof(*caps), .flags = 0,
        .codecs = CB_CODEC_H264, .formats = CB_FORMAT_NV12,
        .memory_types = CB_MEMORY_CPU, .profiles = CB_PROFILE_BASELINE,
        .min_width = 16, .min_height = 16, .max_width = 8192, .max_height = 8192,
        .width_alignment = 2, .height_alignment = 2, .max_inflight = 1
    };
    return CB_OK;
}

static void destroy(void *handle)
{
    struct encoder *s = handle;
    if (!s) return;
    if (s->codec) x264_encoder_close(s->codec);
    if (s->picture_allocated) x264_picture_clean(&s->picture);
    free((void *)s->pending.data);
    free(s);
}

static cb_status create(const char *device, const cb_config *c, void **out, cb_error *e)
{
    if (!out) return error(e, CB_ERR_INVALID, "encoder output is NULL");
    *out = NULL;
    cb_caps caps = { .struct_size = sizeof(caps) };
    cb_status rc = probe(device, &caps, e);
    if (rc != CB_OK) return rc;
    if (!c || c->struct_size < sizeof(*c) || c->codec != CB_CODEC_H264 ||
        c->format != CB_FORMAT_NV12 || c->memory_type != CB_MEMORY_CPU ||
        c->profile != CB_PROFILE_BASELINE || c->width < 16 || c->width > 8192 ||
        c->height < 16 || c->height > 8192 || (c->width & 1) || (c->height & 1) ||
        !c->fps_num || !c->fps_den || c->fps_num > 1000000 || c->fps_den > 1000000 ||
        !c->qp || c->qp > 51 || !c->gop || c->gop > 1000000 ||
        !c->threads || c->threads > 64 ||
        (c->color_matrix != CB_MATRIX_BT709 && c->color_matrix != CB_MATRIX_BT601) ||
        (c->color_range != CB_RANGE_FULL && c->color_range != CB_RANGE_LIMITED))
        return error(e, CB_ERR_UNSUPPORTED, "unsupported x264 configuration");
    struct encoder *s = calloc(1, sizeof(*s));
    if (!s) return error(e, CB_ERR_NOMEM, "allocating x264 encoder");
    s->config = *c;
    x264_param_t p;
    if (x264_param_default_preset(&p, "ultrafast", "zerolatency") < 0) {
        destroy(s);
        return error(e, CB_ERR_BACKEND, "x264 preset initialization failed");
    }
    p.i_width = (int)c->width; p.i_height = (int)c->height;
    p.i_csp = X264_CSP_NV12;
    p.i_threads = (int)c->threads;
    p.b_sliced_threads = 1;
    p.i_bframe = 0;
    p.i_sync_lookahead = 0;
    p.rc.i_lookahead = 0;
    p.rc.i_rc_method = X264_RC_CQP;
    p.rc.i_qp_constant = (int)c->qp;
    p.i_keyint_max = (int)c->gop;
    p.i_keyint_min = 1;
    p.i_scenecut_threshold = 0;
    p.b_open_gop = 0;
    p.b_repeat_headers = 1;
    p.b_annexb = 1;
    p.b_aud = 1;
    p.i_fps_num = c->fps_num; p.i_fps_den = c->fps_den;
    /* VFR adds one lookahead frame even with the zerolatency preset.
     * Use a nominal CFR coding timebase; preserve caller timestamps in packets. */
    p.b_vfr_input = 0;
    p.i_timebase_num = c->fps_den; p.i_timebase_den = c->fps_num;
    p.vui.b_fullrange = c->color_range == CB_RANGE_FULL;
    p.vui.i_colmatrix = c->color_matrix == CB_MATRIX_BT709 ? 1 : 6;
    p.vui.i_colorprim = c->color_matrix == CB_MATRIX_BT709 ? 1 : 6;
    p.vui.i_transfer = c->color_matrix == CB_MATRIX_BT709 ? 1 : 6;
    p.i_log_level = X264_LOG_ERROR;
    if (x264_param_apply_profile(&p, "baseline") < 0) {
        destroy(s);
        return error(e, CB_ERR_UNSUPPORTED, "x264 rejected baseline profile settings");
    }
    s->codec = x264_encoder_open(&p);
    if (!s->codec) {
        destroy(s);
        return error(e, CB_ERR_BACKEND, "x264_encoder_open failed");
    }
    if (x264_encoder_maximum_delayed_frames(s->codec) != 0) {
        destroy(s);
        return error(e, CB_ERR_UNSUPPORTED, "x264 settings would buffer delayed frames");
    }
    if (x264_picture_alloc(&s->picture, X264_CSP_NV12, p.i_width, p.i_height) < 0) {
        destroy(s);
        return error(e, CB_ERR_NOMEM, "allocating x264 NV12 picture");
    }
    s->picture_allocated = 1;
    *out = s;
    return CB_OK;
}

static cb_status submit(void *handle, const cb_frame *f, cb_error *e)
{
    struct encoder *s = handle;
    if (s->draining) return error(e, CB_ERR_STATE, "x264 encoder is draining");
    if (s->pending.data) return CB_AGAIN;
    for (unsigned plane = 0; plane < 2; plane++) {
        uint32_t rows = plane ? f->height / 2 : f->height;
        for (uint32_t y = 0; y < rows; y++)
            memcpy(s->picture.img.plane[plane] + (size_t)y * s->picture.img.i_stride[plane],
                   f->planes[plane] + (size_t)y * f->strides[plane], f->width);
    }
    s->picture.i_pts = s->sequence;
    s->picture.i_type = s->force_idr ? X264_TYPE_IDR : X264_TYPE_AUTO;
    x264_picture_t output;
    x264_nal_t *nals = NULL;
    int nal_count = 0;
    int bytes = x264_encoder_encode(s->codec, &nals, &nal_count, &s->picture, &output);
    if (bytes <= 0 || nal_count <= 0 || output.i_pts != s->sequence)
        return error(e, CB_ERR_BACKEND, "x264 failed zero-delay frame/packet contract");
    uint8_t *data = malloc((size_t)bytes);
    if (!data) return error(e, CB_ERR_BACKEND, "cannot retain encoded frame; recreate encoder");
    memcpy(data, nals[0].p_payload, (size_t)bytes);
    s->pending = (cb_packet) {
        .struct_size = sizeof(cb_packet), .data = data, .size = (uint64_t)bytes,
        .flags = output.b_keyframe ? CB_PACKET_KEYFRAME : 0,
        .frame_id = f->frame_id, .pts_us = f->pts_us,
        .generation = f->generation, .cookie = s
    };
    s->force_idr = 0;
    s->sequence++;
    return CB_OK;
}

static cb_status receive(void *handle, uint32_t timeout_ms, cb_packet *packet, cb_error *e)
{
    (void)timeout_ms; (void)e;
    struct encoder *s = handle;
    if (!s->pending.data) return s->draining ? CB_EOF : CB_AGAIN;
    *packet = s->pending;
    return CB_OK;
}

static cb_status request_keyframe(void *handle, cb_error *e)
{
    (void)e;
    ((struct encoder *)handle)->force_idr = 1;
    return CB_OK;
}

static cb_status drain(void *handle, cb_error *e)
{
    (void)e;
    ((struct encoder *)handle)->draining = 1;
    return CB_OK;
}

static void release_packet(void *handle, cb_packet *packet)
{
    struct encoder *s = handle;
    (void)packet;
    free((void *)s->pending.data);
    memset(&s->pending, 0, sizeof(s->pending));
}

static const cb_backend_v1 backend = {
    .abi_version = CB_ABI_V1, .struct_size = sizeof(cb_backend_v1),
    .name = "x264", .version = X264_POINTVER,
    .probe = probe, .create = create, .submit = submit, .receive = receive,
    .request_keyframe = request_keyframe, .drain = drain,
    .release_packet = release_packet, .destroy = destroy
};

CB_API const cb_backend_v1 *cb_backend_query(uint32_t requested_abi)
{
    return requested_abi == CB_ABI_V1 ? &backend : NULL;
}
