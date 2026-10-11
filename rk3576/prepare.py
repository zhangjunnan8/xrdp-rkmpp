#!/usr/bin/env python3
"""Apply the board-only integration to a pristine XRDP 0.10.6.1 release tree."""
import pathlib
import shutil
import sys

src = pathlib.Path(sys.argv[1])
here = pathlib.Path(__file__).resolve().parent

def replace(path, before, after):
    p = src / path
    text = p.read_text()
    assert text.count(before) == 1, (path, "unexpected upstream source")
    p.write_text(text.replace(before, after))

replace("configure.ac", "AM_CONDITIONAL(XRDP_X264, [test x$enable_x264 = xyes])", """AM_CONDITIONAL(XRDP_X264, [test x$enable_x264 = xyes])
AC_ARG_ENABLE(mpp, AS_HELP_STRING([--enable-mpp],
              [RK3576 direct Rockchip MPP encoder (requires --enable-x264)]),
              [], [enable_mpp=no])
AM_CONDITIONAL(XRDP_MPP, [test x$enable_mpp = xyes])
AS_IF([test x$enable_mpp = xyes], [
    AS_IF([test x$enable_x264 != xyes], [AC_MSG_ERROR([MPP requires x264 fallback])])
    PKG_CHECK_MODULES(XRDP_MPP, rockchip_mpp)
])""")
replace("xrdp/Makefile.am", "if XRDP_X264", """if XRDP_MPP
AM_CPPFLAGS += -DXRDP_MPP $(XRDP_MPP_CFLAGS)
XRDP_EXTRA_LIBS += $(XRDP_MPP_LIBS)
XRDP_EXTRA_SOURCES += xrdp_encoder_mpp.c xrdp_encoder_mpp.h
endif

if XRDP_X264""")
replace("xrdp/xrdp_encoder.c", '#include "xrdp_encoder_x264.h"', '''#include "xrdp_encoder_x264.h"
#ifdef XRDP_MPP
#include "xrdp_encoder_mpp.h"
#endif''')
replace("xrdp/xrdp_encoder.c", "    const char *encoder_name = NULL;", """    const char *encoder_name = NULL;
#ifdef XRDP_MPP
    struct xrdp_tconfig_gfx mpp_config;
    tconfig_load_gfx(GFX_CONF, &mpp_config);
    if (mpp_config.h264_encoder == XTC_H264_MPP)
    {
        self->xrdp_encoder_h264_create = xrdp_encoder_mpp_create;
        self->xrdp_encoder_h264_delete = xrdp_encoder_mpp_delete;
        self->xrdp_encoder_h264_encode = xrdp_encoder_mpp_encode;
        LOG(LOG_LEVEL_INFO, "xrdp_encoder_create: selected RK3576 MPP hardware encoder");
        return;
    }
#endif""")
replace("xrdp/xrdp_tconfig.h", "    XTC_H264_OPENH264\n", "    XTC_H264_OPENH264,\n    XTC_H264_MPP\n")
replace("xrdp/xrdp_tconfig.c", "            free(h264_encoder.u.s);", """            if (g_strcasecmp(h264_encoder.u.s, "MPP") == 0)
            {
                valid_encoder_found = 1;
                config->h264_encoder = XTC_H264_MPP;
                TCLOG(LOG_LEVEL_DEBUG, "[codec] h264_encoder = MPP");
            }
            free(h264_encoder.u.s);""")
# Require valid software configuration for the recovery path too.
replace("xrdp/xrdp_tconfig.c", "x264_loaded != 0 && config->h264_encoder == XTC_H264_X264",
        "x264_loaded != 0 && (config->h264_encoder == XTC_H264_X264 || config->h264_encoder == XTC_H264_MPP)")
replace("xrdp/xrdp_encoder.h", "#define ENC_DONE_FLAGS_FRAME_ID_BIT 1",
        "#define ENC_DONE_FLAGS_FRAME_ID_BIT 1\n#define ENC_DONE_FLAGS_MPP_INPUT_BIT 2")
replace("xrdp/xrdp_encoder.c", "static struct stream *\ngfx_wiretosurface1", """#ifdef XRDP_MPP
#include <limits.h>
#include "xrdp_mpp_pipeline.inc"
#endif

static struct stream *
gfx_wiretosurface1""")
replace("xrdp/xrdp_encoder.c", "    int connection_type;", """    int connection_type;
#ifdef XRDP_MPP
    struct mpp_input_ready_context mpp_ready = {self, 0};
#endif""")
replace("xrdp/xrdp_encoder.c", "        error = self->xrdp_encoder_h264_encode(", """#ifdef XRDP_MPP
        if (self->xrdp_encoder_h264_encode == xrdp_encoder_mpp_encode)
        {
            mpp_ready.frame_id = mpp_release_frame_id(enc);
            if (mpp_ready.frame_id > 0)
                xrdp_encoder_mpp_set_input_ready(self->codec_handle_h264_gfx[mon_index],
                                                mpp_release_input, &mpp_ready);
        }
#endif
        error = self->xrdp_encoder_h264_encode(""")
replace("xrdp/xrdp_encoder.c", "                    connection_type, NULL);\n        if (error == 0)", """                    connection_type, NULL);
#ifdef XRDP_MPP
        if (self->xrdp_encoder_h264_encode == xrdp_encoder_mpp_encode)
            xrdp_encoder_mpp_set_input_ready(self->codec_handle_h264_gfx[mon_index], NULL, NULL);
#endif
        if (error == 0)""")
replace("xrdp/xrdp_mm.c", "        is_gfx = ENC_IS_BIT_SET(enc_done->flags, ENC_DONE_FLAGS_GFX_BIT);", """        if (ENC_IS_BIT_SET(enc_done->flags, ENC_DONE_FLAGS_MPP_INPUT_BIT))
        {
            /* Input is owned by MPP now. Keep the existing bounded client
             * window; this is a capture-buffer ACK, never a fake client ACK. */
            if (self->encoder->gfx_ack_off)
            {
                if (self->mod != NULL)
                    self->mod->mod_frame_ack(self->mod, 0, enc_done->frame_id);
            }
            else
            {
                self->encoder->frame_id_server = enc_done->frame_id;
                xrdp_mm_update_module_frame_ack(self);
            }
            g_free(enc_done);
            continue;
        }
        is_gfx = ENC_IS_BIT_SET(enc_done->flags, ENC_DONE_FLAGS_GFX_BIT);""")
for name in ("xrdp_encoder_mpp.c", "xrdp_encoder_mpp.h", "xrdp_mpp_pipeline.inc"):
    shutil.copyfile(here / name, src / "xrdp" / name)
print("Prepared XRDP direct MPP integration:", src)
