#ifndef CODECBRIDGE_H
#define CODECBRIDGE_H
#include "backend.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cb_plugin cb_plugin;
typedef struct cb_encoder cb_encoder;

CB_API void cb_config_default(cb_config *config);
CB_API const char *cb_status_string(cb_status status);
CB_API cb_status cb_plugin_load(const char *path, cb_plugin **plugin, cb_error *error);
CB_API const char *cb_plugin_name(const cb_plugin *plugin);
CB_API const char *cb_plugin_version(const cb_plugin *plugin);
CB_API cb_status cb_plugin_probe(cb_plugin *plugin, const char *device,
                                 cb_caps *caps, cb_error *error);
CB_API cb_status cb_plugin_unload(cb_plugin *plugin, cb_error *error);
CB_API cb_status cb_encoder_create(cb_plugin *plugin, const char *device,
                                   const cb_config *config, cb_encoder **encoder,
                                   cb_error *error);
CB_API cb_status cb_encoder_submit(cb_encoder *encoder, const cb_frame *frame,
                                   cb_error *error);
CB_API cb_status cb_encoder_receive(cb_encoder *encoder, uint32_t timeout_ms,
                                    cb_packet *packet, cb_error *error);
CB_API cb_status cb_encoder_request_keyframe(cb_encoder *encoder, cb_error *error);
CB_API cb_status cb_encoder_drain(cb_encoder *encoder, cb_error *error);
CB_API cb_status cb_packet_release(cb_encoder *encoder, cb_packet *packet,
                                   cb_error *error);
CB_API cb_status cb_encoder_destroy(cb_encoder *encoder, cb_error *error);

#ifdef __cplusplus
}
#endif
#endif
