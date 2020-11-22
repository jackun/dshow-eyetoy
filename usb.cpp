#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <libusb.h>
#include "usb.hpp"

// Haiyaa
#pragma comment(lib, "legacy_stdio_definitions.lib")
#ifdef __cplusplus
FILE iob[] = { *stdin, *stdout, *stderr };
extern "C" {
	FILE* __cdecl _iob(void) { return iob; }
}
#endif

void usleep(__int64 usec)
{
	HANDLE timer;
	LARGE_INTEGER ft;

	ft.QuadPart = -(10 * usec); // Convert to 100 nanosecond interval, negative value indicates relative time

	timer = CreateWaitableTimer(NULL, TRUE, NULL);
	SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
	CloseHandle(timer);
}

extern void Debug(const char* format, ...);

constexpr int alt_size[] = { 0, 384, 512, 768, 896 };
const int alt = 3;

//#define READREGS 0

libusb_device* find_device(libusb_context* ctx, const int vid, const int pid)
{
	// discover devices
	libusb_device** list = nullptr;
	libusb_device* found = nullptr;
	struct libusb_device_descriptor desc;

	ssize_t cnt = libusb_get_device_list(ctx, &list);
	int err = 0;

	Debug("Devices: %d\n", cnt);
	if (cnt < 0)
		return 0;

	for (ssize_t i = 0; i < cnt; i++) {
		libusb_device* device = list[i];

		int r = libusb_get_device_descriptor(device, &desc);
		if (r < 0) {
			Debug("failed to get device descriptor\n");
			continue;
		}

		if (desc.idProduct == pid && desc.idVendor == vid) {
			found = libusb_ref_device(device);
			break;
		}
	}

	libusb_free_device_list(list, 1);
	return found;
}

static void isoch_irq(struct libusb_transfer* t)
{
	//Debug("In callback: %d, %d %d.", t->actual_length, t->num_iso_packets, t->iso_packet_desc[0].actual_length);

	user_data* user = (user_data*)t->user_data;
	/*for (int i = 0; i < t->num_iso_packets; i++) {
		printf("\t isocb pkt %d: st %d len %d\n", i,
			t->iso_packet_desc[i].status,
			t->iso_packet_desc[i].actual_length);
	}*/

	// Parse results
	for (int i = 0; i < t->num_iso_packets; i++) {
		int actual_length = t->iso_packet_desc[i].actual_length;

		//Debug("iso pkt %d: st %d len %d", i, t->iso_packet_desc[i].status, actual_length);

		if (t->iso_packet_desc[i].status || actual_length == 0) {
			//error, DISCARD_PACKET;
			//Debug("iso %d status %d", i, t->iso_packet_desc[i].status);
			continue;
		}

		uint8_t* tmp = (uint8_t*)libusb_get_iso_packet_buffer(t, i);

		user->dev.transfer_cb(&user->dev, tmp, actual_length);
	}

resubmit:
	if (!user->dev.streaming) {
		libusb_free_transfer(t);
		free(user->buffer);
		return;
	}
	libusb_submit_transfer(t);
}

static std::thread event_thread;


static void handle_events(libusb_context* ctx, user_data* user)
{
	while (user->dev.streaming)
		libusb_handle_events(ctx);
}

void stop_isoch(libusb_context* ctx, libusb_device_handle* husb, user_data& user)
{
	user.dev.streaming = 0;
	if (event_thread.joinable())
		event_thread.join();

}

void start_isoch(libusb_context* ctx, libusb_device_handle* husb, int devep, user_data& user)
{
	int ret = 0;
	int len = alt_size[alt];
	int completed = 0;
	int pkts = 64;// + len/max_pkt;

	ret = libusb_claim_interface(husb, 0);
	Debug("libusb_claim_interface %s\n", libusb_error_name(ret));

	ret = libusb_set_interface_alt_setting(husb, 0, alt);
	Debug("libusb_set_interface_alt_setting %s\n", libusb_error_name(ret));

	int max_psize = libusb_get_max_iso_packet_size(libusb_get_device(husb), devep);

	uint8_t* buffer = (uint8_t*)calloc(len * pkts, sizeof(uint8_t));

	struct libusb_transfer* t = libusb_alloc_transfer(pkts);
	libusb_fill_iso_transfer(t, husb, devep | LIBUSB_ENDPOINT_IN,
		buffer, len * pkts, pkts, isoch_irq, &user, 1000);
	//t->flags |= LIBUSB_TRANSFER_FREE_TRANSFER; //auto free

	if (max_psize < 0)
		Debug("max_psize error %s\n", libusb_error_name(max_psize));

	Debug("Isoch ep: %02x, pkts %d, max pkt: %d, want %d\n", devep, pkts, max_psize, len);
	libusb_set_iso_packet_lengths(t, len);

	user.dev.streaming = 1;
	ret = libusb_submit_transfer(t);
	Debug("libusb_submit_transfer %s\n", libusb_error_name(ret));

	if (!ret) {
		event_thread = std::thread(handle_events, ctx, &user);
	}

	//libusb_free_transfer(t);
	//free(buffer);
}

