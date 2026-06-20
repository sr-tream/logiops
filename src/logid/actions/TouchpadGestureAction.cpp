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

#include <Device.h>
#include <TouchpadDevice.h>
#include <actions/TouchpadGestureAction.h>
#include <algorithm>
#include <backend/hidpp20/features/ReprogControls.h>
#include <cmath>
#include <cstdlib>
#include <util/log.h>

using namespace logid::actions;
using namespace logid::backend;

const char* TouchpadGestureAction::interface_name = "TouchpadGesture";

namespace {
    static constexpr double default_scale = 4.0;
    static constexpr int default_click_threshold = 5;
}

TouchpadGestureAction::TouchpadGestureAction(
    Device* device, config::TouchpadGestureAction& config,
    const std::shared_ptr<ipcgull::node>& parent)
    : Action(device, interface_name, {
            {
                    {"GetConfig", {this, &TouchpadGestureAction::getConfig,
                                   {"fingers", "scale", "invert", "click_threshold"}}},
                    {"SetFingers", {this, &TouchpadGestureAction::setFingers, {"fingers"}}},
                    {"SetScale", {this, &TouchpadGestureAction::setScale, {"scale"}}},
                    {"SetInvert", {this, &TouchpadGestureAction::setInvert, {"invert"}}},
                    {"SetClickThreshold",
                     {this, &TouchpadGestureAction::setClickThreshold, {"threshold"}}}
            },
            {},
            {}
    }),
      _click_node(parent->make_child("click")),
      _config(config) {
    if (_config.click.has_value()) {
        try {
            _click_action = Action::makeAction(device, _config.click.value(), _click_node);
        } catch (InvalidAction& e) {
            logPrintf(WARN, "Mapping touchpad click to invalid action");
        }
    }
}

void TouchpadGestureAction::press() {
    std::shared_lock lock(_config_mutex);
    _pressed = true;
    _movement = 0;
    _pending_x = 0;
    _pending_y = 0;
    _touchpad_active = false;

    if (!_click_action)
        _beginTouchpadGesture();
}

void TouchpadGestureAction::release() {
    std::shared_lock lock(_config_mutex);
    if (_touchpad_active) {
        _device->virtualTouchpad()->endGesture();
    } else if (_click_action && _movement <= _clickThreshold()) {
        _click_action->press();
        _click_action->release();
    }

    _pressed = false;
    _touchpad_active = false;
}

void TouchpadGestureAction::move(int16_t x, int16_t y) {
    std::shared_lock lock(_config_mutex);
    if (!_pressed)
        return;

    const auto scale = _scale();
    auto scaled_x = static_cast<int>(std::lround(static_cast<double>(x) * scale));
    auto scaled_y = static_cast<int>(std::lround(static_cast<double>(y) * scale));
    if (_invert()) {
        scaled_x = -scaled_x;
        scaled_y = -scaled_y;
    }

    _movement += std::abs(x) + std::abs(y);

    if (!_touchpad_active) {
        _pending_x += scaled_x;
        _pending_y += scaled_y;
        if (_movement <= _clickThreshold())
            return;

        _beginTouchpadGesture();
        _device->virtualTouchpad()->moveGesture(_pending_x, _pending_y);
        _pending_x = 0;
        _pending_y = 0;
        return;
    }

    _device->virtualTouchpad()->moveGesture(scaled_x, scaled_y);
}

uint8_t TouchpadGestureAction::reprogFlags() const {
    return (hidpp20::ReprogControls::TemporaryDiverted | hidpp20::ReprogControls::RawXYDiverted);
}

std::tuple<unsigned int, double, bool, int> TouchpadGestureAction::getConfig() const {
    std::shared_lock lock(_config_mutex);
    return {_fingers(), _scale(), _invert(), _clickThreshold()};
}

void TouchpadGestureAction::setFingers(unsigned int fingers) {
    std::unique_lock lock(_config_mutex);
    _config.fingers = std::clamp(fingers, TouchpadDevice::min_fingers, TouchpadDevice::max_fingers);
}

void TouchpadGestureAction::setScale(double scale) {
    std::unique_lock lock(_config_mutex);
    if (scale == 0) {
        _config.scale.reset();
    } else {
        _config.scale = scale;
    }
}

void TouchpadGestureAction::setInvert(bool invert) {
    std::unique_lock lock(_config_mutex);
    _config.invert = invert;
}

void TouchpadGestureAction::setClickThreshold(int threshold) {
    std::unique_lock lock(_config_mutex);
    if (threshold == default_click_threshold) {
        _config.click_threshold.reset();
    } else {
        _config.click_threshold = std::max(0, threshold);
    }
}

unsigned int TouchpadGestureAction::_fingers() const {
    return std::clamp(_config.fingers.value_or(TouchpadDevice::min_fingers),
                      TouchpadDevice::min_fingers, TouchpadDevice::max_fingers);
}

double TouchpadGestureAction::_scale() const {
    return _config.scale.value_or(default_scale);
}

bool TouchpadGestureAction::_invert() const {
    return _config.invert.value_or(false);
}

int TouchpadGestureAction::_clickThreshold() const {
    return std::max(0, _config.click_threshold.value_or(default_click_threshold));
}

void TouchpadGestureAction::_beginTouchpadGesture() {
    _device->virtualTouchpad()->beginGesture(_fingers());
    _touchpad_active = true;
}
