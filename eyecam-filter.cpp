#include "eyecam-filter.hpp"
#include "sleepto.h"

#include <shlobj_core.h>
#include <strsafe.h>
#include <inttypes.h>
#include <libusb.h>
#include "jpgd/jpgd.h"
#include "jo_mpeg.h"
#include "util.hpp"
#include <algorithm>

using namespace DShow;

extern const uint8_t *get_placeholder(size_t&len);

#ifndef NDEBUG
static void Log(const char* format, va_list args)
{
	char str[4096];
	vsprintf_s(str, 4096, format, args);
	OutputDebugStringA(str);
	OutputDebugStringA("\n");
}

void Debug(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	Log(format, args);
	va_end(args);
}
#endif

/* ========================================================================= */

VCamFilter::VCamFilter()
	: OutputFilter(VideoFormat::MJPEG, DEFAULT_CX, DEFAULT_CY,
		       DEFAULT_INTERVAL)
{
	thread_start = CreateEvent(nullptr, true, false, nullptr);
	thread_stop = CreateEvent(nullptr, true, false, nullptr);

	AddVideoFormat(VideoFormat::MJPEG, DEFAULT_CX, DEFAULT_CY, DEFAULT_INTERVAL * 2); // 15fps
	AddVideoFormat(VideoFormat::MJPEG, 640, 480, DEFAULT_INTERVAL); // 30fps, idk, just doesn't want to
	AddVideoFormat(VideoFormat::MJPEG, 640, 480, DEFAULT_INTERVAL * 2); // 15fps

	// too slow
	//AddVideoFormat(VideoFormat::YUY2, DEFAULT_CX, DEFAULT_CY, DEFAULT_INTERVAL);
	//AddVideoFormat(VideoFormat::YUY2, DEFAULT_CX * 2 , DEFAULT_CY * 2, DEFAULT_INTERVAL);
	//AddVideoFormat(VideoFormat::YUY2, DEFAULT_CX * 2, DEFAULT_CY * 2, DEFAULT_INTERVAL * 2);

	AddVideoFormat(VideoFormat::XRGB, DEFAULT_CX, DEFAULT_CY, DEFAULT_INTERVAL);
	AddVideoFormat(VideoFormat::XRGB, DEFAULT_CX, DEFAULT_CY, DEFAULT_INTERVAL * 2);
	AddVideoFormat(VideoFormat::XRGB, 640, 480, DEFAULT_INTERVAL * 2);

	/* ---------------------------------------- */

	th = std::thread([this] { Thread(); });

	OutputFilter::AddRef();
}

VCamFilter::~VCamFilter()
{
	SetEvent(thread_stop);
	th.join();

	if (usb_handle)
	{
		std::lock_guard<std::mutex> lk(hotplug_mutex);
		ov519_stop(user_data.dev);
		ov519_deinit(user_data.dev);
		stop_isoch(usb_ctx, usb_handle, user_data);
		libusb_close(usb_handle);
		libusb_exit(usb_ctx);
		usb_handle = nullptr;
		usb_ctx = nullptr;
	}
	if (waiter.joinable())
		waiter.join();
}

const wchar_t *VCamFilter::FilterName() const
{
	return L"EyeCamFilter";
}

STDMETHODIMP VCamFilter::Pause()
{
	HRESULT hr;

	hr = OutputFilter::Pause();
	if (FAILED(hr)) {
		return hr;
	}

	SetEvent(thread_start);
	return S_OK;
}

inline uint64_t VCamFilter::GetTime()
{
	if (!!clock) {
		REFERENCE_TIME rt;
		HRESULT hr = clock->GetTime(&rt);
		if (SUCCEEDED(hr)) {
			return (uint64_t)rt;
		}
	}

	return gettime_100ns();
}

void VCamFilter::ImageReady(gspca_device* dev)
{
	std::lock_guard<std::mutex> lk(dev->mutex);
	last_image = dev->image;
}

// Poor man's hotplugging
void VCamFilter::WaitForDevice()
{
reopen:
	//TODO multiple cams
	//libusb_device* dev = find_device(usb_ctx, 0x054C, 0x0155);

	libusb_device_handle* tmp = usb_handle; // if reopening don't close handle just yet so other things won't crash
	while (!stopped()) {
		{
			std::lock_guard<std::mutex> lk(hotplug_mutex);
			usb_handle = libusb_open_device_with_vid_pid(usb_ctx, 0x054C, 0x0155);
			if (usb_handle) {
				if (tmp) {
					libusb_close(tmp);
				}
				user_data.dev.usb_handle = usb_handle;
				break;
			}
		}

		Sleep(1000);
	}

	if (stopped())
		return;

	libusb_device* device = nullptr;
	libusb_device_handle* handle = nullptr;

	{
		std::lock_guard<std::mutex> lk(hotplug_mutex);

		libusb_reset_device(usb_handle);
		ov519_init(user_data.dev);
		start_isoch(usb_ctx, usb_handle, 0x01 | LIBUSB_ENDPOINT_IN, user_data);
	}

	while (!stopped() && device) {
		// lock it
		{
			std::lock_guard<std::mutex> lk(hotplug_mutex);
			if (!usb_handle)
				break;
			int ret = libusb_open(device, &handle);
			if (ret) {
				stop_isoch(usb_ctx, usb_handle, user_data);
				ov519_deinit(user_data.dev);
				Debug("reopening device %d", ret);
				goto reopen;
			}
			libusb_close(handle);
		}
		Sleep(1000);
	}
}

