#include "codecbridge/codecbridge.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cb_plugin {
    void *library;
    const cb_backend_v1 *api;
    size_t sessions;
};

struct cb_encoder {
    cb_plugin *plugin;
    void *backend;
    cb_config config;
    cb_packet held;
    int has_packet, draining, ended, failed, has_pts;
    int64_t last_pts;
};

static cb_status fail(cb_error *e, cb_status code, const char *message)
{
    if (e) {
        e->code = code;
        snprintf(e->message, sizeof(e->message), "%s", message ? message : "");
    }
    return code;
}

static void clear_error(cb_error *e)
{
    if (e) memset(e, 0, sizeof(*e));
}

static cb_status result(cb_encoder *s, cb_status rc, cb_error *e)
{
    if (rc == CB_ERR_BACKEND || rc == CB_ERR_DEVICE) s->failed = 1;
    if (rc < 0 && e && !e->code) fail(e, rc, cb_status_string(rc));
    return rc;
}

void cb_config_default(cb_config *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->struct_size = sizeof(*c);
    c->codec = CB_CODEC_H264;
    c->format = CB_FORMAT_NV12;
    c->memory_type = CB_MEMORY_CPU;
    c->profile = CB_PROFILE_BASELINE;
    c->width = 1920; c->height = 1080;
    c->fps_num = 30; c->fps_den = 1;
    c->qp = 23; c->gop = 60; c->threads = 2;
    c->color_matrix = CB_MATRIX_BT709;
    c->color_range = CB_RANGE_FULL;
}

const char *cb_status_string(cb_status rc)
{
    switch (rc) {
    case CB_OK: return "ok";
    case CB_AGAIN: return "try again";
    case CB_EOF: return "end of stream";
    case CB_ERR_INVALID: return "invalid argument";
    case CB_ERR_UNSUPPORTED: return "unsupported capability";
    case CB_ERR_ABI: return "incompatible plugin ABI";
    case CB_ERR_IO: return "I/O error";
    case CB_ERR_NOMEM: return "out of memory";
    case CB_ERR_BUSY: return "resource busy";
    case CB_ERR_TIMEOUT: return "timeout";
    case CB_ERR_DEVICE: return "device failure";
    case CB_ERR_STATE: return "invalid state";
    case CB_ERR_BACKEND: return "backend failure";
    default: return "unknown status";
    }
}

cb_status cb_plugin_load(const char *path, cb_plugin **out, cb_error *e)
{
    clear_error(e);
    if (!out) return fail(e, CB_ERR_INVALID, "plugin output is NULL");
    *out = NULL;
    if (!path || !*path) return fail(e, CB_ERR_INVALID, "plugin path is empty");
    cb_plugin *p = calloc(1, sizeof(*p));
    if (!p) return fail(e, CB_ERR_NOMEM, "allocating plugin");
    p->library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!p->library) {
        fail(e, CB_ERR_IO, dlerror());
        free(p);
        return CB_ERR_IO;
    }
    /* POSIX specifies dlsym's conversion to a function pointer. */
    cb_backend_query_fn query = (cb_backend_query_fn)dlsym(p->library, CB_BACKEND_ENTRY);
    if (query) p->api = query(CB_ABI_V1);
    const cb_backend_v1 *a = p->api;
    if (!a || a->abi_version != CB_ABI_V1 || a->struct_size < sizeof(*a) ||
        !a->name || !*a->name || !a->version || !a->probe || !a->create ||
        !a->submit || !a->receive || !a->request_keyframe || !a->drain ||
        !a->release_packet || !a->destroy) {
        dlclose(p->library);
        free(p);
        return fail(e, CB_ERR_ABI, "missing entry, incompatible ABI, or incomplete function table");
    }
    *out = p;
    return CB_OK;
}

const char *cb_plugin_name(const cb_plugin *p) { return p ? p->api->name : NULL; }
const char *cb_plugin_version(const cb_plugin *p) { return p ? p->api->version : NULL; }

