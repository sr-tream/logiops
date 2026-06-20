/*
 * Copyright 2019-2023 PixlOne
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <TouchpadDevice.h>
#include <algorithm>
#include <system_error>

using namespace logid;

namespace {
    input_absinfo abs_info(int minimum, int maximum, int resolution = 0) {
        input_absinfo info{};
        info.minimum = minimum;
        info.maximum = maximum;
        info.resolution = resolution;
        return info;
    }
}

TouchpadDevice::TouchpadDevice(const char* name) {
    device = libevdev_new();
    libevdev_set_name(device, name);
    libevdev_set_id_bustype(device, BUS_VIRTUAL);
    libevdev_set_id_vendor(device, 0x1);
    libevdev_set_id_product(device, 0x1);

    libevdev_enable_property(device, INPUT_PROP_POINTER);
#ifdef INPUT_PROP_BUTTONPAD
    libevdev_enable_property(device, INPUT_PROP_BUTTONPAD);
#endif

    libevdev_enable_event_type(device, EV_KEY);
    libevdev_enable_event_code(device, EV_KEY, BTN_LEFT, nullptr);
    libevdev_enable_event_code(device, EV_KEY, BTN_TOUCH, nullptr);
    libevdev_enable_event_code(device, EV_KEY, BTN_TOOL_FINGER, nullptr);
    libevdev_enable_event_code(device, EV_KEY, BTN_TOOL_DOUBLETAP, nullptr);
    libevdev_enable_event_code(device, EV_KEY, BTN_TOOL_TRIPLETAP, nullptr);
    libevdev_enable_event_code(device, EV_KEY, BTN_TOOL_QUADTAP, nullptr);

    libevdev_enable_event_type(device, EV_ABS);
    auto slot = abs_info(0, _max_contacts - 1);
    auto tracking_id = abs_info(0, 65535);
    auto position_x = abs_info(0, _touchpad_width, _touchpad_resolution);
    auto position_y = abs_info(0, _touchpad_height, _touchpad_resolution);
    libevdev_enable_event_code(device, EV_ABS, ABS_MT_SLOT, &slot);
    libevdev_enable_event_code(device, EV_ABS, ABS_MT_TRACKING_ID, &tracking_id);
    libevdev_enable_event_code(device, EV_ABS, ABS_MT_POSITION_X, &position_x);
    libevdev_enable_event_code(device, EV_ABS, ABS_MT_POSITION_Y, &position_y);
    libevdev_enable_event_code(device, EV_ABS, ABS_X, &position_x);
    libevdev_enable_event_code(device, EV_ABS, ABS_Y, &position_y);

    int err = libevdev_uinput_create_from_device(device, LIBEVDEV_UINPUT_OPEN_MANAGED, &ui_device);
    if (err != 0) {
        libevdev_free(device);
        throw std::system_error(-err, std::generic_category());
    }
}

TouchpadDevice::~TouchpadDevice() {
    if (_active)
        endGesture();
    libevdev_uinput_destroy(ui_device);
    libevdev_free(device);
}

void TouchpadDevice::beginGesture(unsigned int fingers) {
    std::lock_guard<std::mutex> lock(_input_mutex);

    fingers = std::clamp(fingers, min_fingers, max_fingers);
    if (_active)
        _endGestureUnlocked();

    _active = true;
    _fingers = fingers;
    _resetContacts(fingers);
    _writeContacts();
    _setToolCount(_fingers, 1);
    _sendEvent(EV_KEY, BTN_TOUCH, 1);
    _sync();
}

void TouchpadDevice::moveGesture(int dx, int dy) {
    std::lock_guard<std::mutex> lock(_input_mutex);
    if (!_active)
        return;

    for (unsigned int i = 0; i < _fingers; i++) {
        _contacts[i].x =
            std::clamp(_contacts[i].x + dx, _touchpad_margin, _touchpad_width - _touchpad_margin);
        _contacts[i].y =
            std::clamp(_contacts[i].y + dy, _touchpad_margin, _touchpad_height - _touchpad_margin);
    }

    _writeContacts();
    _sync();
}

void TouchpadDevice::endGesture() {
    std::lock_guard<std::mutex> lock(_input_mutex);
    if (!_active)
        return;

    _endGestureUnlocked();
}

void TouchpadDevice::_sendEvent(uint type, uint code, int value) {
    libevdev_uinput_write_event(ui_device, type, code, value);
}

void TouchpadDevice::_sync() {
    libevdev_uinput_write_event(ui_device, EV_SYN, SYN_REPORT, 0);
}

void TouchpadDevice::_endGestureUnlocked() {
    for (unsigned int i = 0; i < _fingers; i++) {
        _sendEvent(EV_ABS, ABS_MT_SLOT, static_cast<int>(i));
        _sendEvent(EV_ABS, ABS_MT_TRACKING_ID, -1);
    }
    _setToolCount(_fingers, 0);
    _sendEvent(EV_KEY, BTN_TOUCH, 0);
    _sendEvent(EV_KEY, BTN_LEFT, 0);
    _sync();

    _active = false;
    _fingers = 0;
}

void TouchpadDevice::_resetContacts(unsigned int fingers) {
    const auto center_x = _touchpad_width / 2;
    const auto center_y = _touchpad_height / 2;
    const auto start_x = center_x - (static_cast<int>(fingers) - 1) * _contact_spacing / 2;

    for (unsigned int i = 0; i < fingers; i++) {
        _contacts[i].x = start_x + static_cast<int>(i) * _contact_spacing;
        _contacts[i].y = center_y;
        _contacts[i].tracking_id = _next_tracking_id++;
    }
}

void TouchpadDevice::_writeContacts() {
    for (unsigned int i = 0; i < _fingers; i++) {
        _sendEvent(EV_ABS, ABS_MT_SLOT, static_cast<int>(i));
        _sendEvent(EV_ABS, ABS_MT_TRACKING_ID, _contacts[i].tracking_id);
        _sendEvent(EV_ABS, ABS_MT_POSITION_X, _contacts[i].x);
        _sendEvent(EV_ABS, ABS_MT_POSITION_Y, _contacts[i].y);
    }

    _sendEvent(EV_ABS, ABS_X, _contacts[0].x);
    _sendEvent(EV_ABS, ABS_Y, _contacts[0].y);
}

void TouchpadDevice::_setToolCount(unsigned int fingers, int value) {
    _sendEvent(EV_KEY, _toolCode(fingers), value);
}

uint TouchpadDevice::_toolCode(unsigned int fingers) {
    switch (fingers) {
    case 3:
        return BTN_TOOL_TRIPLETAP;
    case 4:
        return BTN_TOOL_QUADTAP;
    default:
        return BTN_TOOL_FINGER;
    }
}
