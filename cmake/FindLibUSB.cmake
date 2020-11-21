# FindLibUSB.cmake - Try to find the Hiredis library
# Once done this will define
#
#  LIBUSB_FOUND - System has libusb
#  LIBUSB_INCLUDE_DIR - The libusb include directory
#  LIBUSB_LIBRARY - The libraries needed to use libusb
#  LIBUSB_DEFINITIONS - Compiler switches required for using libusb
#
#  Original from https://github.com/texane/stlink/blob/master/cmake/modules/FindLibUSB.cmake

#message("CMAKE_VS_PLATFORM_NAME: ${CMAKE_VS_PLATFORM_NAME}")
if (CMAKE_VS_PLATFORM_NAME STREQUAL "x64")
  SET(libUSB_bitness "MS64")
else()
  SET(libUSB_bitness "MS32")
endif()

#MESSAGE("libUSB_bitness: ${libUSB_bitness}")

if(WIN32 AND NOT EXISTS ${CMAKE_BINARY_DIR}/libusb-1.0.23/libusb-1.0.def)
  file(DOWNLOAD
        https://sourceforge.net/projects/libusb/files/libusb-1.0/libusb-1.0.23/libusb-1.0.23.7z/download
        ${CMAKE_BINARY_DIR}/libusb-1.0.23.7z
        SHOW_PROGRESS
    )

  execute_process(COMMAND "C:\\Program Files\\7-Zip\\7z.exe" x -y libusb-1.0.23.7z -olibusb-1.0.23
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
endif()

FIND_PATH(LIBUSB_INCLUDE_DIR NAMES libusb.h
   HINTS
   #/usr
   #/usr/local
   #/opt
   ${CMAKE_BINARY_DIR}/libusb-1.0.23/include
   PATH_SUFFIXES libusb-1.0
   )

if (APPLE)
  set(LIBUSB_NAME libusb-1.0.a)
elseif(WIN32)
  set(LIBUSB_NAME libusb-1.0.lib)
else()
  set(LIBUSB_NAME usb-1.0)
endif()

FIND_LIBRARY(LIBUSB_LIBRARY NAMES ${LIBUSB_NAME}
  HINTS
  #/usr
  #/usr/local
  #/opt
  ${CMAKE_BINARY_DIR}/libusb-1.0.23/${libUSB_bitness}/static
)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LibUSB DEFAULT_MSG LIBUSB_LIBRARY LIBUSB_INCLUDE_DIR)

MARK_AS_ADVANCED(LIBUSB_INCLUDE_DIR LIBUSB_LIBRARY)