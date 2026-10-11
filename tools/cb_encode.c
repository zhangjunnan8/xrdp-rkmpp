#include "codecbridge/codecbridge.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t now_ns(clockid_t clock)
{
    struct timespec ts;
    if (clock_gettime(clock, &ts)) { perror("clock_gettime"); exit(1); }
    return (uint64_t)ts.tv_sec * 1000000000 + (uint64_t)ts.tv_nsec;
}

static void json_string(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') printf("\\%c", ch);
        else if (ch < 32) printf("\\u%04x", ch);
        else putchar(ch);
    }
    putchar('"');
}

static void usage(void)
{
    puts("cb-encode probe PLUGIN.so\n"
         "cb-encode encode --plugin PLUGIN.so --output VIDEO.h264 [options]\n"
         "  --width N --height N --frames N --fps N --qp 1..51\n"
         "  --padding N --keyframe-every N --generation N\n"
         "  --range full|limited --matrix bt709|bt601 --raw-output FRAMES.nv12\n"
         "Files must not already exist. Input is a deterministic moving NV12 pattern.\n"
         "Runs unpaced; reported throughput is not remote desktop FPS.");
}

static int number(const char *s, uint32_t *out)
{
    char *end;
    if (!*s || *s == '-') return 0;
    errno = 0;
    unsigned long value = strtoul(s, &end, 10);
    if (errno || *end || value > UINT32_MAX) return 0;
    *out = (uint32_t)value;
    return 1;
}

static int compare_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static void pattern(uint8_t *buffer, uint32_t width, uint32_t height,
                    uint32_t stride, uint32_t index, uint32_t range)
{
    uint8_t *uv = buffer + (size_t)stride * height;
    memset(buffer, 0xa5, (size_t)stride * height * 3 / 2);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t value = (x + y + index * 3) & 255;
            if (x / 16 == (index * 7 % width) / 16) value = 230;
            buffer[(size_t)y * stride + x] = range == CB_RANGE_FULL ?
                (uint8_t)value : (uint8_t)(16 + value * 219 / 255);
        }
    }
    for (uint32_t y = 0; y < height / 2; y++) {
        for (uint32_t x = 0; x < width; x += 2) {
            uv[(size_t)y * stride + x] = (uint8_t)(80 + (index + y / 16) % 80);
            uv[(size_t)y * stride + x + 1] = (uint8_t)(80 + (index + x / 16) % 80);
        }
    }
}

