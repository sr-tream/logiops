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
#include <UhidBatteryDevice.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace logid;

namespace {
constexpr uint8_t BatteryReportId = 1;
constexpr uint8_t IdentityReportId = 2;

constexpr uint8_t GenericInputDescriptor[] = {
    0x05, 0x0c,             // Usage Page (Consumer)
    0x09, 0x01,             // Usage (Consumer Control)
    0xa1, 0x01,             // Collection (Application)
    0x85, IdentityReportId, //   Report ID
    0x15, 0x00,             //   Logical Minimum (0)
    0x25, 0x01,             //   Logical Maximum (1)
    0x75, 0x01,             //   Report Size (1)
    0x95, 0x01,             //   Report Count (1)
    0x09, 0xe2,             //   Usage (Mute)
    0x81, 0x02,             //   Input (Data, Variable, Absolute)
    0x75, 0x07,             //   Report Size (7)
    0x95, 0x01,             //   Report Count (1)
    0x81, 0x03,             //   Input (Constant, Variable, Absolute)
    0xc0,                   // End Collection
};

constexpr uint8_t KeyboardInputDescriptor[] = {
    0x05, 0x01,             // Usage Page (Generic Desktop)
    0x09, 0x06,             // Usage (Keyboard)
    0xa1, 0x01,             // Collection (Application)
    0x85, IdentityReportId, //   Report ID
    0x05, 0x07,             //   Usage Page (Keyboard/Keypad)
    0x19, 0xe0,             //   Usage Minimum (Keyboard LeftControl)
    0x29, 0xe7,             //   Usage Maximum (Keyboard Right GUI)
    0x15, 0x00,             //   Logical Minimum (0)
    0x25, 0x01,             //   Logical Maximum (1)
    0x75, 0x01,             //   Report Size (1)
    0x95, 0x08,             //   Report Count (8)
    0x81, 0x02,             //   Input (Data, Variable, Absolute)
    0x95, 0x01,             //   Report Count (1)
    0x75, 0x08,             //   Report Size (8)
    0x81, 0x03,             //   Input (Constant, Variable, Absolute)
    0x95, 0x06,             //   Report Count (6)
    0x75, 0x08,             //   Report Size (8)
    0x15, 0x00,             //   Logical Minimum (0)
    0x25, 0x65,             //   Logical Maximum (101)
    0x05, 0x07,             //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,             //   Usage Minimum (Reserved)
    0x29, 0x65,             //   Usage Maximum (Keyboard Application)
    0x81, 0x00,             //   Input (Data, Array, Absolute)
    0xc0,                   // End Collection
};

constexpr uint8_t MouseInputDescriptor[] = {
    0x05, 0x01,             // Usage Page (Generic Desktop)
    0x09, 0x02,             // Usage (Mouse)
    0xa1, 0x01,             // Collection (Application)
    0x85, IdentityReportId, //   Report ID
    0x09, 0x01,             //   Usage (Pointer)
    0xa1, 0x00,             //   Collection (Physical)
    0x05, 0x09,             //     Usage Page (Button)
    0x19, 0x01,             //     Usage Minimum (Button 1)
    0x29, 0x03,             //     Usage Maximum (Button 3)
    0x15, 0x00,             //     Logical Minimum (0)
    0x25, 0x01,             //     Logical Maximum (1)
    0x75, 0x01,             //     Report Size (1)
    0x95, 0x03,             //     Report Count (3)
    0x81, 0x02,             //     Input (Data, Variable, Absolute)
    0x75, 0x05,             //     Report Size (5)
    0x95, 0x01,             //     Report Count (1)
    0x81, 0x03,             //     Input (Constant, Variable, Absolute)
    0x05, 0x01,             //     Usage Page (Generic Desktop)
    0x09, 0x30,             //     Usage (X)
    0x09, 0x31,             //     Usage (Y)
    0x09, 0x38,             //     Usage (Wheel)
    0x15, 0x81,             //     Logical Minimum (-127)
    0x25, 0x7f,             //     Logical Maximum (127)
    0x75, 0x08,             //     Report Size (8)
    0x95, 0x03,             //     Report Count (3)
    0x81, 0x06,             //     Input (Data, Variable, Relative)
    0xc0,                   //   End Collection
    0xc0,                   // End Collection
};

constexpr uint8_t BatteryReportDescriptor[] = {
    0x05, 0x06,            // Usage Page (Generic Device Controls)
    0x09, 0x20,            // Usage (Battery Strength)
    0xa1, 0x01,            // Collection (Application)
    0x85, BatteryReportId, //   Report ID
    0x15, 0x00,            //   Logical Minimum (0)
    0x25, 0x64,            //   Logical Maximum (100)
    0x75, 0x08,            //   Report Size (8)
    0x95, 0x01,            //   Report Count (1)
    0x09, 0x20,            //   Usage (Battery Strength)
    0x81, 0x02,            //   Input (Data, Variable, Absolute)
    0x05, 0x85,            //   Usage Page (Battery System)
    0x09, 0x44,            //   Usage (Charging)
    0x15, 0x00,            //   Logical Minimum (0)
    0x25, 0x01,            //   Logical Maximum (1)
    0x75, 0x01,            //   Report Size (1)
    0x95, 0x01,            //   Report Count (1)
    0x81, 0x02,            //   Input (Data, Variable, Absolute)
    0x75, 0x07,            //   Report Size (7)
    0x95, 0x01,            //   Report Count (1)
    0x81, 0x03,            //   Input (Constant, Variable, Absolute)
    0x05, 0x06,            //   Usage Page (Generic Device Controls)
    0x09, 0x20,            //   Usage (Battery Strength)
    0x75, 0x08,            //   Report Size (8)
    0x95, 0x01,            //   Report Count (1)
    0xb1, 0x02,            //   Feature (Data, Variable, Absolute)
    0xc0,                  // End Collection
};

void copyString(uint8_t *dest, size_t size, const std::string &source) {
  if (!size)
    return;

  std::snprintf(reinterpret_cast<char *>(dest), size, "%s", source.c_str());
}

void append(std::vector<uint8_t> &out, const uint8_t *begin, size_t size) {
  out.insert(out.end(), begin, begin + size);
}

std::vector<uint8_t> reportDescriptor(UhidBatteryDevice::DeviceKind kind) {
  std::vector<uint8_t> descriptor;
  switch (kind) {
  case UhidBatteryDevice::DeviceKind::Keyboard:
    append(descriptor, KeyboardInputDescriptor,
           sizeof(KeyboardInputDescriptor));
    break;
  case UhidBatteryDevice::DeviceKind::Mouse:
    append(descriptor, MouseInputDescriptor, sizeof(MouseInputDescriptor));
    break;
  case UhidBatteryDevice::DeviceKind::Generic:
    append(descriptor, GenericInputDescriptor, sizeof(GenericInputDescriptor));
    break;
  }
  append(descriptor, BatteryReportDescriptor, sizeof(BatteryReportDescriptor));
  return descriptor;
}
} // namespace

