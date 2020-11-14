#pragma once

#include <windows.h>
#include <cstdint>
#include <thread>
#include "dshow/output-filter.hpp"

#define DEFAULT_CX 320
#define DEFAULT_CY 240
#define DEFAULT_INTERVAL 333333ULL

class VCamFilter : public DShow::OutputFilter {
	std::thread th;

	const uint8_t *placeholder;
	uint32_t cx = DEFAULT_CX;
	uint32_t cy = DEFAULT_CY;
	uint64_t interval = DEFAULT_INTERVAL;
	HANDLE thread_start;
	HANDLE thread_stop;

	inline bool stopped() const
	{
		return WaitForSingleObject(thread_stop, 0) != WAIT_TIMEOUT;
	}

	inline uint64_t GetTime();

	void Thread();
	void Frame(uint64_t ts);
	void ShowDefaultFrame(uint8_t *ptr);

protected:
	const wchar_t *FilterName() const override;

public:
	VCamFilter();
	~VCamFilter() override;

	STDMETHODIMP Pause() override;
};
