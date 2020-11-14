#include "eyecam-filter.hpp"
#include "sleepto.h"

#include <shlobj_core.h>
#include <strsafe.h>
#include <inttypes.h>

using namespace DShow;

extern const uint8_t *get_placeholder();

/* ========================================================================= */

VCamFilter::VCamFilter()
	: OutputFilter(VideoFormat::XRGB, DEFAULT_CX, DEFAULT_CY,
		       DEFAULT_INTERVAL)
{
	thread_start = CreateEvent(nullptr, true, false, nullptr);
	thread_stop = CreateEvent(nullptr, true, false, nullptr);

	//AddVideoFormat(VideoFormat::I420, DEFAULT_CX, DEFAULT_CY,
	//	       DEFAULT_INTERVAL);
	//AddVideoFormat(VideoFormat::YUY2, DEFAULT_CX, DEFAULT_CY,
	//	       DEFAULT_INTERVAL);
	AddVideoFormat(VideoFormat::XRGB, DEFAULT_CX, DEFAULT_CY,
		DEFAULT_INTERVAL);

	/* ---------------------------------------- */

	th = std::thread([this] { Thread(); });

	AddRef();
}

VCamFilter::~VCamFilter()
{
	SetEvent(thread_stop);
	th.join();
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

	//nv12_scale_init(&scaler, TARGET_FORMAT_NV12, GetCX(), GetCY(), cx, cy);

	while (!stopped()) {
		Frame(filter_time);
		sleepto_100ns(cur_time += interval);
		filter_time += interval;
	}
}

void VCamFilter::Frame(uint64_t ts)
{
	uint32_t new_cx = cx;
	uint32_t new_cy = cy;
	uint64_t new_interval = interval;


	//enum queue_state state = video_queue_state(vq);
	//if (state != prev_state) {
	//	if (state == SHARED_QUEUE_STATE_READY) {
	//		video_queue_get_info(vq, &new_cx, &new_cy,
	//				     &new_interval);
	//	} else if (state == SHARED_QUEUE_STATE_STOPPING) {
	//		video_queue_close(vq);
	//		vq = nullptr;
	//	}

	//	prev_state = state;
	//}

	//if (state != SHARED_QUEUE_STATE_READY) {
	//	new_cx = DEFAULT_CX;
	//	new_cy = DEFAULT_CY;
	//	new_interval = DEFAULT_INTERVAL;
	//}

	if (new_cx != cx || new_cy != cy || new_interval != interval) {
		//nv12_scale_init(&scaler, TARGET_FORMAT_NV12, GetCX(), GetCY(),
		//		new_cx, new_cy);

		cx = new_cx;
		cy = new_cy;
		interval = new_interval;
	}

	//if (GetVideoFormat() == VideoFormat::I420)
	//	scaler.format = TARGET_FORMAT_I420;
	//else if (GetVideoFormat() == VideoFormat::YUY2)
	//	scaler.format = TARGET_FORMAT_YUY2;
	//else
	//	scaler.format = TARGET_FORMAT_NV12;

	uint8_t *ptr;
	if (LockSampleData(&ptr)) {
		//if (state == SHARED_QUEUE_STATE_READY)
		//	ShowOBSFrame(ptr);
		//else
			ShowDefaultFrame(ptr);

		UnlockSampleData(ts, ts + interval);
	}
}

void VCamFilter::ShowDefaultFrame(uint8_t *ptr)
{
	//if (placeholder) {
	//	nv12_do_scale(&scaler, ptr, placeholder);
	//} else {
		//memset(ptr, 127, GetCX() * GetCY() * 3 / 2);
	static unsigned int y_offset = 0;
	for (int y = 0; y < GetCY(); y++) {
		cy = (y + y_offset) % GetCY();
		for (int x = 0; x < GetCX(); x++) {
			int c = (255 * y) / GetCY();
			int* dst = (int*)&ptr[(cy * GetCX() + x) * 4];
			*dst = (255 << 24) | (255 - c) << 16 | (c << 8) | (255 - c);
		}
	}
	y_offset+=6;
	//}
}
