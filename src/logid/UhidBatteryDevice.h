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
#ifndef LOGID_UHIDBATTERYDEVICE_H
#define LOGID_UHIDBATTERYDEVICE_H

#include <atomic>
#include <cstdint>
#include <linux/uhid.h>
#include <mutex>
#include <string>
#include <thread>

namespace logid {
class UhidBatteryDevice {
public:
  enum class DeviceKind { Generic, Keyboard, Mouse };

  UhidBatteryDevice(std::string name, std::string uniq, uint16_t product_id,
                    uint8_t capacity, bool charging,
                    DeviceKind kind = DeviceKind::Generic);

  ~UhidBatteryDevice();

  void update(uint8_t capacity, bool charging);

  UhidBatteryDevice(const UhidBatteryDevice &) = delete;

  UhidBatteryDevice(UhidBatteryDevice &&) = delete;

private:
  bool _writeEvent(const uhid_event &event);

  bool _sendCreate();

  void _sendDestroy();

  void _sendBatteryInput();

  void _sendIdentityIdleInput();

  void _sendGetReportReply(uint32_t id);

  void _sendSetReportReply(uint32_t id);

  void _eventLoop();

  struct State {
    uint8_t capacity;
    bool charging;
  };

  [[nodiscard]] State _state() const;

  const std::string _name;
  const std::string _uniq;
  const uint16_t _product_id;
  const DeviceKind _kind;

  int _fd{-1};
  std::atomic_bool _running{false};
  std::thread _event_thread;

  mutable std::mutex _state_mutex;
  uint8_t _battery_capacity;
  bool _charging;

  std::mutex _write_mutex;
};
} // namespace logid

#endif // LOGID_UHIDBATTERYDEVICE_H
