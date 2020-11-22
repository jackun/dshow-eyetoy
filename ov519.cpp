#include <libusb.h>
#include <chrono>
#include "usb.hpp"
#include "ov519.hpp"
#include "util.hpp"

using namespace std::chrono_literals;

static const struct pix_format ov519_vga_mode[] = {
	{320, 240, 320, 320 * 240 * 3 / 8 + 590, FMT_JPEG, 1},
	{640, 480, 640, 640 * 480 * 3 / 8 + 590, FMT_JPEG, 0},
};

static const struct pix_format ov519_sif_mode[] = {
	{160, 120, 160, 160 * 120 * 3 / 8 + 590, FMT_JPEG, 3},
	{176, 144, 176, 176 * 144 * 3 / 8 + 590, FMT_JPEG, 1},
	{320, 240, 320, 320 * 240 * 3 / 8 + 590, FMT_JPEG, 2},
	{352, 288, 352, 352 * 288 * 3 / 8 + 590, FMT_JPEG, 0},
};

/* OV518 Camera interface register numbers */
#define R518_GPIO_OUT			0x56	/* OV518(+) only */
#define R518_GPIO_CTL			0x57	/* OV518(+) only */

/* OV519 Camera interface register numbers */
#define OV519_R10_H_SIZE		0x10
#define OV519_R11_V_SIZE		0x11
#define OV519_R12_X_OFFSETL		0x12
#define OV519_R13_X_OFFSETH		0x13
#define OV519_R14_Y_OFFSETL		0x14
#define OV519_R15_Y_OFFSETH		0x15
#define OV519_R16_DIVIDER		0x16
#define OV519_R20_DFR			0x20
#define OV519_R25_FORMAT		0x25

/* OV519 System Controller register numbers */
#define OV519_R51_RESET1		0x51
#define OV519_R54_EN_CLK1		0x54
#define OV519_R57_SNAPSHOT		0x57

#define OV519_GPIO_DATA_OUT0		0x71
#define OV519_GPIO_IO_CTRL0		0x72

/*#define OV511_ENDPOINT_ADDRESS 1	 * Isoc endpoint number */

/* I2C registers */
#define R51x_I2C_W_SID		0x41
#define R51x_I2C_SADDR_3	0x42
#define R51x_I2C_SADDR_2	0x43
#define R51x_I2C_R_SID		0x44
#define R51x_I2C_DATA		0x45
#define R518_I2C_CTL		0x47	/* OV518(+) only */
#define OVFX2_I2C_ADDR		0x00

struct ov_regvals {
	uint8_t reg;
	uint8_t val;
};
struct ov_i2c_regvals {
	uint8_t reg;
	uint8_t val;
};

/* 7640 and 7648. The defaults should be OK for most registers. */
constexpr struct ov_i2c_regvals norm_7640[] = {
	{ 0x12, 0x80 },
	{ 0x12, 0x14 },
};

extern void usleep(__int64 usec);
const uint8_t VendorDeviceOutRequest = 0x40;
const uint8_t VendorDeviceRequest = 0xc0;
int fnum = 0;


#define libusb_control_transfer_sleep usleep(150); ret = libusb_control_transfer

void reg_w(libusb_device_handle* husb, uint8_t reg, uint8_t val)
{
	int ret = 0;
	libusb_control_transfer_sleep(husb, VendorDeviceOutRequest, 0x01, 0x00, reg, &val, 1, 1000);
}

uint8_t reg_r(libusb_device_handle* husb, uint8_t reg)
{
	int ret = 0;
	uint8_t data[1];
	libusb_control_transfer_sleep(husb, VendorDeviceRequest, 0x01, 0x00, reg, data, 1, 1000);
	return data[0];
}

/*
 * Writes bits at positions specified by mask to an OV51x reg. Bits that are in
 * the same position as 1's in "mask" are cleared and set to "value". Bits
 * that are in the same position as 0's in "mask" are preserved, regardless
 * of their respective state in "value".
 */
