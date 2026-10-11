/* SPDX-License-Identifier: Apache-2.0 */
#ifndef XRDP_ENCODER_MPP_H
#define XRDP_ENCODER_MPP_H
void *xrdp_encoder_mpp_create(void);
int xrdp_encoder_mpp_delete(void *handle);
/* One-shot notification on the calling encoder thread, after all input bytes
 * have been copied. The callback may immediately overwrite the source image. */
void xrdp_encoder_mpp_set_input_ready(void *handle, void (*ready)(void *), void *arg);
int xrdp_encoder_mpp_encode(void *handle, int session, int left, int top,
                           int width, int height, int twidth, int theight,
                           int format, const char *data,
                           short *crects, int num_crects,
                           char *cdata, int *cdata_bytes, int connection_type,
                           int *flags_ptr);
#endif
