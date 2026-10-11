# Release tags are documented for humans; checkout uses immutable commit IDs.
set(RKMPP_XRDP_VERSION "0.10.6.1")
set(RKMPP_XRDP_COMMIT "80c52879fead585d6347df5b3c587718adf8f5a5")
set(RKMPP_XORG_VERSION "0.10.5")
set(RKMPP_XORG_COMMIT "c2d3cbafebe00be3221f985c06ef6b97f1c63790")

# Offline caches/mirrors may change the transport, never the pinned revision.
set(XRDP_RKMPP_XRDP_REPOSITORY "https://github.com/neutrinolabs/xrdp.git"
    CACHE STRING "XRDP upstream Git repository or local mirror")
set(XRDP_RKMPP_XORG_REPOSITORY "https://github.com/neutrinolabs/xorgxrdp.git"
    CACHE STRING "xorgxrdp upstream Git repository or local mirror")
