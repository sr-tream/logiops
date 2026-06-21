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
#include <features/BatteryStatus.h>

#include <algorithm>
#include <backend/Error.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <util/log.h>
#include <util/task.h>

using namespace logid::features;
using namespace logid::backend;

namespace {
uint64_t fnv1a64(const std::string &value) {
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : value) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

uint8_t inferredCapacity(hidpp20::BatteryStatus::State state) {
  using State = hidpp20::BatteryStatus::State;
  switch (state) {
  case State::Full:
    return 100;
  case State::AlmostFull:
  case State::Recharging:
    return 90;
  case State::SlowRecharge:
    return 20;
  default:
    return 0;
  }
}
} // namespace

BatteryStatus::BatteryStatus(Device *dev) : DeviceFeature(dev) {
  try {
    auto feature = std::make_shared<hidpp20::UnifiedBattery>(&dev->hidpp20());
    _readers.push_back({
        "UNIFIED_BATTERY",
        feature,
        [feature]() { return feature->getBattery(); },
        [](const hidpp::Report &report) {
          return hidpp20::UnifiedBattery::batteryStatusEvent(report);
        },
        hidpp20::UnifiedBattery::GetBatteryCapability,
        30,
    });
  } catch (hidpp20::UnsupportedFeature &e) {
  }

  try {
    auto feature = std::make_shared<hidpp20::BatteryStatus>(&dev->hidpp20());
    _readers.push_back({
        "BATTERY_STATUS",
        feature,
        [feature]() { return feature->getBattery(); },
        [](const hidpp::Report &report) {
          return hidpp20::BatteryStatus::batteryStatusEvent(report);
        },
        hidpp20::BatteryStatus::GetBatteryLevelStatus,
        20,
    });
  } catch (hidpp20::UnsupportedFeature &e) {
  }

  try {
    auto feature = std::make_shared<hidpp20::BatteryVoltage>(&dev->hidpp20());
    _readers.push_back({
        "BATTERY_VOLTAGE",
        feature,
        [feature]() { return feature->getBattery(); },
        [](const hidpp::Report &report) {
          return hidpp20::BatteryVoltage::batteryStatusEvent(report);
        },
        hidpp20::BatteryVoltage::GetBatteryVoltage,
        10,
    });
  } catch (hidpp20::UnsupportedFeature &e) {
  }

  if (_readers.empty())
    throw UnsupportedFeature();
}

void BatteryStatus::configure() {
  _readAndPublish("initial");
  _scheduleStartupRefreshes();
}

void BatteryStatus::_readAndPublish(const char *context) {
  std::optional<HidppBatteryStatus> selected;
  std::string selected_source;
  std::optional<uint8_t> selected_capacity;
  int selected_priority = -1;
  bool any_charging = false;

  for (auto &reader : _readers) {
    try {
      auto status = reader.read();
      auto capacity = _capacity(status);
      if (!capacity)
        continue;

      any_charging = any_charging || _charging(status);

      if (!selected || reader.priority > selected_priority ||
          (reader.priority == selected_priority &&
           *capacity > selected_capacity.value_or(0))) {
        selected = status;
        selected_source = reader.source;
        selected_capacity = capacity;
        selected_priority = reader.priority;
      }
    } catch (std::exception &e) {
      logPrintf(DEBUG, "Failed to read battery status for %s: %s",
                _device->name().c_str(), e.what());
    }
  }

  if (selected) {
    if (any_charging && selected->state != hidpp20::BatteryStatus::State::Full &&
        !_charging(*selected)) {
      selected->state = hidpp20::BatteryStatus::State::Recharging;
    }

    _publish(*selected, std::string(context) + "/" + selected_source);
  }
}

void BatteryStatus::_scheduleStartupRefreshes() {
  auto self_weak = self<BatteryStatus>();
  for (auto delay : {1000, 2500, 5000}) {
    run_task_after(
        [self_weak]() {
          if (auto self = self_weak.lock())
            self->_readAndPublish("startup-refresh");
        },
        std::chrono::milliseconds(delay));
  }
}

