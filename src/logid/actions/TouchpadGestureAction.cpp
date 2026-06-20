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

using namespace logid::actions;
using namespace logid::backend;

const char* TouchpadGestureAction::interface_name = "TouchpadGesture";

namespace {
    static constexpr double default_scale = 8.0;
}

TouchpadGestureAction::TouchpadGestureAction(
    Device* device, config::TouchpadGestureAction& config,
    [[maybe_unused]] const std::shared_ptr<ipcgull::node>& parent)
    : Action(device, interface_name,
             {{{"GetConfig", {this, &TouchpadGestureAction::getConfig, {"fingers", "scale"}}},
               {"SetFingers", {this, &TouchpadGestureAction::setFingers, {"fingers"}}},
               {"SetScale", {this, &TouchpadGestureAction::setScale, {"scale"}}}},
              {},
              {}}),
      _config(config) {}

void TouchpadGestureAction::press() {
    std::shared_lock lock(_config_mutex);
    _pressed = true;
    _device->virtualTouchpad()->beginGesture(_fingers());
}

void TouchpadGestureAction::release() {
    std::shared_lock lock(_config_mutex);
    _pressed = false;
    _device->virtualTouchpad()->endGesture();
}

void TouchpadGestureAction::move(int16_t x, int16_t y) {
    std::shared_lock lock(_config_mutex);
    if (!_pressed)
        return;

    const auto scale = _scale();
    _device->virtualTouchpad()->moveGesture(
        static_cast<int>(std::lround(static_cast<double>(x) * scale)),
        static_cast<int>(std::lround(static_cast<double>(y) * scale)));
}

uint8_t TouchpadGestureAction::reprogFlags() const {
    return (hidpp20::ReprogControls::TemporaryDiverted | hidpp20::ReprogControls::RawXYDiverted);
}

std::tuple<unsigned int, double> TouchpadGestureAction::getConfig() const {
    std::shared_lock lock(_config_mutex);
    return {_fingers(), _scale()};
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

unsigned int TouchpadGestureAction::_fingers() const {
    return std::clamp(_config.fingers.value_or(TouchpadDevice::min_fingers),
                      TouchpadDevice::min_fingers, TouchpadDevice::max_fingers);
}

double TouchpadGestureAction::_scale() const {
    return _config.scale.value_or(default_scale);
}
