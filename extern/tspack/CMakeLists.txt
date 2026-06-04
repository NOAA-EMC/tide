# TSPACK - Tension Spline Package (C version)
# Vendored from https://github.com/noaa-oar-arl/pytspack/tree/master/src
#
# Original algorithm by Robert J. Renka (ACM TOMS Algorithm 716).
# C translation by NOAA OAR ARL.

add_library(tspack STATIC tspack.c)
target_include_directories(tspack PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)
set_target_properties(tspack PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON
)

install(TARGETS tspack ARCHIVE DESTINATION lib)
install(FILES tspack.h DESTINATION include)