void reg_w_mask(struct libusb_device_handle* sd,
	uint16_t index,
	uint8_t value,
	uint8_t mask)
{
	int ret;
	uint8_t oldval;

	if (mask != 0xff) {
		value &= mask;			/* Enforce mask on value */
		ret = reg_r(sd, index);
		if (ret < 0)
			return;

		oldval = ret & ~mask;		/* Clear the masked bits */
		value |= oldval;		/* Set the desired bits */
	}
	reg_w(sd, index, value);
}

uint8_t i2c_r(libusb_device_handle* husb, uint8_t reg)
{
	int ret = 0;
	uint8_t data[1];
	uint8_t tmp[] = { reg, 0x03, 0x05 };
	libusb_control_transfer_sleep(husb, VendorDeviceOutRequest, 0x01, 0x00, 0x43, &tmp[0], 1, 1000);
	libusb_control_transfer_sleep(husb, VendorDeviceOutRequest, 0x01, 0x00, 0x47, &tmp[1], 1, 1000);
	libusb_control_transfer_sleep(husb, VendorDeviceOutRequest, 0x01, 0x00, 0x47, &tmp[2], 1, 1000);
	libusb_control_transfer_sleep(husb, VendorDeviceRequest, 0x01, 0x00, 0x45, data, 1, 1000);
	return data[0];
}

uint8_t i2c_w(libusb_device_handle* husb, uint8_t reg, uint8_t val)
{
	int ret = 0;
	uint8_t tmp[] = { reg, val, 0x01 };
	libusb_control_transfer_sleep(husb, VendorDeviceOutRequest, 0x01, 0x00, 0x42, &tmp[0], 1, 1000);
	libusb_control_transfer_sleep(husb, VendorDeviceOutRequest, 0x01, 0x00, 0x45, &tmp[1], 1, 1000);
	libusb_control_transfer_sleep(husb, VendorDeviceOutRequest, 0x01, 0x00, 0x47, &tmp[2], 1, 1000);
	return 0;
}

uint8_t i2c_w_mask(libusb_device_handle* husb, uint8_t reg, uint8_t val, uint8_t mask)
{
	int ret = 0;
	uint8_t oldval = i2c_r(husb, reg);
	val &= mask;
	oldval &= ~mask;
	val |= oldval;
	i2c_w(husb, reg, val);
	return 0;
}

/*
 * add data to the current frame
 *
 * This function is called by the subdrivers at interrupt level.
 *
 * To build a frame, these ones must add
 *	- one FIRST_PACKET
 *	- 0 or many INTER_PACKETs
 *	- one LAST_PACKET
 * DISCARD_PACKET invalidates the whole frame.
 */
void gspca_frame_add(struct gspca_device* gspca_dev,
	enum packet_type packet_type,
	const uint8_t* data,
	int len)
{
	unsigned long flags;

	//Debug("add t:%d l:%d", packet_type, len);

	if (packet_type == FIRST_PACKET) {
		gspca_dev->image.resize(0);
	}
	else {
		switch (gspca_dev->last_packet_type) {
		case DISCARD_PACKET:
			if (packet_type == LAST_PACKET) {
				gspca_dev->last_packet_type = packet_type;
				gspca_dev->image.resize(0);
			}
			return;
		case LAST_PACKET:
			return;
		}
	}

	/* append the packet to the frame buffer */
	if (len > 0) {
		if (gspca_dev->image.size() + len > PAGE_ALIGN(gspca_dev->pixfmt.sizeimage)) {
			Debug("frame overflow %d > %d\n",
				gspca_dev->image.size() + len,
				PAGE_ALIGN(gspca_dev->pixfmt.sizeimage));
			packet_type = DISCARD_PACKET;
		}
		else {
			/* !! image is NULL only when last pkt is LAST or DISCARD
						if (gspca_dev.image == NULL) {
							pr_err("gspca_frame_add() image == NULL\n");
							return;
						}
			 */
			std::vector<uint8_t> tmp(data, data + len);
			gspca_dev->image.insert(gspca_dev->image.end(), tmp.begin(), tmp.end());
		}
	}
	gspca_dev->last_packet_type = packet_type;

	/* if last packet, invalidate packet concatenation until
	 * next first packet, wake up the application and advance
	 * in the queue */
	if (packet_type == LAST_PACKET) {

#ifndef NDEBUG
		static int frames = 0;
		static std::chrono::steady_clock::time_point last{};
		frames++;
		auto curr = std::chrono::high_resolution_clock::now();

		if (curr - last > 1000ms) {
			Debug("FPS: %d", frames);
			frames = 0;
			last = curr;
		}
#endif

		gspca_dev->sequence++;
		Debug("frame complete len:%d", gspca_dev->image.size());
		gspca_dev->ready_cb(gspca_dev);
		gspca_dev->image.resize(0);
	}
}

