#ifndef CODECBRIDGE_BACKEND_H
#define CODECBRIDGE_BACKEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__)
#define CB_API __attribute__((visibility("default")))
#else
#define CB_API
#endif

#define CB_ABI_V1 UINT32_C(0x00010000)
#define CB_BACKEND_ENTRY "cb_backend_query"

typedef int32_t cb_status;
enum {
    CB_OK = 0, CB_AGAIN = 1, CB_EOF = 2,
    CB_ERR_INVALID = -1, CB_ERR_UNSUPPORTED = -2, CB_ERR_ABI = -3,
    CB_ERR_IO = -4, CB_ERR_NOMEM = -5, CB_ERR_BUSY = -6,
    CB_ERR_TIMEOUT = -7, CB_ERR_DEVICE = -8, CB_ERR_STATE = -9,
    CB_ERR_BACKEND = -10
};
enum { CB_CODEC_H264 = 1, CB_FORMAT_NV12 = 1, CB_MEMORY_CPU = 1 };
enum { CB_PROFILE_BASELINE = 1, CB_MATRIX_BT709 = 1, CB_MATRIX_BT601 = 2 };
enum { CB_RANGE_LIMITED = 1, CB_RANGE_FULL = 2 };
enum { CB_BACKEND_HARDWARE = 1, CB_PACKET_KEYFRAME = 1 };

typedef struct cb_error {
    int32_t code;
    char message[256];
} cb_error;

typedef struct cb_caps {
    uint32_t struct_size;
    uint32_t flags;
    uint32_t codecs;
    uint32_t formats;
    uint32_t memory_types;
    uint32_t profiles;
    uint32_t min_width, min_height;
    uint32_t max_width, max_height;
    uint32_t width_alignment, height_alignment;
    uint32_t max_inflight;
} cb_caps;

/* V1: constant QP, complete CPU NV12 frames, H.264 Annex B access units.
 * Change dimensions/colour/generation by creating a new encoder. */
typedef struct cb_config {
    uint32_t struct_size;
    uint32_t codec, format, memory_type, profile;
    uint32_t width, height;
    uint32_t fps_num, fps_den;
    uint32_t qp, gop, threads;
    uint32_t color_matrix, color_range;
    uint64_t generation;
} cb_config;

typedef struct cb_frame {
    uint32_t struct_size;
    uint32_t format, memory_type;
    uint32_t width, height;
    const uint8_t *planes[2];
    uint64_t plane_bytes[2];
    uint32_t strides[2];
    uint64_t frame_id, generation;
    int64_t pts_us;
} cb_frame;

/* Release before another receive. Never free data directly. */
typedef struct cb_packet {
    uint32_t struct_size;
    uint32_t flags;
    const uint8_t *data;
    uint64_t size;
    uint64_t frame_id, generation;
    int64_t pts_us;
    void *cookie;
} cb_packet;

/* Calls on a plugin and its encoders must be externally serialized.
 * A successful submit consumes/copies input before returning: the caller
 * may immediately reuse its planes. Non-OK submit does not accept input.
 * receive waits at most timeout_ms for backend work; idle may return AGAIN
 * immediately. This is cooperative, not process-level timeout isolation.
 * DEVICE/BACKEND errors invalidate a session. Release held packets, destroy,
 * and create a new session/generation to recover. */
typedef struct cb_backend_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    const char *name;
    const char *version;
    cb_status (*probe)(const char *device, cb_caps *caps, cb_error *error);
    cb_status (*create)(const char *device, const cb_config *config,
                        void **encoder, cb_error *error);
    cb_status (*submit)(void *encoder, const cb_frame *frame, cb_error *error);
    cb_status (*receive)(void *encoder, uint32_t timeout_ms,
                         cb_packet *packet, cb_error *error);
    cb_status (*request_keyframe)(void *encoder, cb_error *error);
    cb_status (*drain)(void *encoder, cb_error *error);
    void (*release_packet)(void *encoder, cb_packet *packet);
    void (*destroy)(void *encoder);
} cb_backend_v1;

typedef const cb_backend_v1 *(*cb_backend_query_fn)(uint32_t requested_abi);

#ifdef __cplusplus
}
#endif
#endif
