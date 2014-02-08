# - Try to find the libairspy library
# Once done this defines
#
#  LIBAIRSPY_FOUND - system has libairspy
#  LIBAIRSPY_INCLUDE_DIR - the libairspy include directory
#  LIBAIRSPY_LIBRARIES - Link these to use libairspy

# Copyright (c) 2013  Benjamin Vernoux
#


if (LIBAIRSPY_INCLUDE_DIR AND LIBAIRSPY_LIBRARIES)

  # in cache already
  set(LIBAIRSPY_FOUND TRUE)

else (LIBAIRSPY_INCLUDE_DIR AND LIBAIRSPY_LIBRARIES)
  IF (NOT WIN32)
    # use pkg-config to get the directories and then use these values
    # in the FIND_PATH() and FIND_LIBRARY() calls
    find_package(PkgConfig)
    pkg_check_modules(PC_LIBAIRSPY QUIET libairspy)
  ENDIF(NOT WIN32)

  FIND_PATH(LIBAIRSPY_INCLUDE_DIR
    NAMES airspy.h
    HINTS $ENV{LIBAIRSPY_DIR}/include ${PC_LIBAIRSPY_INCLUDEDIR}
    PATHS /usr/local/include/libairspy /usr/include/libairspy /usr/local/include
    /usr/include ${CMAKE_SOURCE_DIR}/../libairspy/src
    /opt/local/include/libairspy
    ${LIBAIRSPY_INCLUDE_DIR}
  )

  set(libairspy_library_names airspy)

  FIND_LIBRARY(LIBAIRSPY_LIBRARIES
    NAMES ${libairspy_library_names}
    HINTS $ENV{LIBAIRSPY_DIR}/lib ${PC_LIBAIRSPY_LIBDIR}
    PATHS /usr/local/lib /usr/lib /opt/local/lib ${PC_LIBAIRSPY_LIBDIR} ${PC_LIBAIRSPY_LIBRARY_DIRS} ${CMAKE_SOURCE_DIR}/../libairspy/src
  )

  if(LIBAIRSPY_INCLUDE_DIR)
    set(CMAKE_REQUIRED_INCLUDES ${LIBAIRSPY_INCLUDE_DIR})
  endif()

  if(LIBAIRSPY_LIBRARIES)
    set(CMAKE_REQUIRED_LIBRARIES ${LIBAIRSPY_LIBRARIES})
  endif()

  include(FindPackageHandleStandardArgs)
  FIND_PACKAGE_HANDLE_STANDARD_ARGS(LIBAIRSPY DEFAULT_MSG LIBAIRSPY_LIBRARIES LIBAIRSPY_INCLUDE_DIR)

  MARK_AS_ADVANCED(LIBAIRSPY_INCLUDE_DIR LIBAIRSPY_LIBRARIES)

endif (LIBAIRSPY_INCLUDE_DIR AND LIBAIRSPY_LIBRARIES)