static cb_status receive_bounded(cb_encoder *encoder, cb_packet *packet, cb_error *error)
{
    uint64_t deadline = now_ns(CLOCK_MONOTONIC) + UINT64_C(5000000000);
    cb_status rc;
    do {
        rc = cb_encoder_receive(encoder, 100, packet, error);
        if (rc != CB_AGAIN && rc != CB_ERR_TIMEOUT) return rc;
        struct timespec wait = { .tv_nsec = 1000000 };
        nanosleep(&wait, NULL);
    } while (now_ns(CLOCK_MONOTONIC) < deadline);
    error->code = CB_ERR_TIMEOUT;
    snprintf(error->message, sizeof(error->message), "no packet within 5 seconds");
    return CB_ERR_TIMEOUT;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--help")) { usage(); return 0; }
    if (argc < 3) { usage(); return 2; }
    int probing = !strcmp(argv[1], "probe");
    const char *plugin_path = NULL, *output_path = NULL, *raw_path = NULL;
    cb_config config;
    cb_config_default(&config);
    config.generation = 1;
    uint32_t frames = 60, padding = 0, keyframe_every = 0;
    if (probing) {
        if (argc != 3) { usage(); return 2; }
        plugin_path = argv[2];
    } else {
        if (strcmp(argv[1], "encode")) { usage(); return 2; }
        for (int i = 2; i < argc; i += 2) {
            if (i + 1 >= argc) { usage(); return 2; }
            const char *option = argv[i], *value = argv[i + 1];
            uint32_t *destination = NULL;
            if (!strcmp(option, "--plugin")) plugin_path = value;
            else if (!strcmp(option, "--output")) output_path = value;
            else if (!strcmp(option, "--raw-output")) raw_path = value;
            else if (!strcmp(option, "--width")) destination = &config.width;
            else if (!strcmp(option, "--height")) destination = &config.height;
            else if (!strcmp(option, "--frames")) destination = &frames;
            else if (!strcmp(option, "--fps")) destination = &config.fps_num;
            else if (!strcmp(option, "--qp")) destination = &config.qp;
            else if (!strcmp(option, "--padding")) destination = &padding;
            else if (!strcmp(option, "--keyframe-every")) destination = &keyframe_every;
            else if (!strcmp(option, "--generation")) {
                uint32_t generation;
                if (!number(value, &generation)) { usage(); return 2; }
                config.generation = generation;
            } else if (!strcmp(option, "--range")) {
                if (!strcmp(value, "full")) config.color_range = CB_RANGE_FULL;
                else if (!strcmp(value, "limited")) config.color_range = CB_RANGE_LIMITED;
                else { usage(); return 2; }
            } else if (!strcmp(option, "--matrix")) {
                if (!strcmp(value, "bt709")) config.color_matrix = CB_MATRIX_BT709;
                else if (!strcmp(value, "bt601")) config.color_matrix = CB_MATRIX_BT601;
                else { usage(); return 2; }
            } else { fprintf(stderr, "unknown option: %s\n", option); return 2; }
            if (destination && !number(value, destination)) { usage(); return 2; }
        }
        if (!plugin_path || !output_path || !frames || frames > 1000000 ||
            padding > 65536 || !config.fps_num || config.fps_num > 240) {
            usage(); return 2;
        }
    }
    cb_error error = {0};
    cb_plugin *plugin = NULL;
    cb_encoder *encoder = NULL;
    cb_packet packet = { .struct_size = sizeof(packet) };
    cb_caps caps = { .struct_size = sizeof(caps) };
    FILE *output = NULL, *raw = NULL;
    uint8_t *buffer = NULL;
    uint64_t *latencies = NULL;
    int exit_code = 1;
    cb_status rc = cb_plugin_load(plugin_path, &plugin, &error);
    if (rc != CB_OK) goto done;
    rc = cb_plugin_probe(plugin, NULL, &caps, &error);
    if (rc != CB_OK) goto done;
    if (probing) {
        printf("{\"backend\":"); json_string(cb_plugin_name(plugin));
        printf(",\"version\":"); json_string(cb_plugin_version(plugin));
        printf(",\"hardware\":%s,\"min_width\":%u,\"min_height\":%u,"
               "\"max_width\":%u,\"max_height\":%u,\"max_inflight\":%u}\n",
               caps.flags & CB_BACKEND_HARDWARE ? "true" : "false",
               caps.min_width, caps.min_height, caps.max_width, caps.max_height,
               caps.max_inflight);
        exit_code = 0;
        goto done;
    }
    rc = cb_encoder_create(plugin, NULL, &config, &encoder, &error);
    if (rc != CB_OK) goto done;
    uint32_t stride = config.width + padding;
    size_t plane_size = (size_t)stride * config.height;
    buffer = malloc(plane_size * 3 / 2);
    latencies = calloc(frames, sizeof(*latencies));
    if (!buffer || !latencies) { rc = CB_ERR_NOMEM; goto done; }
    output = fopen(output_path, "wbx");
    if (raw_path) raw = fopen(raw_path, "wbx");
    if (!output || (raw_path && !raw)) { perror("creating output"); rc = CB_ERR_IO; goto done; }
    uint64_t total_bytes = 0, encode_ns = 0, keyframes = 0;
    uint64_t wall_start = now_ns(CLOCK_MONOTONIC), cpu_start = now_ns(CLOCK_PROCESS_CPUTIME_ID);
    for (uint32_t i = 0; i < frames; i++) {
        pattern(buffer, config.width, config.height, stride, i, config.color_range);
        cb_frame frame = {
            .struct_size = sizeof(frame), .format = CB_FORMAT_NV12,
            .memory_type = CB_MEMORY_CPU, .width = config.width, .height = config.height,
            .planes = { buffer, buffer + plane_size },
            .plane_bytes = { plane_size, plane_size / 2 }, .strides = { stride, stride },
            .frame_id = i + 1, .generation = config.generation,
            .pts_us = (int64_t)((uint64_t)i * 1000000 / config.fps_num)
        };
        if (raw) {
            for (unsigned plane = 0; plane < 2; plane++) {
                uint32_t rows = plane ? config.height / 2 : config.height;
                for (uint32_t y = 0; y < rows; y++) {
                    if (fwrite(frame.planes[plane] + (size_t)y * stride, 1, config.width, raw) != config.width) {
                        rc = CB_ERR_IO; goto done;
                    }
                }
            }
        }
        if (i && keyframe_every && i % keyframe_every == 0) {
            rc = cb_encoder_request_keyframe(encoder, &error);
            if (rc != CB_OK) goto done;
        }
        uint64_t start = now_ns(CLOCK_MONOTONIC);
        rc = cb_encoder_submit(encoder, &frame, &error);
        if (rc != CB_OK) goto done;
        rc = receive_bounded(encoder, &packet, &error);
        if (rc != CB_OK) goto done;
        latencies[i] = now_ns(CLOCK_MONOTONIC) - start;
        encode_ns += latencies[i];
        if (packet.frame_id != frame.frame_id || packet.pts_us != frame.pts_us ||
            packet.generation != config.generation ||
            ((i == 0 || (keyframe_every && i % keyframe_every == 0)) && !(packet.flags & CB_PACKET_KEYFRAME))) {
            snprintf(error.message, sizeof(error.message), "packet identity or requested keyframe mismatch");
            rc = CB_ERR_BACKEND; goto done;
        }
        total_bytes += packet.size;
        if (packet.flags & CB_PACKET_KEYFRAME) keyframes++;
        if (fwrite(packet.data, 1, (size_t)packet.size, output) != packet.size) {
            rc = CB_ERR_IO; goto done;
        }
        rc = cb_packet_release(encoder, &packet, &error);
        if (rc != CB_OK) goto done;
    }
    rc = cb_encoder_drain(encoder, &error);
    if (rc != CB_OK) goto done;
    rc = receive_bounded(encoder, &packet, &error);
    if (rc != CB_EOF) {
        snprintf(error.message, sizeof(error.message), "unexpected output after zero-delay drain");
        rc = CB_ERR_BACKEND; goto done;
    }
    uint64_t wall_ns = now_ns(CLOCK_MONOTONIC) - wall_start;
    uint64_t cpu_ns = now_ns(CLOCK_PROCESS_CPUTIME_ID) - cpu_start;
    int io_error = fclose(output); output = NULL;
    if (raw) { if (fclose(raw)) io_error = 1; raw = NULL; }
    if (io_error) { rc = CB_ERR_IO; goto done; }
    qsort(latencies, frames, sizeof(*latencies), compare_u64);
    printf("{\"backend\":"); json_string(cb_plugin_name(plugin));
    printf(",\"hardware\":%s,\"width\":%u,\"height\":%u,\"frames\":%u,"
           "\"bytes\":%" PRIu64 ",\"keyframes\":%" PRIu64 ","
           "\"generation\":%" PRIu64 ",\"encode_mean_ms\":%.3f,"
           "\"encode_p50_ms\":%.3f,\"encode_p95_ms\":%.3f,"
           "\"wall_ms\":%.3f,\"cpu_ms\":%.3f,\"unpaced_fps\":%.3f}\n",
           caps.flags & CB_BACKEND_HARDWARE ? "true" : "false",
           config.width, config.height, frames, total_bytes, keyframes, config.generation,
           (double)encode_ns / frames / 1000000.0,
           (double)latencies[(frames - 1) / 2] / 1000000.0,
           (double)latencies[((uint64_t)frames * 95 + 99) / 100 - 1] / 1000000.0,
           (double)wall_ns / 1000000.0, (double)cpu_ns / 1000000.0,
           (double)frames * 1000000000.0 / (double)(wall_ns ? wall_ns : 1));
    exit_code = 0;
done:
    if (exit_code) fprintf(stderr, "error[%d] %s: %s\n", rc, cb_status_string(rc), error.message);
    if (packet.data && encoder) cb_packet_release(encoder, &packet, NULL);
    if (encoder) cb_encoder_destroy(encoder, NULL);
    if (plugin) cb_plugin_unload(plugin, NULL);
    if (output) fclose(output);
    if (raw) fclose(raw);
    free(latencies);
    free(buffer);
    return exit_code;
}