UhidBatteryDevice::UhidBatteryDevice(std::string name, std::string uniq,
                                     uint16_t product_id, uint8_t capacity,
                                     bool charging, DeviceKind kind)
    : _name(std::move(name)), _uniq(std::move(uniq)), _product_id(product_id),
      _kind(kind), _battery_capacity(std::min<uint8_t>(capacity, 100)),
      _charging(charging) {
  _fd = open("/dev/uhid", O_RDWR | O_CLOEXEC);
  if (_fd < 0)
    throw std::runtime_error(std::string("open(/dev/uhid): ") +
                             std::strerror(errno));

  if (!_sendCreate()) {
    close(_fd);
    _fd = -1;
    throw std::runtime_error("failed to create UHID battery device");
  }

  _running = true;
  _event_thread = std::thread([this]() { _eventLoop(); });
  _sendBatteryInput();
}

UhidBatteryDevice::~UhidBatteryDevice() {
  _running = false;

  if (_event_thread.joinable())
    _event_thread.join();

  if (_fd >= 0) {
    _sendDestroy();
    close(_fd);
    _fd = -1;
  }
}

void UhidBatteryDevice::update(uint8_t capacity, bool charging) {
  {
    std::lock_guard<std::mutex> lock(_state_mutex);
    _battery_capacity = std::min<uint8_t>(capacity, 100);
    _charging = charging;
  }

  _sendBatteryInput();
}

