/* SPDX-License-Identifier: Apache-2.0
 * Standalone tests use the real config parser with a staged configuration.
 * Linked only into test executables with --wrap=tconfig_load_gfx.
 */
#include <config_ac.h>
#include <stdlib.h>
#include "xrdp_tconfig.h"

#ifndef XRDP_TEST_GFX_CONFIG_DEFAULT
#error "The test build must provide its staged gfx.toml path"
#endif

int __real_tconfig_load_gfx(const char *filename,
                          struct xrdp_tconfig_gfx *config);

int __wrap_tconfig_load_gfx(const char *filename,
                          struct xrdp_tconfig_gfx *config)
{
    const char *path = getenv("XRDP_TEST_GFX_CONFIG");
    (void)filename;
    if (path == NULL || *path == '\0')
    {
        path = XRDP_TEST_GFX_CONFIG_DEFAULT;
    }
    return __real_tconfig_load_gfx(path, config);
}