/* Temporarily stops OV511 from functioning. Must do this before changing
 * registers while the camera is streaming */
void ov519_stop(gspca_device& dev)
{
	Debug("stopping\n");

	auto sd = dev.usb_handle;
	reg_w(sd, OV519_R51_RESET1, 0x0f);
	reg_w(sd, OV519_R51_RESET1, 0x00);
	reg_w(sd, 0x22, 0x00);		/* FRAR */
}

/* Restarts OV511 after ov511_stop() is called. Has no effect if it is not
 * actually stopped (for performance). */
void ov519_restart(gspca_device& dev)
{
	Debug("restarting");
	//if (!sd->stopped)
	//	return;
	//sd->stopped = 0;
	auto sd = dev.usb_handle;
	reg_w(sd, OV519_R51_RESET1, 0x0f);
	reg_w(sd, OV519_R51_RESET1, 0x00);
	reg_w(sd, 0x22, 0x1d);		/* FRAR */
}

/* Sets up the OV519 with the given image parameters
 *
 * OV519 needs a completely different approach, until we can figure out what
 * the individual registers do.
 *
 * Do not put any sensor-specific code in here (including I2C I/O functions)
 */
static void ov519_mode_init_regs(struct gspca_device& gspca_dev)
{
	static const struct ov_regvals mode_init_519[] = {
		{ 0x5d,	0x03 }, /* Turn off suspend mode */
		{ 0x53,	0x9f }, /* was 9b in 1.65-1.08 */
		{ OV519_R54_EN_CLK1, 0x0f }, /* bit2 (jpeg enable) */
		{ 0xa2,	0x20 }, /* a2-a5 are undocumented */
		{ 0xa3,	0x18 },
		{ 0xa4,	0x04 },
		{ 0xa5,	0x28 },
		{ 0x37,	0x00 },	/* SetUsbInit */
		{ 0x55,	0x02 }, /* 4.096 Mhz audio clock */
		/* Enable both fields, YUV Input, disable defect comp (why?) */
		{ 0x22,	0x1d },
		{ 0x17,	0x50 }, /* undocumented */
		{ 0x37,	0x00 }, /* undocumented */
		{ 0x40,	0xff }, /* I2C timeout counter */
		{ 0x46,	0x00 }, /* I2C clock prescaler */
		{ 0x59,	0x04 },	/* new from windrv 090403 */
		{ 0xff,	0x00 }, /* undocumented */
		/* windows reads 0x55 at this point, why? */
	};

	auto sd = gspca_dev.usb_handle;

	/******** Set the mode ********/
	for (size_t i = 0; i < countof(mode_init_519); i++)
		reg_w(sd, mode_init_519[i].reg, mode_init_519[i].val);

	/* Select 8-bit input mode */
	reg_w_mask(sd, OV519_R20_DFR, 0x10, 0x10);

	reg_w(sd, OV519_R10_H_SIZE, gspca_dev.pixfmt.width >> 4);
	reg_w(sd, OV519_R11_V_SIZE, gspca_dev.pixfmt.height >> 3);

	// FIXME OBS decodes normally, jpgd flips UV and vice versa :confused_as_fuck:
	/*if (gspca_dev.cam_mode[gspca_dev.curr_mode].priv)
		reg_w(sd, OV519_R12_X_OFFSETL, 0x01);
	else*/
		reg_w(sd, OV519_R12_X_OFFSETL, 0x00);

	reg_w(sd, OV519_R13_X_OFFSETH, 0x00);
	reg_w(sd, OV519_R14_Y_OFFSETL, 0x00);
	reg_w(sd, OV519_R15_Y_OFFSETH, 0x00);
	reg_w(sd, OV519_R16_DIVIDER, 0x00);
	reg_w(sd, OV519_R25_FORMAT, 0x03); /* YUV422 */
//	reg_w(sd, OV519_R25_FORMAT, 0x01);
	reg_w(sd, 0x26, 0x00); /* Undocumented */

	/* FIXME: These are only valid at the max resolution. */
	gspca_dev.clockdiv = 0;
	switch (gspca_dev.frame_rate) {
	default:
		/*		case 30: */
		reg_w(sd, 0xa4, 0x0c);
		reg_w(sd, 0x23, 0xff);
		break;
	case 25:
		reg_w(sd, 0xa4, 0x0c);
		reg_w(sd, 0x23, 0x1f);
		break;
	case 20:
		reg_w(sd, 0xa4, 0x0c);
		reg_w(sd, 0x23, 0x1b);
		break;
	case 15:
		reg_w(sd, 0xa4, 0x04);
		reg_w(sd, 0x23, 0xff);
		gspca_dev.clockdiv = 1;
		break;
	case 10:
		reg_w(sd, 0xa4, 0x04);
		reg_w(sd, 0x23, 0x1f);
		gspca_dev.clockdiv = 1;
		break;
	case 5:
		reg_w(sd, 0xa4, 0x04);
		reg_w(sd, 0x23, 0x1b);
		gspca_dev.clockdiv = 1;
		break;
	}
}

