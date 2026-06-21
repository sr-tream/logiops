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
#ifndef LOGID_FEATURE_BATTERYSTATUS_H
#define LOGID_FEATURE_BATTERYSTATUS_H

#include <Device.h>
#include <UhidBatteryDevice.h>
#include <backend/hidpp20/features/BatteryStatus.h>
#include <features/DeviceFeature.h>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace logid::features {
class BatteryStatus : public DeviceFeature {
public:
  void configure() final;

  void listen() final;

  void setProfile(config::Profile &profile) final;

  void onSleep() final;

  void onWakeup() final;

protected:
  explicit BatteryStatus(Device *dev);

private:
  using HidppBatteryStatus = backend::hidpp20::BatteryStatus::Status;

  void _readAndPublish(const char *context);

  void _scheduleStartupRefreshes();

  void _withdrawUhidBattery(const char *reason);

  void _publish(const HidppBatteryStatus &status, const std::string &source);

  [[nodiscard]] std::optional<uint8_t>
  _capacity(const HidppBatteryStatus &status) const;

  [[nodiscard]] bool _charging(const HidppBatteryStatus &status) const;

  [[nodiscard]] UhidBatteryDevice::DeviceKind _deviceKind() const;

  [[nodiscard]] std::string _uniqueId() const;

  struct Reader {
    const char *source;
    std::shared_ptr<backend::hidpp20::Feature> feature;
    std::function<HidppBatteryStatus()> read;
    std::function<HidppBatteryStatus(const backend::hidpp::Report &)>
        parseEvent;
    uint8_t eventFunction;
    int priority;
  };

  EventHandlerLock<backend::hidpp::Device> _ev_handler;
  std::vector<Reader> _readers;
  std::unique_ptr<UhidBatteryDevice> _uhid_battery;
  std::atomic_uint64_t _sleep_generation{0};
  bool _uhid_error_logged{false};
};
} // namespace logid::features

#endif // LOGID_FEATURE_BATTERYSTATUS_H