void BatteryStatus::listen() {
  if (!_ev_handler.empty())
    return;

  auto readers = _readers;
  _ev_handler = _device->hidpp20().addEventHandler(
      {[readers](const hidpp::Report &report) -> bool {
         return std::any_of(
             readers.begin(), readers.end(), [&report](const Reader &reader) {
               return report.feature() == reader.feature->featureIndex() &&
                      report.function() == reader.eventFunction;
             });
       },
       [readers,
        self_weak = self<BatteryStatus>()](const hidpp::Report &report) {
         if (auto self = self_weak.lock()) {
           auto reader = std::find_if(
               readers.begin(), readers.end(),
               [&report](const Reader &candidate) {
                 return report.feature() == candidate.feature->featureIndex() &&
                        report.function() == candidate.eventFunction;
               });
           if (reader != readers.end())
             self->_readAndPublish(reader->source);
         }
       }});
}

void BatteryStatus::setProfile(config::Profile &) {}

void BatteryStatus::onSleep() {
  auto generation = ++_sleep_generation;
  auto self_weak = self<BatteryStatus>();
  run_task_after(
      [self_weak, generation]() {
        if (auto self = self_weak.lock()) {
          if (self->_sleep_generation.load() == generation)
            self->_withdrawUhidBattery("sleep");
        }
      },
      std::chrono::seconds(15));
}

void BatteryStatus::onWakeup() { ++_sleep_generation; }

void BatteryStatus::_withdrawUhidBattery(const char *reason) {
  if (!_uhid_battery)
    return;

  logPrintf(DEBUG, "Withdrawing UHID battery for %s after %s",
            _device->name().c_str(), reason);
  _uhid_battery.reset();
}

void BatteryStatus::_publish(const HidppBatteryStatus &status,
                             const std::string &source) {
  auto capacity = _capacity(status);
  if (!capacity)
    return;

  logPrintf(DEBUG,
            "Battery status for %s via %s: %u%%, state 0x%02x, "
            "charging %s",
            _device->name().c_str(), source.c_str(), *capacity,
            static_cast<uint8_t>(status.state),
            _charging(status) ? "yes" : "no");

  if (!_uhid_battery) {
    try {
      _uhid_battery = std::make_unique<UhidBatteryDevice>(
          _device->name(), _uniqueId(), _device->pid(), *capacity,
          _charging(status), _deviceKind());
      logPrintf(INFO, "Publishing battery for %s through UHID at %u%% (%s)",
                _device->name().c_str(), *capacity,
                _charging(status) ? "charging" : "discharging");
    } catch (std::exception &e) {
      if (!_uhid_error_logged) {
        logPrintf(WARN, "Could not publish battery for %s through UHID: %s",
                  _device->name().c_str(), e.what());
        _uhid_error_logged = true;
      }
      return;
    }
  } else {
    _uhid_battery->update(*capacity, _charging(status));
  }
}

std::optional<uint8_t>
BatteryStatus::_capacity(const HidppBatteryStatus &status) const {
  if (status.level)
    return status.level;

  uint8_t inferred = inferredCapacity(status.state);
  if (inferred)
    return inferred;

  return std::nullopt;
}

bool BatteryStatus::_charging(const HidppBatteryStatus &status) const {
  using State = hidpp20::BatteryStatus::State;
  switch (status.state) {
  case State::Recharging:
  case State::AlmostFull:
  case State::SlowRecharge:
    return true;
  default:
    return false;
  }
}

logid::UhidBatteryDevice::DeviceKind BatteryStatus::_deviceKind() const {
  using DeviceKind = UhidBatteryDevice::DeviceKind;
  using namespace backend::hidpp;

  switch (_device->deviceType()) {
  case DeviceKeyboard:
  case DeviceNumpad:
  case DevicePresenter:
    return DeviceKind::Keyboard;
  case DeviceMouse:
  case DeviceTrackball:
  case DeviceTouchpad:
    return DeviceKind::Mouse;
  case DeviceUnknown:
  default:
    return DeviceKind::Generic;
  }
}

std::string BatteryStatus::_uniqueId() const {
  std::ostringstream out;
  out << "logiops-" << std::hex << std::setfill('0') << std::setw(4)
      << _device->pid() << "-" << std::dec
      << static_cast<int>(_device->hidpp20().deviceIndex()) << "-" << std::hex
      << fnv1a64(_device->hidpp20().devicePath());
  return out.str();
}
