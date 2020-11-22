#pragma once
#include <mutex>
#include <vector>
#include <functional>

#define PAGE_ALIGN(x) (x)
struct libusb_device_handle;

enum packet_type
{
	FIRST_PACKET,
	INTER_PACKET,
	LAST_PACKET,
	DISCARD_PACKET,
};

enum format
{
	FMT_JPEG,
	FMT_YUYV,
};

struct pix_format
{
	int width, height;
	int bytesperline;
	int sizeimage;
	format format;
	int priv;
};

struct gspca_device;
typedef std::function<void(gspca_device*)> image_ready_cb;
typedef std::function<void(gspca_device*, uint8_t* data, int len)> gspca_cb;
//void (*gspca_cb)(gspca_device* device, uint8_t* data, int len);

struct gspca_device
{
	libusb_device_handle* usb_handle;
	gspca_cb transfer_cb = nullptr;
	int streaming = 1;
	packet_type last_packet_type = DISCARD_PACKET;
	pix_format pixfmt;

	int curr_mode;
	const pix_format* cam_mode;
	int frame_rate;
	int clockdiv;

	int sequence = 0;
	std::vector<uint8_t> image;
	std::mutex mutex;
	image_ready_cb ready_cb = nullptr;
	bool blink = false;
};

void ov519_stop(gspca_device& dev);
void ov519_restart(gspca_device& dev);
int ov519_init(gspca_device& gspca_dev);
void ov519_deinit(gspca_device& dev);
