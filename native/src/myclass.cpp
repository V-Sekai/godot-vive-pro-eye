/*
 * This file is part of godot-vive-pro-eye, a Godot 3 driver for the HTC
 * Vive Pro Eye eye tracking hardware.
 *
 * Copyright (c) 2019 Lehrstuhl für Informatik 2,
 * Friedrich-Alexander-Universität Erlangen-Nürnberg (FAU)
 * Author: Florian Jung (florian.jung@fau.de)
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <iostream>
#include <string>

#include "myclass.h"

#include "SRanipal.h"
#include "SRanipal_Enums.h"
#include "SRanipal_Eye.h"
#include "SRanipal_Lip.h"


#include <boost/lockfree/spsc_queue.hpp>
#include <chrono>
#include <thread>

using namespace godot;
using namespace std;
using namespace ViveSR;

void MyClass::_register_methods() {
#define reg(name) register_method(#name, &MyClass::name)
	reg(update_eye_data);
	reg(next_eye_data);
	reg(get_eyeball_position);
	reg(get_gaze_direction);
	reg(is_user_present);
	reg(get_timestamp);
	reg(get_gaze_distance);
	reg(get_pupil_size);
	reg(get_eye_openness);
	reg(_ready);
}

void MyClass::_init() {
	lip_data_v2.image = lip_image;
}

void MyClass::_ready() {
	cout << "MyClass::_ready()" << endl;
	ViveSR::Error err = static_cast<ViveSR::Error>(ViveSR::anipal::Initial(ViveSR::anipal::Eye::ANIPAL_TYPE_EYE, NULL));
	switch (err) {
		case ViveSR::Error::WORK:
			cout << "Successfully initialized SRanipal." << endl;
			poll_eyes_thread = std::thread(&MyClass::poll_eyes, this);
			break;
		case ViveSR::Error::RUNTIME_NOT_FOUND:
			cout << "Failed to initialize SRanipal: Runtime not found." << endl;
			break;
		default:
			cout << "Failed to initialize SRanipal. Unknown error code " << err << endl;
	}
	int32_t error = ViveSR::anipal::Initial(ViveSR::anipal::Lip::ANIPAL_TYPE_LIP_V2, NULL);
	if (error == ViveSR::Error::WORK) {
		printf("Successfully initialize version2 Lip engine.\n");
		poll_lips_thread = std::thread(&MyClass::poll_lips, this);
	} else {
		printf("Fail to initialize version2 Lip engine. please refer the code %d %s.\n", error, CovertErrorCode(error).c_str());
	}
}

MyClass::MyClass() {
	cout << "MyClass ctor" << endl;
}

MyClass::~MyClass() {
	ViveSR::anipal::Release(ViveSR::anipal::Lip::ANIPAL_TYPE_LIP_V2);
	cout << "MyClass dtor" << endl;
}

void MyClass::poll_eyes() {
	ViveSR::anipal::Eye::EyeData poll_eye_data;

	while (true) {
		std::this_thread::sleep_for(1ms);

		int prev_frame = poll_eye_data.frame_sequence;
		int result = ViveSR::anipal::Eye::GetEyeData(&poll_eye_data);
		if (result == ViveSR::Error::WORK) {
			int delta = poll_eye_data.frame_sequence - prev_frame;

			if (delta > 0) {
				if (delta > 1) {
					cout << "frame delta: " << poll_eye_data.frame_sequence - prev_frame << endl;
				}
				bool success = queue.push(poll_eye_data);
				if (!success) {
					cout << "ringbuf overflow" << endl;
				}
			}
		}
	}
}

void godot::MyClass::poll_lips() {
	ViveSR::anipal::Eye::EyeData poll_eye_data;

	while (true) {
		std::this_thread::sleep_for(1ms);
		int32_t result = ViveSR::anipal::Lip::GetLipData_v2(&lip_data_v2);
		if (result == ViveSR::Error::WORK) {
			float *weightings = lip_data_v2.prediction_data.blend_shape_weight;
			cout << "Lip Ver2:" << endl;
			for (int i = 0; i < ViveSR::anipal::Lip::blend_shape_nums; i++) {
				cout << weightings[i] << endl;
			}
		}
	}
}

bool MyClass::next_eye_data() {
	bool success = queue.pop(eye_data);
	if (success)
		data_valid = true;
	return success;
}

bool MyClass::update_eye_data() {
	bool success = false;
	while (next_eye_data())
		success = true;
	return success;
}

const ViveSR::anipal::Eye::SingleEyeData *MyClass::get_eye(int eye) {
	ViveSR::anipal::Eye::SingleEyeData *e;
	switch (eye) {
		case -1: e = &eye_data.verbose_data.left; break;
		case 1: e = &eye_data.verbose_data.right; break;
		default: e = &eye_data.verbose_data.combined.eye_data; break;
	}
	return e;
}

static godot::Vector3 convert_vec(const ViveSR::anipal::Eye::Vector3 &v) {
	return godot::Vector3(-v.x, v.y, -v.z);
}

Vector3 MyClass::get_eyeball_position(int eye) {
	return convert_vec(get_eye(eye)->gaze_origin_mm) / 1000.0;
}

Vector3 MyClass::get_gaze_direction(int eye) {
	return convert_vec(get_eye(eye)->gaze_direction_normalized);
}

bool MyClass::is_user_present() {
	return eye_data.no_user;
}

double MyClass::get_timestamp() {
	return eye_data.timestamp;
}

double MyClass::get_gaze_distance() {
	if (eye_data.verbose_data.combined.convergence_distance_validity)
		return eye_data.verbose_data.combined.convergence_distance_mm / 1000.;
	else
		return -1.;
}

double MyClass::get_pupil_size(int eye) {
	return get_eye(eye)->pupil_diameter_mm;
}

double MyClass::get_eye_openness(int eye) {
	return get_eye(eye)->eye_openness;
}
std::string godot::MyClass::CovertErrorCode(int error) {

	std::string result = "";
	switch (error) {
		case (ViveSR::Error::RUNTIME_NOT_FOUND):
			result = "RUNTIME_NOT_FOUND";
			break;
		case (ViveSR::Error::NOT_INITIAL):
			result = "NOT_INITIAL";
			break;
		case (ViveSR::Error::FAILED):
			result = "FAILED";
			break;
		case (ViveSR::Error::WORK):
			result = "WORK";
			break;
		case (ViveSR::Error::INVALID_INPUT):
			result = "INVALID_INPUT";
			break;
		case (ViveSR::Error::FILE_NOT_FOUND):
			result = "FILE_NOT_FOUND";
			break;
		case (ViveSR::Error::DATA_NOT_FOUND):
			result = "DATA_NOT_FOUND";
			break;
		case (ViveSR::Error::UNDEFINED):
			result = "UNDEFINED";
			break;
		case (ViveSR::Error::INITIAL_FAILED):
			result = "INITIAL_FAILED";
			break;
		case (ViveSR::Error::NOT_IMPLEMENTED):
			result = "NOT_IMPLEMENTED";
			break;
		case (ViveSR::Error::NULL_POINTER):
			result = "NULL_POINTER";
			break;
		case (ViveSR::Error::OVER_MAX_LENGTH):
			result = "OVER_MAX_LENGTH";
			break;
		case (ViveSR::Error::FILE_INVALID):
			result = "FILE_INVALID";
			break;
		case (ViveSR::Error::UNINSTALL_STEAM):
			result = "UNINSTALL_STEAM";
			break;
		case (ViveSR::Error::MEMCPY_FAIL):
			result = "MEMCPY_FAIL";
			break;
		case (ViveSR::Error::NOT_MATCH):
			result = "NOT_MATCH";
			break;
		case (ViveSR::Error::NODE_NOT_EXIST):
			result = "NODE_NOT_EXIST";
			break;
		case (ViveSR::Error::UNKONW_MODULE):
			result = "UNKONW_MODULE";
			break;
		case (ViveSR::Error::MODULE_FULL):
			result = "MODULE_FULL";
			break;
		case (ViveSR::Error::UNKNOW_TYPE):
			result = "UNKNOW_TYPE";
			break;
		case (ViveSR::Error::INVALID_MODULE):
			result = "INVALID_MODULE";
			break;
		case (ViveSR::Error::INVALID_TYPE):
			result = "INVALID_TYPE";
			break;
		case (ViveSR::Error::MEMORY_NOT_ENOUGH):
			result = "MEMORY_NOT_ENOUGH";
			break;
		case (ViveSR::Error::BUSY):
			result = "BUSY";
			break;
		case (ViveSR::Error::NOT_SUPPORTED):
			result = "NOT_SUPPORTED";
			break;
		case (ViveSR::Error::INVALID_VALUE):
			result = "INVALID_VALUE";
			break;
		case (ViveSR::Error::COMING_SOON):
			result = "COMING_SOON";
			break;
		case (ViveSR::Error::INVALID_CHANGE):
			result = "INVALID_CHANGE";
			break;
		case (ViveSR::Error::TIMEOUT):
			result = "TIMEOUT";
			break;
		case (ViveSR::Error::DEVICE_NOT_FOUND):
			result = "DEVICE_NOT_FOUND";
			break;
		case (ViveSR::Error::INVALID_DEVICE):
			result = "INVALID_DEVICE";
			break;
		case (ViveSR::Error::NOT_AUTHORIZED):
			result = "NOT_AUTHORIZED";
			break;
		case (ViveSR::Error::ALREADY):
			result = "ALREADY";
			break;
		case (ViveSR::Error::INTERNAL):
			result = "INTERNAL";
			break;
		case (ViveSR::Error::CONNECTION_FAILED):
			result = "CONNECTION_FAILED";
			break;
		case (ViveSR::Error::ALLOCATION_FAILED):
			result = "ALLOCATION_FAILED";
			break;
		case (ViveSR::Error::OPERATION_FAILED):
			result = "OPERATION_FAILED";
			break;
		case (ViveSR::Error::NOT_AVAILABLE):
			result = "NOT_AVAILABLE";
			break;
		case (ViveSR::Error::CALLBACK_IN_PROGRESS):
			result = "CALLBACK_IN_PROGRESS";
			break;
		case (ViveSR::Error::SERVICE_NOT_FOUND):
			result = "SERVICE_NOT_FOUND";
			break;
		case (ViveSR::Error::DISABLED_BY_USER):
			result = "DISABLED_BY_USER";
			break;
		case (ViveSR::Error::EULA_NOT_ACCEPT):
			result = "EULA_NOT_ACCEPT";
			break;
		case (ViveSR::Error::RUNTIME_NO_RESPONSE):
			result = "RUNTIME_NO_RESPONSE";
			break;
		case (ViveSR::Error::OPENCL_NOT_SUPPORT):
			result = "OPENCL_NOT_SUPPORT";
			break;
		case (ViveSR::Error::NOT_SUPPORT_EYE_TRACKING):
			result = "NOT_SUPPORT_EYE_TRACKING";
			break;
		case (ViveSR::Error::LIP_NOT_SUPPORT):
			result = "LIP_NOT_SUPPORT";
			break;
		default:
			result = "No such error code";
	}
	return result;
}