static void ov519_configure(gspca_device& gspca_dev)
{
	auto sd = gspca_dev.usb_handle;
	i2c_w(sd, 0x12, 0x80); //reset
	i2c_r(sd, 0x00); //dummy sync read

	for (size_t i = 0; i < countof(norm_7640); i++)
		i2c_w(sd, norm_7640[i].reg, norm_7640[i].val);

	constexpr struct ov_regvals init_519[] = {
		{ 0x5a, 0x6d }, /* EnableSystem */
		{ 0x53, 0x9b }, /* don't enable the microcontroller */
		{ OV519_R54_EN_CLK1, 0xff }, /* set bit2 to enable jpeg */
		{ 0x5d, 0x03 },
		{ 0x49, 0x01 },
		{ 0x48, 0x00 },
		/* Set LED pin to output mode. Bit 4 must be cleared or sensor
		 * detection will fail. This deserves further investigation. */
		{ OV519_GPIO_IO_CTRL0, 0xee },
		{ OV519_R51_RESET1, 0x0f },
		{ OV519_R51_RESET1, 0x00 },
		{ 0x22, 0x00 },
		/* windows reads 0x55 at this point*/
	};

	for(size_t i = 0; i < countof(init_519); i++)
		reg_w(sd, init_519[i].reg, init_519[i].val);

	//width, height
	reg_w(sd, 0x10, 320 >> 4);
	reg_w(sd, 0x11, 240 >> 3);

	// Reset Video FIFO bit3
	reg_w(sd, 0x51, 0x0f /*0x08*/);
	reg_w(sd, 0x51, 0x00);
}

