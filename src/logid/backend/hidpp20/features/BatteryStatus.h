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
#ifndef LOGID_BACKEND_HIDPP20_FEATURE_BATTERYSTATUS_H
#define LOGID_BACKEND_HIDPP20_FEATURE_BATTERYSTATUS_H

#include <backend/hidpp/Report.h>
#include <backend/hidpp20/Feature.h>
#include <backend/hidpp20/feature_defs.h>
#include <optional>
#include <vector>

namespace logid::backend::hidpp20 {
class BatteryStatus : public Feature {
public:
  static constexpr uint16_t ID = FeatureID::BATTERY_STATUS;

  [[nodiscard]] uint16_t getID() final { return ID; }

  explicit BatteryStatus(Device *dev);

  enum Function : uint8_t { GetBatteryLevelStatus = 0 };

  enum class State : uint8_t {
    Discharging = 0x00,
    Recharging = 0x01,
    AlmostFull = 0x02,
    Full = 0x03,
    SlowRecharge = 0x04,
    InvalidBattery = 0x05,
    ThermalError = 0x06,
    Unknown = 0xff
  };

  struct Status {
    std::optional<uint8_t> level;
    std::optional<uint8_t> nextLevel;
    State state;
  };

  [[nodiscard]] Status getBattery();

  [[nodiscard]] static Status batteryStatusEvent(const hidpp::Report &report);

private:
  [[nodiscard]] static Status
  parseStatus(std::vector<uint8_t>::const_iterator begin,
              std::vector<uint8_t>::const_iterator end);
};

class BatteryVoltage : public Feature {
public:
  static constexpr uint16_t ID = FeatureID::BATTERY_VOLTAGE;

  [[nodiscard]] uint16_t getID() final { return ID; }

  explicit BatteryVoltage(Device *dev);

  enum Function : uint8_t { GetBatteryVoltage = 0 };

  [[nodiscard]] BatteryStatus::Status getBattery();

  [[nodiscard]] static BatteryStatus::Status
  batteryStatusEvent(const hidpp::Report &report);

private:
  [[nodiscard]] static BatteryStatus::Status
  parseStatus(std::vector<uint8_t>::const_iterator begin,
              std::vector<uint8_t>::const_iterator end);
};

class UnifiedBattery : public Feature {
public:
  static constexpr uint16_t ID = FeatureID::UNIFIED_BATTERY;

  [[nodiscard]] uint16_t getID() final { return ID; }

  explicit UnifiedBattery(Device *dev);

  enum Function : uint8_t { GetBatteryCapability = 0, GetBatteryStatus = 1 };

  [[nodiscard]] BatteryStatus::Status getBattery();

  [[nodiscard]] static BatteryStatus::Status
  batteryStatusEvent(const hidpp::Report &report);

private:
  [[nodiscard]] static BatteryStatus::Status
  parseStatus(std::vector<uint8_t>::const_iterator begin,
              std::vector<uint8_t>::const_iterator end);
};
} // namespace logid::backend::hidpp20

#endif // LOGID_BACKEND_HIDPP20_FEATURE_BATTERYSTATUS_H