cb_status cb_plugin_probe(cb_plugin *p, const char *device, cb_caps *caps, cb_error *e)
{
    clear_error(e);
    if (!p || !caps || caps->struct_size < sizeof(*caps))
        return fail(e, CB_ERR_INVALID, "invalid plugin or caps structure");
    cb_caps value = { .struct_size = sizeof(value) };
    cb_status rc = p->api->probe(device, &value, e);
    if (rc != CB_OK) return rc;
    if (value.struct_size < sizeof(value) || !value.width_alignment ||
        !value.height_alignment || !value.max_inflight || !value.min_width ||
        !value.min_height || value.max_width < value.min_width ||
        value.max_height < value.min_height)
        return fail(e, CB_ERR_BACKEND, "backend returned invalid capabilities");
    *caps = value;
    return CB_OK;
}

cb_status cb_plugin_unload(cb_plugin *p, cb_error *e)
{
    clear_error(e);
    if (!p) return fail(e, CB_ERR_INVALID, "plugin is NULL");
    if (p->sessions) return fail(e, CB_ERR_BUSY, "destroy encoders before unloading plugin");
    dlclose(p->library);
    free(p);
    return CB_OK;
}

cb_status cb_encoder_create(cb_plugin *p, const char *device, const cb_config *c,
                            cb_encoder **out, cb_error *e)
{
    clear_error(e);
    if (!out) return fail(e, CB_ERR_INVALID, "encoder output is NULL");
    *out = NULL;
    if (!p || !c || c->struct_size < sizeof(*c) || !c->fps_num || !c->fps_den ||
        c->fps_num > 1000000 || c->fps_den > 1000000 || !c->qp || c->qp > 51 || !c->gop ||
        c->gop > 1000000 || !c->threads || c->threads > 64)
        return fail(e, CB_ERR_INVALID, "invalid encoder configuration");
    if (c->codec != CB_CODEC_H264 || c->format != CB_FORMAT_NV12 ||
        c->memory_type != CB_MEMORY_CPU || c->profile != CB_PROFILE_BASELINE ||
        (c->color_matrix != CB_MATRIX_BT709 && c->color_matrix != CB_MATRIX_BT601) ||
        (c->color_range != CB_RANGE_FULL && c->color_range != CB_RANGE_LIMITED))
        return fail(e, CB_ERR_UNSUPPORTED, "V1 supports baseline H.264 and CPU NV12 with BT.601/709");
    if (c->width < 2 || c->height < 2 || c->width > 16384 || c->height > 16384 ||
        (c->width & 1) || (c->height & 1))
        return fail(e, CB_ERR_INVALID, "NV12 dimensions must be even and within 2..16384");
    cb_caps caps = { .struct_size = sizeof(caps) };
    cb_status rc = cb_plugin_probe(p, device, &caps, e);
    if (rc != CB_OK) return rc;
    if (!(caps.codecs & c->codec) || !(caps.formats & c->format) ||
        !(caps.memory_types & c->memory_type) || !(caps.profiles & c->profile) ||
        c->width < caps.min_width || c->width > caps.max_width ||
        c->height < caps.min_height || c->height > caps.max_height ||
        c->width % caps.width_alignment || c->height % caps.height_alignment)
        return fail(e, CB_ERR_UNSUPPORTED, "configuration exceeds probed backend capabilities");
    cb_encoder *s = calloc(1, sizeof(*s));
    if (!s) return fail(e, CB_ERR_NOMEM, "allocating encoder");
    s->config = *c;
    s->plugin = p;
    rc = p->api->create(device, c, &s->backend, e);
    if (rc != CB_OK || !s->backend) {
        if (s->backend) p->api->destroy(s->backend);
        free(s);
        if (rc == CB_OK) return fail(e, CB_ERR_BACKEND, "backend returned NULL encoder");
        return rc;
    }
    p->sessions++;
    *out = s;
    return CB_OK;
}

static cb_status ready(cb_encoder *s, cb_error *e)
{
    if (!s) return fail(e, CB_ERR_INVALID, "encoder is NULL");
    if (s->failed) return fail(e, CB_ERR_STATE, "encoder failed; destroy and recreate");
    if (s->has_packet) return fail(e, CB_ERR_BUSY, "release outstanding packet first");
    return CB_OK;
}

