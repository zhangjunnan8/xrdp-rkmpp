#include "codecbridge/codecbridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(expr) do { checks++; if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
#define RC(expr, expected) CHECK((expr) == (expected))

static int has_nal(const cb_packet *packet, uint8_t type)
{
    for (uint64_t i = 0; i + 3 < packet->size; i++) {
        if (!packet->data[i] && !packet->data[i + 1] && packet->data[i + 2] == 1 &&
            (packet->data[i + 3] & 31) == type) return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    cb_error error;
    cb_plugin *plugin = NULL;
    RC(cb_plugin_load(argv[1], &plugin, &error), CB_OK);
    cb_caps caps = { .struct_size = sizeof(caps) };
    RC(cb_plugin_probe(plugin, NULL, &caps, &error), CB_OK);
    CHECK(!(caps.flags & CB_BACKEND_HARDWARE) && caps.max_inflight == 1);
    RC(cb_plugin_probe(plugin, "/dev/not-supported", &caps, &error), CB_ERR_UNSUPPORTED);
    for (unsigned cycle = 0; cycle < 40; cycle++) {
        cb_config config;
        cb_config_default(&config);
        config.width = cycle & 1 ? 318 : 64;
        config.height = cycle & 1 ? 242 : 48;
        config.generation = cycle + 1;
        config.threads = cycle & 1 ? 1 : 2;
        cb_encoder *encoder = NULL;
        RC(cb_encoder_create(plugin, NULL, &config, &encoder, &error), CB_OK);
        uint32_t stride = config.width + 19;
        size_t ybytes = (size_t)stride * config.height;
        uint8_t *pixels = malloc(ybytes * 3 / 2);
        CHECK(pixels != NULL);
        memset(pixels, 128, ybytes * 3 / 2);
        cb_frame frame = { .struct_size = sizeof(frame), .format = CB_FORMAT_NV12,
            .memory_type = CB_MEMORY_CPU, .width = config.width, .height = config.height,
            .planes = {pixels, pixels + ybytes}, .strides = {stride, stride},
            .plane_bytes = {ybytes, ybytes / 2}, .generation = config.generation };
        cb_packet packet = { .struct_size = sizeof(packet) };
        RC(cb_encoder_receive(encoder, 0, &packet, &error), CB_AGAIN);
        for (unsigned i = 0; i < 5; i++) {
            frame.frame_id = i + 100; frame.pts_us = (int64_t)i * 33333;
            if (i == 3) RC(cb_encoder_request_keyframe(encoder, &error), CB_OK);
            RC(cb_encoder_submit(encoder, &frame, &error), CB_OK);
            /* The caller may immediately reuse the submitted input. */
            pixels[0]++;
            frame.pts_us++;
            RC(cb_encoder_submit(encoder, &frame, &error), CB_AGAIN);
            frame.pts_us--;
            RC(cb_encoder_receive(encoder, 100, &packet, &error), CB_OK);
            CHECK(packet.frame_id == frame.frame_id && packet.pts_us == frame.pts_us &&
                  packet.generation == config.generation);
            CHECK(has_nal(&packet, 9));
            if (i == 0 || i == 3) {
                CHECK(packet.flags & CB_PACKET_KEYFRAME);
                CHECK(has_nal(&packet, 5) && has_nal(&packet, 7) && has_nal(&packet, 8));
            } else {
                CHECK(!(packet.flags & CB_PACKET_KEYFRAME));
                CHECK(has_nal(&packet, 1));
            }
            RC(cb_packet_release(encoder, &packet, &error), CB_OK);
        }
        if (cycle & 1) {
            RC(cb_encoder_drain(encoder, &error), CB_OK);
            RC(cb_encoder_receive(encoder, 100, &packet, &error), CB_EOF);
        } else {
            /* Destroy must also reclaim an encoded packet not yet received. */
            frame.pts_us++; frame.frame_id++;
            RC(cb_encoder_submit(encoder, &frame, &error), CB_OK);
        }
        free(pixels);
        RC(cb_encoder_destroy(encoder, &error), CB_OK);
    }
    RC(cb_plugin_unload(plugin, &error), CB_OK);
    printf("x264 lifecycle checks: %u passed\n", checks);
    return 0;
}
