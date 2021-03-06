# Try to find HDF5 libraries and headers.
# Usage of this module:
#
# find_package(hdf5)
#
# Variables defined by this module:
#
#  HDF5_FOUND
#  HDF5_LIBRARIES
#  HDF5_INCLUDE_DIRS


if(NOT HDF5_PREFIX AND NOT $ENV{HDF5_BASE} STREQUAL "")
	set(HDF5_PREFIX $ENV{HDF5_BASE})
  message(STATUS "Searching for HDF5 library in path " ${HDF5_PREFIX})
endif()
if(NOT HDF5_PREFIX)
	set(HDF5_PREFIX "/usr/")
endif()

message(STATUS "Trying to find HDF5 using cmake package")
# set(HDF5_USE_STATIC_LIBRARIES ON)
set(HDF5_PREFER_PARALLEL ON)
find_package (
  HDF5
# NAMES hdf5 COMPONENTS C
)

if(NOT HDF5_IS_PARALLEL)
	message(STATUS "HDF5 cmake module provides only serial version")
	set(HDF5_FOUND OFF CACHE BOOL "HDF5_FOUND" FORCE)
	unset(HDF5_LIBRARIES)
	unset(HDF5_INCLUDE_DIRS)
endif()

if(NOT HDF5_FOUND)
	message(STATUS "HDF5 package not found, try to find libs")
endif()

find_library(
	HDF5_LIBRARIES
	NAMES hdf5 libhdf5
	HINTS ${HDF5_PREFIX}/lib
# NO_DEFAULT_PATH
)

find_path(
	HDF5_INCLUDE_DIRS
	NAMES hdf5.h
	HINTS ${HDF5_PREFIX}/include
# NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
  HDF5 DEFAULT_MSG
  HDF5_LIBRARIES
  HDF5_INCLUDE_DIRS
)

# set flags
if(HDF5_FOUND)
#  set (HDF5_LINKER_FLAGS "-lhdf5_hl -lhdf5 -ldl -lm -lz")
   set (HDF5_LINKER_FLAGS $ENV{HDF5_LIB} ${SZIP_LIB} -lz)
endif()

mark_as_advanced(
	HDF5_LIBRARIES
	HDF5_INCLUDE_DIRS
)

if (HDF5_FOUND)
	message(STATUS "HDF5 includes:   " ${HDF5_INCLUDE_DIRS})
	message(STATUS "HDF5 libraries:  " ${HDF5_LIBRARIES})
endif()