cb_status cb_encoder_submit(cb_encoder *s, const cb_frame *f, cb_error *e)
{
    clear_error(e);
    cb_status rc = ready(s, e);
    if (rc != CB_OK) return rc;
    if (s->draining) return fail(e, CB_ERR_STATE, "cannot submit after drain");
    if (!f || f->struct_size < sizeof(*f))
        return fail(e, CB_ERR_INVALID, "invalid frame structure");
    if (f->format != s->config.format || f->memory_type != s->config.memory_type)
        return fail(e, CB_ERR_UNSUPPORTED, "frame format or memory type mismatch");
    if (f->width != s->config.width || f->height != s->config.height ||
        f->generation != s->config.generation || f->pts_us < 0 ||
        (s->has_pts && f->pts_us <= s->last_pts))
        return fail(e, CB_ERR_INVALID, "frame dimensions, generation or timestamp mismatch");
    for (unsigned i = 0; i < 2; i++) {
        uint64_t rows = i ? f->height / 2 : f->height;
        uint64_t extent = (rows - 1) * f->strides[i] + f->width;
        if (!f->planes[i] || f->strides[i] < f->width || extent > SIZE_MAX ||
            f->plane_bytes[i] < extent)
            return fail(e, CB_ERR_INVALID, "NV12 plane is NULL, undersized or has invalid stride");
    }
    rc = s->plugin->api->submit(s->backend, f, e);
    if (rc == CB_OK) {
        s->last_pts = f->pts_us;
        s->has_pts = 1;
    }
    return result(s, rc, e);
}

cb_status cb_encoder_receive(cb_encoder *s, uint32_t timeout_ms, cb_packet *packet,
                             cb_error *e)
{
    clear_error(e);
    cb_status rc = ready(s, e);
    if (rc != CB_OK) return rc;
    if (!packet || packet->struct_size < sizeof(*packet))
        return fail(e, CB_ERR_INVALID, "invalid packet structure");
    memset(packet, 0, sizeof(*packet));
    packet->struct_size = sizeof(*packet);
    if (s->ended) return CB_EOF;
    cb_packet value = { .struct_size = sizeof(value) };
    rc = s->plugin->api->receive(s->backend, timeout_ms, &value, e);
    if (rc == CB_OK) {
        if (value.struct_size < sizeof(value) || !value.data || !value.size ||
            value.size > SIZE_MAX || value.generation != s->config.generation) {
            s->plugin->api->release_packet(s->backend, &value);
            return result(s, fail(e, CB_ERR_BACKEND, "backend returned invalid packet"), e);
        }
        s->held = value;
        s->has_packet = 1;
        *packet = value;
    } else if (rc == CB_EOF) {
        if (!s->draining)
            return result(s, fail(e, CB_ERR_BACKEND, "unexpected EOF before drain"), e);
        s->ended = 1;
    }
    return result(s, rc, e);
}

cb_status cb_encoder_request_keyframe(cb_encoder *s, cb_error *e)
{
    clear_error(e);
    cb_status rc = ready(s, e);
    if (rc != CB_OK) return rc;
    if (s->draining) return fail(e, CB_ERR_STATE, "cannot request keyframe after drain");
    return result(s, s->plugin->api->request_keyframe(s->backend, e), e);
}

cb_status cb_encoder_drain(cb_encoder *s, cb_error *e)
{
    clear_error(e);
    cb_status rc = ready(s, e);
    if (rc != CB_OK) return rc;
    if (s->draining) return CB_OK;
    rc = s->plugin->api->drain(s->backend, e);
    if (rc == CB_OK) s->draining = 1;
    return result(s, rc, e);
}

cb_status cb_packet_release(cb_encoder *s, cb_packet *packet, cb_error *e)
{
    clear_error(e);
    if (!s || !packet) return fail(e, CB_ERR_INVALID, "encoder or packet is NULL");
    if (!s->has_packet || packet->data != s->held.data || packet->cookie != s->held.cookie)
        return fail(e, CB_ERR_STATE, "packet is not owned by this encoder");
    s->plugin->api->release_packet(s->backend, &s->held);
    memset(&s->held, 0, sizeof(s->held));
    s->has_packet = 0;
    memset(packet, 0, sizeof(*packet));
    packet->struct_size = sizeof(*packet);
    return CB_OK;
}

cb_status cb_encoder_destroy(cb_encoder *s, cb_error *e)
{
    clear_error(e);
    if (!s) return fail(e, CB_ERR_INVALID, "encoder is NULL");
    if (s->has_packet) return fail(e, CB_ERR_BUSY, "release outstanding packet before destroy");
    s->plugin->api->destroy(s->backend);
    s->plugin->sessions--;
    free(s);
    return CB_OK;
}
