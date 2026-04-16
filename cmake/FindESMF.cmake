
if (DEFINED ENV{ESMFMKFILE})
  set(ESMFMKFILE $ENV{ESMFMKFILE})
  message("ESMFMKFILE from ENV:   ${ESMFMKFILE}")
elseif (DEFINED ESMFMKFILE)
  message("ESMFMKFILE from CACHE: ${ESMFMKFILE}")
else()
  # Try to find it in common locations
  find_file(ESMFMKFILE esmf.mk PATHS /opt/views/view/lib /usr/lib /usr/local/lib)
  if (NOT ESMFMKFILE)
    message(FATAL_ERROR "ESMFMKFILE not defined and esmf.mk not found")
  endif()
  message("ESMFMKFILE found:      ${ESMFMKFILE}")
endif()

# convert esmf.mk makefile variables to cmake variables until ESMF
# provides proper cmake package
file(STRINGS ${ESMFMKFILE} esmf_mk_text)
foreach(line ${esmf_mk_text})
  string(REGEX REPLACE "^[ ]+" "" line ${line}) # strip leading spaces
  if (line MATCHES "^ESMF_*")                   # process only line starting with ESMF_
    string(REGEX MATCH "^ESMF_[^=]+" esmf_name ${line})
    string(REPLACE "${esmf_name}=" "" emsf_value ${line})
    set(${esmf_name} "${emsf_value}")
  endif()
endforeach()
string(REPLACE "-I" "" ESMF_F90COMPILEPATHS ${ESMF_F90COMPILEPATHS})
string(REPLACE " " ";" ESMF_F90COMPILEPATHS ${ESMF_F90COMPILEPATHS})

# Parse ESMF link paths and libs into CMake lists
if(DEFINED ESMF_F90LINKPATHS)
  string(REPLACE " " ";" ESMF_F90LINKPATHS "${ESMF_F90LINKPATHS}")
endif()
if(DEFINED ESMF_F90ESMFLINKRPATHS)
  string(REPLACE " " ";" ESMF_F90ESMFLINKRPATHS "${ESMF_F90ESMFLINKRPATHS}")
endif()
if(DEFINED ESMF_F90ESMFLINKLIBS)
  string(REPLACE " " ";" ESMF_F90ESMFLINKLIBS "${ESMF_F90ESMFLINKLIBS}")
  # Remove empty list entries
  list(FILTER ESMF_F90ESMFLINKLIBS EXCLUDE REGEX "^$")
endif()

# We use only these 4 variables in our build system. Make sure they are all set
if(ESMF_VERSION_MAJOR AND
   ESMF_F90COMPILEPATHS AND
   ESMF_F90ESMFLINKRPATHS AND
   ESMF_F90ESMFLINKLIBS)
  message(" Found ESMF:")
  message("ESMF_VERSION_MAJOR:     ${ESMF_VERSION_MAJOR}")
  message("ESMF_F90COMPILEPATHS:   ${ESMF_F90COMPILEPATHS}")
  message("ESMF_F90ESMFLINKRPATHS: ${ESMF_F90ESMFLINKRPATHS}")
  message("ESMF_F90ESMFLINKLIBS:   ${ESMF_F90ESMFLINKLIBS}")
else()
  message("One of the ESMF_ variables is not defined")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ESMF
                                    FOUND_VAR
                                      ESMF_FOUND
                                    REQUIRED_VARS
                                      ESMF_F90COMPILEPATHS
                                      ESMF_F90ESMFLINKRPATHS
                                      ESMF_F90ESMFLINKLIBS
                                    VERSION_VAR
                                      ESMF_VERSION_STRING)

# ---------------------------------------------------------------------------
# Extract PIO paths from esmf.mk (ESMF is built against PIO)
# ---------------------------------------------------------------------------
# ESMF_F90COMPILEPATHS already includes the PIO include directory.
# Parse ESMF_F90LINKPATHS for -L dirs and ESMF_F90LINKLIBS for -l libs
# to build PIO_Fortran_INCLUDE_DIR and PIO link targets.

# Extract PIO include dir from the compile paths (contains pio.mod)
set(PIO_Fortran_INCLUDE_DIR "")
foreach(_incdir ${ESMF_F90COMPILEPATHS})
  if(EXISTS "${_incdir}/pio.mod" OR EXISTS "${_incdir}/pio.h")
    set(PIO_Fortran_INCLUDE_DIR "${_incdir}")
    break()
  endif()
endforeach()

# Extract PIO library directory from ESMF_F90LINKPATHS
set(PIO_LIBRARY_DIR "")
if(DEFINED ESMF_F90LINKPATHS)
  string(REPLACE "-L" "" _linkpaths "${ESMF_F90LINKPATHS}")
  string(REPLACE " " ";" _linkpaths "${_linkpaths}")
  foreach(_libdir ${_linkpaths})
    if(EXISTS "${_libdir}/libpiof.so" OR EXISTS "${_libdir}/libpiof.a" OR
       EXISTS "${_libdir}/libpioc.so" OR EXISTS "${_libdir}/libpioc.a")
      set(PIO_LIBRARY_DIR "${_libdir}")
      break()
    endif()
  endforeach()
endif()

# Find the actual library files
find_library(PIO_Fortran_LIBRARY NAMES piof HINTS ${PIO_LIBRARY_DIR})
find_library(PIO_C_LIBRARY NAMES pioc HINTS ${PIO_LIBRARY_DIR})

# Create imported targets for PIO
if(PIO_Fortran_LIBRARY AND PIO_C_LIBRARY AND PIO_Fortran_INCLUDE_DIR)
  set(PIO_FOUND TRUE)
  message(STATUS "Found PIO via esmf.mk:")
  message(STATUS "  PIO_Fortran_INCLUDE_DIR: ${PIO_Fortran_INCLUDE_DIR}")
  message(STATUS "  PIO_Fortran_LIBRARY:     ${PIO_Fortran_LIBRARY}")
  message(STATUS "  PIO_C_LIBRARY:           ${PIO_C_LIBRARY}")

  if(NOT TARGET PIO::PIO_Fortran)
    add_library(PIO::PIO_Fortran SHARED IMPORTED)
    set_target_properties(PIO::PIO_Fortran PROPERTIES
      IMPORTED_LOCATION "${PIO_Fortran_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${PIO_Fortran_INCLUDE_DIR}"
    )
  endif()
  if(NOT TARGET PIO::PIO_C)
    add_library(PIO::PIO_C SHARED IMPORTED)
    set_target_properties(PIO::PIO_C PROPERTIES
      IMPORTED_LOCATION "${PIO_C_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${PIO_Fortran_INCLUDE_DIR}"
    )
  endif()
else()
  set(PIO_FOUND FALSE)
  message(WARNING "PIO not found via esmf.mk. PIO_Fortran_INCLUDE_DIR=${PIO_Fortran_INCLUDE_DIR} PIO_LIBRARY_DIR=${PIO_LIBRARY_DIR}")
endif()