bool UhidBatteryDevice::_writeEvent(const uhid_event &event) {
  std::lock_guard<std::mutex> lock(_write_mutex);
  const auto *bytes = reinterpret_cast<const uint8_t *>(&event);
  size_t written = 0;

  while (written < sizeof(event)) {
    const ssize_t rc = write(_fd, bytes + written, sizeof(event) - written);
    if (rc < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    written += static_cast<size_t>(rc);
  }

  return true;
}

bool UhidBatteryDevice::_sendCreate() {
  uhid_event event{};
  event.type = UHID_CREATE2;

  copyString(event.u.create2.name, sizeof(event.u.create2.name), _name);
  copyString(event.u.create2.phys, sizeof(event.u.create2.phys),
             "logiops/uhid-battery");
  copyString(event.u.create2.uniq, sizeof(event.u.create2.uniq), _uniq);

  auto descriptor = reportDescriptor(_kind);
  if (descriptor.size() > sizeof(event.u.create2.rd_data))
    return false;

  event.u.create2.rd_size = descriptor.size();
  std::memcpy(event.u.create2.rd_data, descriptor.data(), descriptor.size());

  event.u.create2.bus = BUS_BLUETOOTH;
  event.u.create2.vendor = 0x046d;
  event.u.create2.product = _product_id;
  event.u.create2.version = 1;
  event.u.create2.country = 0;

  return _writeEvent(event);
}

void UhidBatteryDevice::_sendDestroy() {
  uhid_event event{};
  event.type = UHID_DESTROY;
  _writeEvent(event);
}

void UhidBatteryDevice::_sendBatteryInput() {
  auto state = _state();

  uhid_event event{};
  event.type = UHID_INPUT2;
  event.u.input2.size = 3;
  event.u.input2.data[0] = BatteryReportId;
  event.u.input2.data[1] = state.capacity;
  event.u.input2.data[2] = state.charging ? 1 : 0;
  _writeEvent(event);
}

void UhidBatteryDevice::_sendIdentityIdleInput() {
  uhid_event event{};
  event.type = UHID_INPUT2;
  event.u.input2.data[0] = IdentityReportId;

  switch (_kind) {
  case DeviceKind::Keyboard:
    event.u.input2.size = 9;
    break;
  case DeviceKind::Mouse:
    event.u.input2.size = 5;
    break;
  case DeviceKind::Generic:
    event.u.input2.size = 2;
    break;
  }

  _writeEvent(event);
}

void UhidBatteryDevice::_sendGetReportReply(uint32_t id) {
  uhid_event event{};
  event.type = UHID_GET_REPORT_REPLY;
  event.u.get_report_reply.id = id;
  event.u.get_report_reply.err = 0;
  event.u.get_report_reply.size = 2;
  event.u.get_report_reply.data[0] = BatteryReportId;
  event.u.get_report_reply.data[1] = _state().capacity;
  _writeEvent(event);
}

void UhidBatteryDevice::_sendSetReportReply(uint32_t id) {
  uhid_event event{};
  event.type = UHID_SET_REPORT_REPLY;
  event.u.set_report_reply.id = id;
  event.u.set_report_reply.err = 0;
  _writeEvent(event);
}

void UhidBatteryDevice::_eventLoop() {
  using clock = std::chrono::steady_clock;
  unsigned startup_resends = 0;
  auto next_startup_resend = clock::time_point{};

  pollfd pfd{};
  pfd.fd = _fd;
  pfd.events = POLLIN;

  while (_running) {
    if (startup_resends && clock::now() >= next_startup_resend) {
      _sendBatteryInput();
      --startup_resends;
      next_startup_resend = clock::now() + std::chrono::milliseconds(250);
    }

    const int rc = poll(&pfd, 1, startup_resends ? 100 : 250);
    if (rc < 0) {
      if (errno == EINTR)
        continue;
      return;
    }
    if (rc == 0 || !(pfd.revents & POLLIN))
      continue;

    uhid_event event{};
    const ssize_t read_size = read(_fd, &event, sizeof(event));
    if (read_size <= 0)
      return;

    switch (event.type) {
    case UHID_START:
    case UHID_OPEN:
      _sendIdentityIdleInput();
      _sendBatteryInput();
      startup_resends = 12;
      next_startup_resend = clock::now() + std::chrono::milliseconds(250);
      break;
    case UHID_GET_REPORT:
      _sendGetReportReply(event.u.get_report.id);
      break;
    case UHID_SET_REPORT:
      _sendSetReportReply(event.u.set_report.id);
      break;
    default:
      break;
    }
  }
}

UhidBatteryDevice::State UhidBatteryDevice::_state() const {
  std::lock_guard<std::mutex> lock(_state_mutex);
  return {_battery_capacity, _charging};
}
