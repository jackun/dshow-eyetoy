#pragma once

#include <windows.h>
#include <cstdint>
#include <thread>
#include <vector>
#include <mutex>
#include "dshow/output-filter.hpp"
#include "usb.hpp"

#define DEFAULT_CX 320
#define DEFAULT_CY 240
#define DEFAULT_INTERVAL 333333ULL

class VCamFilter : public DShow::OutputFilter {
	std::thread th, waiter;

	const uint8_t* placeholder;
	uint32_t cx = DEFAULT_CX;
	uint32_t cy = DEFAULT_CY;
	uint64_t interval = DEFAULT_INTERVAL;
	HANDLE thread_start;
	HANDLE thread_stop;

	std::vector<uint8_t> last_image;
	user_data user_data{};
	libusb_context* usb_ctx = nullptr;
	libusb_device_handle* usb_handle = nullptr;
	std::mutex hotplug_mutex;

	inline bool stopped() const
	{
		return WaitForSingleObject(thread_stop, 0) != WAIT_TIMEOUT;
	}

	inline uint64_t GetTime();

	void Thread();
	void Frame(uint64_t ts);
	void ShowDefaultFrame(uint8_t* ptr);
	void ImageReady(gspca_device* user);
	void WaitForDevice();

protected:
	const wchar_t* FilterName() const override;

public:
	VCamFilter();
	~VCamFilter() override;

	STDMETHODIMP Pause() override;
};
