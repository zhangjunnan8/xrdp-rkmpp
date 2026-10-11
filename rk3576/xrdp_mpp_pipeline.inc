/* SPDX-License-Identifier: Apache-2.0
 * Included by xrdp_encoder.c. Only release one captured frame whose entire
 * command batch is STARTFRAME, one raw AVC420 image, ENDFRAME. Other commands,
 * software codecs and complex batches retain upstream lifetime/pacing rules.
 */
struct mpp_input_ready_context
{
    struct xrdp_encoder *encoder;
    int frame_id;
};
static unsigned int mpp_le16(const unsigned char *p)
{ return p[0] | (unsigned int)p[1] << 8; }
static unsigned int mpp_le32(const unsigned char *p)
{ return mpp_le16(p) | mpp_le16(p+2) << 16; }

static int mpp_release_frame_id(XRDP_ENC_DATA *enc)
{
    const unsigned char *p = (const unsigned char *)enc->u.gfx.cmd;
    unsigned int left = enc->u.gfx.cmd_bytes, len, frame;
    const char *enabled = g_getenv("XRDP_MPP_PIPELINE");
    if (!enabled || g_strcmp(enabled, "1") || !p ||
            enc->u.gfx.cmd_bytes < 28 || left > 32768) return 0;
    if (mpp_le16(p) != XR_RDPGFX_CMDID_STARTFRAME || mpp_le32(p+4) != 16) return 0;
    frame = mpp_le32(p+8); p += 16; left -= 16;
    if (left < 17 || mpp_le16(p) != XR_RDPGFX_CMDID_WIRETOSURFACE_1) return 0;
    len = mpp_le32(p+4);
    if (len < 17 || len > left || mpp_le16(p+10) != 0x000b || (mpp_le32(p+13) & 1)) return 0;
    p += len; left -= len;
    if (left != 12 || mpp_le16(p) != XR_RDPGFX_CMDID_ENDFRAME ||
            mpp_le32(p+4) != 12 || mpp_le32(p+8) != frame || frame > INT_MAX) return 0;
    return (int)frame;
}

#ifndef XRDP_MPP_PARSER_ONLY
static void mpp_release_input(void *arg)
{
    struct mpp_input_ready_context *ctx = arg;
    struct xrdp_encoder *self = ctx->encoder;
    XRDP_ENC_DATA_DONE *done = g_new0(XRDP_ENC_DATA_DONE, 1);
    if (!done) return; /* Normal completion will release it instead. */
    ENC_SET_BIT(done->flags, ENC_DONE_FLAGS_MPP_INPUT_BIT);
    done->frame_id = ctx->frame_id;
    tc_mutex_lock(self->mutex);
    fifo_add_item(self->fifo_processed, done);
    tc_mutex_unlock(self->mutex);
    g_set_wait_obj(self->xrdp_encoder_event_processed);
}
#endif