void VCamFilter::Thread()
{
	HANDLE h[2] = {thread_start, thread_stop};
	DWORD ret = WaitForMultipleObjects(2, h, false, INFINITE);
	if (ret != WAIT_OBJECT_0)
		return;

	uint64_t cur_time = gettime_100ns();
	uint64_t filter_time = GetTime();

	cx = GetCX();
	cy = GetCY();
	interval = GetInterval();
	user_data.dev.ready_cb = std::bind(&VCamFilter::ImageReady, this, std::placeholders::_1);

	int result = 0;
	user_data.dev.curr_mode = cx == 640 ? 1 : 0;
	user_data.dev.frame_rate = int(10000000 / interval);
	if ((result = libusb_init(&usb_ctx)) != 0)
	{
		Debug("libusb error: %d", result);
	}
	else
	{
		//libusb_set_debug(usb_ctx, libusb_log_level::LIBUSB_LOG_LEVEL_DEBUG);
		waiter = std::thread([this] { WaitForDevice(); });
	}

	while (!stopped()) {
		Frame(filter_time);
		sleepto_100ns(cur_time += interval);
		filter_time += interval;
	}

	if (user_data.dev.usb_handle)
		ov519_deinit(user_data.dev);
}

void YUVfromRGB(double& Y, double& U, double& V, const double R, const double G, const double B)
{
	Y = 0.257 * R + 0.504 * G + 0.098 * B + 16;
	U = -0.148 * R - 0.291 * G + 0.439 * B + 128;
	V = 0.439 * R - 0.368 * G - 0.071 * B + 128;
}

void VCamFilter::Frame(uint64_t ts)
{
	uint8_t *ptr;
	std::vector<uint8_t> image;
	{
		std::lock_guard<std::mutex> lk(user_data.dev.mutex);
		image = last_image;
	}

	//Debug("Current mode: %d, %dx%d", GetVideoFormat(), GetCX(), GetCY());

	if (LockSampleData(&ptr)) {
		//Debug("Video format %d, last size %zu", GetVideoFormat(), last_image.size());
		if (image.size() && GetVideoFormat() == VideoFormat::MJPEG) {
			memcpy(ptr, image.data(), image.size());
		}
		else if (image.size() && GetVideoFormat() == VideoFormat::YUY2)
		{
			int width, height, actual_comps;
			unsigned char* rgbData = jpgd::decompress_jpeg_image_from_memory(image.data(), image.size(), &width, &height, &actual_comps, 4);
			uint8_t* yuy2 = ptr;
			size_t min_width = min(width, cx);
			if (rgbData) {
				for (int y = 0; y < height; y++)
				{
					uint8_t* rgb = rgbData + y * width * 4;
					for (int x = 0; x < min_width; x+=2)
					{
						double y0, u0, v0, y1, u1, v1;
						YUVfromRGB(y0, u0, v0, rgb[0], rgb[1], rgb[2]);
						YUVfromRGB(y1, u1, v1, rgb[4], rgb[5], rgb[6]);
						yuy2[0] = (uint8_t)std::clamp(y0, 0., 255.);
						yuy2[1] = (uint8_t)std::clamp((u0 + u1) / 2, 0., 255.);
						yuy2[2] = (uint8_t)std::clamp(y1, 0., 255.);
						yuy2[3] = (uint8_t)std::clamp((v0 + v1) / 2, 0., 255.);
						rgb += 8;
						yuy2 += 4;
					}
				}
			}
			free(rgbData);
			//Debug("yuy2 frame");
		}
		else if (image.size() && GetVideoFormat() == VideoFormat::XRGB)
		{
			int width, height, actual_comps;
			unsigned char* rgbData = jpgd::decompress_jpeg_image_from_memory(image.data(), image.size(), &width, &height, &actual_comps, 4);

			size_t min_width = min(width, cx);
			if (rgbData) {
				for (int y = 0; y < height; y++)
					memcpy(&ptr[y * cx * 4], &rgbData [(height - y - 1) * width * 4], min_width * 4);
					//memcpy(ptr + y * cx * 4, rgbData + y * width * 4, min_width * 4);
			}
			free(rgbData);
		}
		else
			ShowDefaultFrame(ptr);

		UnlockSampleData(ts, ts + interval);
	}
}

void VCamFilter::ShowDefaultFrame(uint8_t *ptr)
{
	if (GetVideoFormat() == VideoFormat::MJPEG) {
		size_t len = 0;
		auto placeholder = get_placeholder(len);
		if (placeholder)
			memcpy(ptr, placeholder, len);
	}
	else if (GetVideoFormat() == VideoFormat::XRGB) {
		static unsigned int y_offset = 0;
		for (int y = 0; y < GetCY(); y++) {
			cy = (y + y_offset) % GetCY();
			int c = (255 * y) / GetCY();

			for (int x = 0; x < GetCX(); x++) {
				int* dst = (int*)&ptr[(cy * GetCX() + x) * 4];
				*dst = (255 << 24) | (255 - c) << 16 | (c << 8) | (255 - c);
			}
		}
		y_offset += 6;
	}
	else if (GetVideoFormat() == VideoFormat::YUY2) {
		memset(ptr, 127, cx * cy * 2);
	}
}
