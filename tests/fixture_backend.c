/* Fault injection only. This is deliberately not a video encoder. */
#include "codecbridge/backend.h"
#include <stdlib.h>
#include <string.h>

#if CB_FIXTURE_MODE == 5
CB_API int cb_fixture_without_entry(void) { return 0; }
#else
struct fixture { int draining; uint64_t generation, frame_id; int64_t pts; uint8_t *data; };
static cb_status probe(const char *device, cb_caps *caps, cb_error *error)
{
    (void)error;
    if (device && !strcmp(device, "unavailable")) return CB_ERR_DEVICE;
    *caps = (cb_caps){ .struct_size = sizeof(*caps), .codecs = CB_CODEC_H264,
        .formats = CB_FORMAT_NV12, .memory_types = CB_MEMORY_CPU, .profiles = CB_PROFILE_BASELINE,
        .min_width = 2, .min_height = 2, .max_width = 8192, .max_height = 8192,
        .width_alignment = 2, .height_alignment = 2, .max_inflight = 1 };
    if (device && !strcmp(device, "bad-caps")) caps->width_alignment = 0;
    return CB_OK;
}
static cb_status create(const char *device, const cb_config *config, void **out, cb_error *error)
{
    (void)device; (void)error;
    struct fixture *s = calloc(1, sizeof(*s));
    if (!s) return CB_ERR_NOMEM;
    s->generation = config->generation;
    *out = s;
    return CB_OK;
}
static cb_status submit(void *handle, const cb_frame *frame, cb_error *error)
{
    (void)error;
    struct fixture *s = handle;
    if (frame->frame_id == 900) return CB_ERR_TIMEOUT;
    if (frame->frame_id == 901) return CB_ERR_DEVICE;
    if (s->data) return CB_AGAIN;
    s->data = malloc(4);
    if (!s->data) return CB_ERR_NOMEM;
    memset(s->data, 0, 4);
    s->frame_id = frame->frame_id;
    s->pts = frame->pts_us;
    return CB_OK;
}
static cb_status receive(void *handle, uint32_t timeout_ms, cb_packet *packet, cb_error *error)
{
    (void)error;
    struct fixture *s = handle;
    if (!s->data) return s->draining ? CB_EOF : (timeout_ms ? CB_ERR_TIMEOUT : CB_AGAIN);
    *packet = (cb_packet) { .struct_size = sizeof(*packet), .data = s->data, .size = 4,
        .generation = s->generation, .frame_id = s->frame_id, .pts_us = s->pts, .cookie = s };
    if (s->frame_id == 902) packet->generation++;
    return CB_OK;
}
static cb_status keyframe(void *handle, cb_error *error) { (void)handle; (void)error; return CB_OK; }
static cb_status drain(void *handle, cb_error *error)
{ (void)error; ((struct fixture *)handle)->draining = 1; return CB_OK; }
static void release(void *handle, cb_packet *packet)
{ (void)packet; struct fixture *s = handle; free(s->data); s->data = NULL; }
static void destroy(void *handle)
{ struct fixture *s = handle; free(s->data); free(s); }
static cb_backend_v1 backend = {
    .abi_version = CB_ABI_V1, .struct_size = sizeof(cb_backend_v1),
    .name = "test-fixture-not-a-codec", .version = "1",
    .probe = probe, .create = create, .submit = submit, .receive = receive,
    .request_keyframe = keyframe, .drain = drain, .release_packet = release, .destroy = destroy
};
CB_API const cb_backend_v1 *cb_backend_query(uint32_t abi)
{
    if (abi != CB_ABI_V1) return NULL;
#if CB_FIXTURE_MODE == 1
    backend.abi_version++;
#elif CB_FIXTURE_MODE == 2
    backend.struct_size = 8;
#elif CB_FIXTURE_MODE == 3
    backend.receive = NULL;
#endif
    return &backend;
}
#endif