static void mode_init_ov_sensor_regs(gspca_device& gspca_dev)
{
	auto sd = gspca_dev.usb_handle;
	int qvga = gspca_dev.cam_mode[gspca_dev.curr_mode].priv & 1;

	i2c_w_mask(sd, 0x14, qvga ? 0x20 : 0x00, 0x20);
	i2c_w_mask(sd, 0x28, qvga ? 0x00 : 0x20, 0x20);
	/* Setting this undocumented bit in qvga mode removes a very
	   annoying vertical shaking of the image */
	i2c_w_mask(sd, 0x2d, qvga ? 0x40 : 0x00, 0x40);
	/* Unknown */
	i2c_w_mask(sd, 0x67, qvga ? 0xf0 : 0x90, 0xf0);
	/* Allow higher automatic gain (to allow higher framerates) */
	i2c_w_mask(sd, 0x74, qvga ? 0x20 : 0x00, 0x20);
	i2c_w_mask(sd, 0x12, 0x04, 0x04); /* AWB: 1 */

	/******** Clock programming ********/
	i2c_w(sd, 0x11, gspca_dev.clockdiv);
}

static void ov519_pkt_scan(struct gspca_device *dev,
			uint8_t *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	/* Header of ov519 is 16 bytes:
	 *     Byte     Value      Description
	 *     0        0xff       magic
	 *     1        0xff       magic
	 *     2        0xff       magic
	 *     3        0xXX       0x50 = SOF, 0x51 = EOF
	 *     9        0xXX       0x01 initial frame without data,
	 *              0x00       standard frame with image
	 *     14       Lo         in EOF: length of image data / 8
	 *     15       Hi
	 */

	if (data[0] == 0xff && data[1] == 0xff && data[2] == 0xff) {
		switch (data[3]) {
		case 0x50:		/* start of frame */
			/* Don't check the button state here, as the state
			   usually (always ?) changes at EOF and checking it
			   here leads to unnecessary snapshot state resets. */
#define HDRSZ 16
			data += HDRSZ;
			len -= HDRSZ;
#undef HDRSZ
			if (data[0] == 0xff || data[1] == 0xd8)
				gspca_frame_add(dev, FIRST_PACKET,
						data, len);
			else
				dev->last_packet_type = DISCARD_PACKET;
			return;
		case 0x51:		/* end of frame */
			//ov51x_handle_button(user, data[11] & 1);
			if (data[9] != 0)
				dev->last_packet_type = DISCARD_PACKET;
			gspca_frame_add(dev, LAST_PACKET,
					NULL, 0);
			return;
		}
	}

	/* intermediate packet */
	gspca_frame_add(dev, INTER_PACKET, data, len);
}

static std::thread blinker_thread;

static void blinker(gspca_device* dev)
{
	bool led = true;
	while (dev->blink)
	{
		reg_w(dev->usb_handle, 0x71, led && dev->streaming ? 1 : 0);
		led = !led;
		usleep(100000);
	}
	reg_w(dev->usb_handle, 0x71, 0);
}

void ov519_deinit(gspca_device& dev)
{
	dev.blink = false;
	if (blinker_thread.joinable())
		blinker_thread.join();
}

int ov519_init(struct gspca_device& gspca_dev)
{
	ov519_stop(gspca_dev);

	if (!gspca_dev.frame_rate)
		gspca_dev.frame_rate = 30;

	gspca_dev.blink = true;
	gspca_dev.cam_mode = ov519_vga_mode;
	//gspca_dev.curr_mode = 1;
	gspca_dev.pixfmt = gspca_dev.cam_mode[gspca_dev.curr_mode];
	gspca_dev.transfer_cb = ov519_pkt_scan;
	ov519_configure(gspca_dev);
	ov519_mode_init_regs(gspca_dev);
	mode_init_ov_sensor_regs(gspca_dev);
	ov519_restart(gspca_dev);

	blinker_thread = std::thread(blinker, &gspca_dev);

	return 0;
}
