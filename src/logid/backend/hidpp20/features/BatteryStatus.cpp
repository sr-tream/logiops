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
#include <backend/hidpp20/features/BatteryStatus.h>

#include <array>
#include <cassert>
#include <cmath>
#include <optional>
#include <utility>

using namespace logid::backend::hidpp20;

namespace {
std::optional<uint8_t> estimateBatteryPercentage(uint16_t voltage) {
  static constexpr std::array<std::pair<uint16_t, uint8_t>, 13>
      voltage_to_percentage{{
          {4186, 100},
          {4067, 90},
          {3989, 80},
          {3922, 70},
          {3859, 60},
          {3811, 50},
          {3778, 40},
          {3751, 30},
          {3717, 20},
          {3671, 10},
          {3646, 5},
          {3579, 2},
          {3500, 0},
      }};

  if (voltage >= voltage_to_percentage.front().first)
    return voltage_to_percentage.front().second;
  if (voltage <= voltage_to_percentage.back().first)
    return voltage_to_percentage.back().second;

  for (size_t i = 0; i + 1 < voltage_to_percentage.size(); ++i) {
    auto [v_high, p_high] = voltage_to_percentage[i];
    auto [v_low, p_low] = voltage_to_percentage[i + 1];
    if (voltage < v_low || voltage > v_high)
      continue;

    double ratio = static_cast<double>(voltage - v_low) / (v_high - v_low);
    double percent = p_low + (p_high - p_low) * ratio;
    return static_cast<uint8_t>(std::round(percent));
  }

  return std::nullopt;
}

BatteryStatus::State parseState(uint8_t state) {
  using State = BatteryStatus::State;
  switch (state) {
  case static_cast<uint8_t>(State::Discharging):
  case static_cast<uint8_t>(State::Recharging):
  case static_cast<uint8_t>(State::AlmostFull):
  case static_cast<uint8_t>(State::Full):
  case static_cast<uint8_t>(State::SlowRecharge):
  case static_cast<uint8_t>(State::InvalidBattery):
  case static_cast<uint8_t>(State::ThermalError):
    return static_cast<State>(state);
  default:
    return State::Unknown;
  }
}
} // namespace

BatteryStatus::BatteryStatus(Device *dev) : Feature(dev, ID) {}

BatteryStatus::Status BatteryStatus::getBattery() {
  std::vector<uint8_t> params;
  auto response = callFunction(GetBatteryLevelStatus, params);
  return parseStatus(response.begin(), response.end());
}

BatteryStatus::Status
BatteryStatus::batteryStatusEvent(const hidpp::Report &report) {
  assert(report.function() == GetBatteryLevelStatus);
  return parseStatus(report.paramBegin(), report.paramEnd());
}

BatteryStatus::Status
BatteryStatus::parseStatus(std::vector<uint8_t>::const_iterator begin,
                           std::vector<uint8_t>::const_iterator end) {
  Status status{};
  status.state = State::Unknown;

  if (std::distance(begin, end) < 3)
    return status;

  const uint8_t level = *begin++;
  const uint8_t next_level = *begin++;
  const uint8_t state = *begin;

  if (level > 0 && level <= 100)
    status.level = level;

  if (next_level > 0 && next_level <= 100)
    status.nextLevel = next_level;

  status.state = parseState(state);

  return status;
}

BatteryVoltage::BatteryVoltage(Device *dev) : Feature(dev, ID) {}

BatteryStatus::Status BatteryVoltage::getBattery() {
  std::vector<uint8_t> params;
  auto response = callFunction(GetBatteryVoltage, params);
  return parseStatus(response.begin(), response.end());
}

BatteryStatus::Status
BatteryVoltage::batteryStatusEvent(const hidpp::Report &report) {
  assert(report.function() == GetBatteryVoltage);
  return parseStatus(report.paramBegin(), report.paramEnd());
}

BatteryStatus::Status
BatteryVoltage::parseStatus(std::vector<uint8_t>::const_iterator begin,
                            std::vector<uint8_t>::const_iterator end) {
  BatteryStatus::Status status{};
  status.state = BatteryStatus::State::Unknown;

  if (std::distance(begin, end) < 3)
    return status;

  const uint16_t voltage = static_cast<uint16_t>(*begin++) << 8 | *begin++;
  const uint8_t flags = *begin;

  status.level = estimateBatteryPercentage(voltage);
  status.state = BatteryStatus::State::Discharging;

  if (flags & (1 << 7)) {
    status.state = BatteryStatus::State::Recharging;
    if ((flags & 0x03) == 0x01)
      status.state = BatteryStatus::State::Full;
  }
  if (flags & (1 << 4))
    status.state = BatteryStatus::State::SlowRecharge;

  return status;
}

UnifiedBattery::UnifiedBattery(Device *dev) : Feature(dev, ID) {}

BatteryStatus::Status UnifiedBattery::getBattery() {
  std::vector<uint8_t> params;
  auto response = callFunction(GetBatteryStatus, params);
  return parseStatus(response.begin(), response.end());
}

BatteryStatus::Status
UnifiedBattery::batteryStatusEvent(const hidpp::Report &report) {
  return parseStatus(report.paramBegin(), report.paramEnd());
}

BatteryStatus::Status
UnifiedBattery::parseStatus(std::vector<uint8_t>::const_iterator begin,
                            std::vector<uint8_t>::const_iterator end) {
  BatteryStatus::Status status{};
  status.state = BatteryStatus::State::Unknown;

  if (std::distance(begin, end) < 4)
    return status;

  const uint8_t discharge = *begin++;
  const uint8_t level = *begin++;
  const uint8_t state = *begin;

  if (discharge > 0 && discharge <= 100) {
    status.level = discharge;
  } else {
    switch (level) {
    case 8:
      status.level = 90;
      break;
    case 4:
      status.level = 50;
      break;
    case 2:
      status.level = 20;
      break;
    case 1:
      status.level = 5;
      break;
    default:
      break;
    }
  }

  status.state = parseState(state);
  return status;
}
