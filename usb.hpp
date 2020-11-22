#pragma once
#include <cstdint>
#include "ov519.hpp"

struct user_data
{
	void* buffer = nullptr;
	gspca_device dev;
};

struct libusb_device_handle;
struct libusb_context;
struct libusb_device;

libusb_device* find_device(libusb_context* ctx, const int vid, const int pid);
void start_isoch(libusb_context* ctx, libusb_device_handle* husb, int devep, user_data& user);
void stop_isoch(libusb_context* ctx, libusb_device_handle* husb, user_data& user);

uint8_t i2c_w_mask(libusb_device_handle* husb, uint8_t reg, uint8_t val, uint8_t mask);
uint8_t i2c_w(libusb_device_handle* husb, uint8_t reg, uint8_t val);
uint8_t i2c_r(libusb_device_handle* husb, uint8_t reg);
uint8_t reg_r(libusb_device_handle* husb, uint8_t reg);
void reg_w(libusb_device_handle* husb, uint8_t reg, uint8_t val);
void reg_w_mask(struct libusb_device_handle* sd, uint16_t index, uint8_t value, uint8_t mask);
