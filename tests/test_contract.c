#include "codecbridge/codecbridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(expr) do { checks++; if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
#define RC(expr, expected) CHECK((expr) == (expected))

int main(int argc, char **argv)
{
    CHECK(argc == 6);
    cb_error error;
    cb_plugin *plugin = NULL;
    RC(cb_plugin_load("/no-such-codecbridge-plugin.so", &plugin, &error), CB_ERR_IO);
    CHECK(plugin == NULL && error.message[0]);
    for (int i = 2; i < argc; i++) {
        RC(cb_plugin_load(argv[i], &plugin, &error), CB_ERR_ABI);
        CHECK(plugin == NULL);
    }
    RC(cb_plugin_load(argv[1], &plugin, &error), CB_OK);
    CHECK(!strcmp(cb_plugin_name(plugin), "test-fixture-not-a-codec"));
    cb_caps caps = { .struct_size = sizeof(caps) };
    RC(cb_plugin_probe(plugin, "unavailable", &caps, &error), CB_ERR_DEVICE);
    RC(cb_plugin_probe(plugin, "bad-caps", &caps, &error), CB_ERR_BACKEND);
    RC(cb_plugin_probe(plugin, NULL, &caps, &error), CB_OK);
    CHECK(!(caps.flags & CB_BACKEND_HARDWARE));
    cb_config config;
    cb_config_default(&config);
    config.width = 64; config.height = 48; config.generation = 42;
    cb_encoder *encoder = NULL;
    config.struct_size--;
    RC(cb_encoder_create(plugin, NULL, &config, &encoder, &error), CB_ERR_INVALID);
    config.struct_size++;
    config.width++;
    RC(cb_encoder_create(plugin, NULL, &config, &encoder, &error), CB_ERR_INVALID);
    config.width--;
    config.memory_type = 99;
    RC(cb_encoder_create(plugin, NULL, &config, &encoder, &error), CB_ERR_UNSUPPORTED);
    config.memory_type = CB_MEMORY_CPU;
    RC(cb_encoder_create(plugin, NULL, &config, &encoder, &error), CB_OK);
    RC(cb_plugin_unload(plugin, &error), CB_ERR_BUSY);
    uint8_t pixels[80 * 48 * 3 / 2] = {0};
    cb_frame frame = { .struct_size = sizeof(frame), .format = CB_FORMAT_NV12,
        .memory_type = CB_MEMORY_CPU, .width = 64, .height = 48,
        .planes = { pixels, pixels + 80 * 48 }, .strides = { 80, 80 },
        .plane_bytes = {80 * 48, 80 * 24}, .frame_id = 1, .generation = 42, .pts_us = 0 };
    cb_packet packet = { .struct_size = sizeof(packet) };
    RC(cb_encoder_receive(encoder, 0, &packet, &error), CB_AGAIN);
    RC(cb_encoder_receive(encoder, 1, &packet, &error), CB_ERR_TIMEOUT);
    frame.generation++;
    RC(cb_encoder_submit(encoder, &frame, &error), CB_ERR_INVALID);
    frame.generation--;
    frame.plane_bytes[1] = 1;
    RC(cb_encoder_submit(encoder, &frame, &error), CB_ERR_INVALID);
    frame.plane_bytes[1] = 80 * 24;
    frame.strides[0] = 63;
    RC(cb_encoder_submit(encoder, &frame, &error), CB_ERR_INVALID);
    frame.strides[0] = 80;
    frame.planes[0] = NULL;
    RC(cb_encoder_submit(encoder, &frame, &error), CB_ERR_INVALID);
    frame.planes[0] = pixels;
    frame.frame_id = 900;
    RC(cb_encoder_submit(encoder, &frame, &error), CB_ERR_TIMEOUT);
    frame.frame_id = 1;
    RC(cb_encoder_submit(encoder, &frame, &error), CB_OK);
    frame.pts_us++;
    RC(cb_encoder_submit(encoder, &frame, &error), CB_AGAIN);
    RC(cb_encoder_receive(encoder, 0, &packet, &error), CB_OK);
    CHECK(packet.frame_id == 1 && packet.pts_us == 0 && packet.generation == 42);
    RC(cb_encoder_destroy(encoder, &error), CB_ERR_BUSY);
    RC(cb_encoder_receive(encoder, 0, &packet, &error), CB_ERR_BUSY);
    RC(cb_encoder_submit(encoder, &frame, &error), CB_ERR_BUSY);
    cb_encoder *other = NULL;
    RC(cb_encoder_create(plugin, NULL, &config, &other, &error), CB_OK);
    RC(cb_packet_release(other, &packet, &error), CB_ERR_STATE);
    RC(cb_encoder_destroy(other, &error), CB_OK);
    RC(cb_packet_release(encoder, &packet, &error), CB_OK);
    RC(cb_packet_release(encoder, &packet, &error), CB_ERR_STATE);
    frame.pts_us = 0;
    RC(cb_encoder_submit(encoder, &frame, &error), CB_ERR_INVALID);
    RC(cb_encoder_request_keyframe(encoder, &error), CB_OK);
    frame.pts_us = 1;
    RC(cb_encoder_submit(encoder, &frame, &error), CB_OK);
    RC(cb_encoder_drain(encoder, &error), CB_OK);
    RC(cb_encoder_submit(encoder, &frame, &error), CB_ERR_STATE);
    RC(cb_encoder_request_keyframe(encoder, &error), CB_ERR_STATE);
    RC(cb_encoder_receive(encoder, 0, &packet, &error), CB_OK);
    RC(cb_packet_release(encoder, &packet, &error), CB_OK);
    RC(cb_encoder_receive(encoder, 0, &packet, &error), CB_EOF);
    RC(cb_encoder_receive(encoder, 0, &packet, &error), CB_EOF);
    RC(cb_encoder_drain(encoder, &error), CB_OK);
    RC(cb_encoder_destroy(encoder, &error), CB_OK);
    for (unsigned i = 0; i < 100; i++) {
        RC(cb_encoder_create(plugin, NULL, &config, &encoder, &error), CB_OK);
        frame.frame_id = i & 1 ? 901 : 902; frame.pts_us = 0;
        if (i & 1) RC(cb_encoder_submit(encoder, &frame, &error), CB_ERR_DEVICE);
        else {
            RC(cb_encoder_submit(encoder, &frame, &error), CB_OK);
            RC(cb_encoder_receive(encoder, 0, &packet, &error), CB_ERR_BACKEND);
        }
        RC(cb_encoder_request_keyframe(encoder, &error), CB_ERR_STATE);
        RC(cb_encoder_destroy(encoder, &error), CB_OK);
    }
    RC(cb_plugin_unload(plugin, &error), CB_OK);
    printf("contract checks: %u passed\n", checks);
    return 0;
}
