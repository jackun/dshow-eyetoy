cmake_minimum_required(VERSION 3.5)
project(eyecam)

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_SOURCE_DIR}/cmake")
find_package(LibUSB REQUIRED)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_output_suffix "64")
else()
	set(_output_suffix "32")
endif()

SET(EYECAM_GUID "9F2D3DE6-1F0E-410C-85C1-D15B33F4B7AA")

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/eyecam-module.def.in"
	"${CMAKE_CURRENT_BINARY_DIR}/eyecam-module.def")

set(MODULE_DESCRIPTION "EyeCam")
add_definitions(-DUNICODE)
add_definitions(-DENABLE_LOGGING)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(dshow_SOURCES
	./dshow/log.cpp
	./dshow/dshow-base.cpp
	./dshow/dshow-enum.cpp
	./dshow/dshow-formats.cpp
	./dshow/dshow-media-type.cpp
	./dshow/output-filter.cpp
	)

set(dshow_HEADERS
	./dshow/ComPtr.hpp
	./dshow/CoTaskMemPtr.hpp
	./dshow/log.hpp
	./dshow/dshow-base.hpp
	./dshow/dshow-enum.hpp
	./dshow/dshow-formats.hpp
	./dshow/dshow-media-type.hpp
	./dshow/output-filter.hpp
	./dshow/dshowcapture.hpp
	)

set(eyecam_SOURCES
	"${CMAKE_CURRENT_BINARY_DIR}/eyecam-module.def"
	sleepto.c
	placeholder.cpp
	eyecam-module.cpp
	eyecam-filter.cpp
	usb.cpp
	ov519.cpp
	
	jpgd/jpgd.cpp
	jo_mpeg.cpp
	#virtualcam-module.rc
	)

set(eyecam_HEADERS
	util.hpp
	sleepto.h
	eyecam-filter.hpp
	usb.hpp
	ov519.hpp

	jpgd/jpgd.h
	jpgd/jpgd_idct.h
	jo_mpeg.h
	)

string(REPLACE "-" ";" GUID_VALS ${EYECAM_GUID})
set(INVALID_GUID TRUE)
list(LENGTH GUID_VALS GUID_VAL_COUNT)
if(GUID_VAL_COUNT EQUAL 5)
	string(REPLACE ";" "0" GUID_HEX ${GUID_VALS})
	string(REGEX MATCH "[0-9a-fA-F]+" GUID_ACTUAL_HEX ${GUID_HEX})
	if(GUID_ACTUAL_HEX STREQUAL GUID_HEX)
		list(GET GUID_VALS 0 GUID_VALS_DATA1)
		list(GET GUID_VALS 1 GUID_VALS_DATA2)
		list(GET GUID_VALS 2 GUID_VALS_DATA3)
		list(GET GUID_VALS 3 GUID_VALS_DATA4)
		list(GET GUID_VALS 4 GUID_VALS_DATA5)
		string(LENGTH ${GUID_VALS_DATA1} GUID_VALS_DATA1_LENGTH)
		string(LENGTH ${GUID_VALS_DATA2} GUID_VALS_DATA2_LENGTH)
		string(LENGTH ${GUID_VALS_DATA3} GUID_VALS_DATA3_LENGTH)
		string(LENGTH ${GUID_VALS_DATA4} GUID_VALS_DATA4_LENGTH)
		string(LENGTH ${GUID_VALS_DATA5} GUID_VALS_DATA5_LENGTH)
		if(GUID_VALS_DATA1_LENGTH EQUAL 8 AND
		   GUID_VALS_DATA2_LENGTH EQUAL 4 AND
		   GUID_VALS_DATA3_LENGTH EQUAL 4 AND
		   GUID_VALS_DATA4_LENGTH EQUAL 4 AND
		   GUID_VALS_DATA5_LENGTH EQUAL 12)
			set(GUID_VAL01 ${GUID_VALS_DATA1})
			set(GUID_VAL02 ${GUID_VALS_DATA2})
			set(GUID_VAL03 ${GUID_VALS_DATA3})
			string(SUBSTRING ${GUID_VALS_DATA4} 0 2 GUID_VAL04)
			string(SUBSTRING ${GUID_VALS_DATA4} 2 2 GUID_VAL05)
			string(SUBSTRING ${GUID_VALS_DATA5} 0 2 GUID_VAL06)
			string(SUBSTRING ${GUID_VALS_DATA5} 2 2 GUID_VAL07)
			string(SUBSTRING ${GUID_VALS_DATA5} 4 2 GUID_VAL08)
			string(SUBSTRING ${GUID_VALS_DATA5} 6 2 GUID_VAL09)
			string(SUBSTRING ${GUID_VALS_DATA5} 8 2 GUID_VAL10)
			string(SUBSTRING ${GUID_VALS_DATA5} 10 2 GUID_VAL11)
			set(INVALID_GUID FALSE)
		endif()
	endif()
endif()

if(INVALID_GUID)
	message(WARNING "Invalid EyeCam GUID!")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/eyecam-guid.h.in ${CMAKE_CURRENT_BINARY_DIR}/eyecam-guid.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${LIBUSB_INCLUDE_DIR})

if(MSVC)
	add_compile_options("$<IF:$<CONFIG:Debug>,/MDd,/MD>")
endif()

#set(CMAKE_MODULE_LINKER_FLAGS "${MAKE_MODULE_LINKER_FLAGS} /ignore:4104")

add_library(eyecam MODULE
	${dshow_SOURCES}
	${dshow_HEADERS}
	${eyecam_SOURCES}
	${eyecam_HEADERS}
)

target_link_libraries(eyecam
	winmm
	strmiids
	#gdiplus
	${LIBUSB_LIBRARY}
)

message("LIBUSB_LIBRARY: ${LIBUSB_LIBRARY}")

set_target_properties(eyecam
	PROPERTIES
		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist"
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist"
		OUTPUT_NAME "eyecam${_output_suffix}")

SET(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/dist")
INSTALL(TARGETS eyecam
			LIBRARY DESTINATION .
			CONFIGURATIONS Release RelWithDebInfo MinSizeRel Debug) #All

configure_file(eyecam-install.bat.in "${CMAKE_CURRENT_BINARY_DIR}/dist/eyecam-install.bat")
configure_file(eyecam-uninstall.bat.in "${CMAKE_CURRENT_BINARY_DIR}/dist/eyecam-uninstall.bat")
