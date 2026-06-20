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

#include <backend/hidpp10/ReceiverMonitor.h>
#include <backend/Error.h>
#include <util/task.h>
#include <util/log.h>

using namespace logid::backend::hidpp10;
using namespace logid::backend::hidpp;

ReceiverMonitor::ReceiverMonitor(const std::string& path,
                                 const std::shared_ptr<raw::DeviceMonitor>& monitor, double timeout)
        : _receiver(Receiver::make(path, monitor, timeout)) {

    Receiver::NotificationFlags notification_flags{true, true, true};
    _receiver->setNotifications(notification_flags);
}

void ReceiverMonitor::_ready() {
    if (_connect_ev_handler.empty()) {
        _connect_ev_handler = _receiver->rawDevice()->addEventHandler(
                {[](const std::vector<uint8_t>& report) -> bool {
                    if (report[Offset::Type] == Report::Type::Short ||
                        report[Offset::Type] == Report::Type::Long) {
                        uint8_t sub_id = report[Offset::SubID];
                        return (sub_id == Receiver::DeviceConnection ||
                                sub_id == Receiver::DeviceDisconnection);
                    }
                    return false;
                }, [self_weak = _self](const std::vector<uint8_t>& raw) -> void {
                    /* Running in a new thread prevents deadlocks since the
                     * receiver may be enumerating.
                     */
                    hidpp::Report report(raw);

                    if (auto self = self_weak.lock()) {
                        run_task([self_weak, report]() {
                            auto self = self_weak.lock();
                            if (!self)
                                return;

                            if (report.subId() == Receiver::DeviceConnection) {
                                self->_addHandler(Receiver::deviceConnectionEvent(report));
                            } else if (report.subId() == Receiver::DeviceDisconnection) {
                                self->_removeHandler(Receiver::deviceDisconnectionEvent(report));
                            }
                        });
                    }

                }
                });
    }

    if (_discover_ev_handler.empty()) {
        _discover_ev_handler = _receiver->addEventHandler(
                {[](const hidpp::Report& report) -> bool {
                    return (report.subId() == Receiver::DeviceDiscovered) &&
                           (report.type() == Report::Type::Long);
                },
                 [self_weak = _self](const hidpp::Report& report) {
                     auto self = self_weak.lock();
                     if (!self)
                         return;
                     std::lock_guard lock(self->_pair_mutex);
                     if (self->_pair_state == Discovering) {
                         bool filled = Receiver::fillDeviceDiscoveryEvent(
                                 self->_discovery_event, report);

                         if (filled) {
                             self->_pair_state = FindingPasskey;
                             run_task([self_weak, event = self->_discovery_event]() {
                                 if (auto self = self_weak.lock())
                                     self->receiver()->startBoltPairing(event);
                             });
                         }
                     }
                 }
                });
    }

    if (_passkey_ev_handler.empty()) {
        _passkey_ev_handler = _receiver->addEventHandler(
                {[](const hidpp::Report& report) -> bool {
                    return report.subId() == Receiver::PasskeyRequest &&
                           report.type() == hidpp::Report::Type::Long;
                },
                 [self_weak = _self](const hidpp::Report& report) {
                     if (auto self = self_weak.lock()) {
                         std::lock_guard lock(self->_pair_mutex);
                         if (self->_pair_state == FindingPasskey) {
                             auto passkey = Receiver::passkeyEvent(report);

                             self->_pair_state = Pairing;
                             self->pairReady(self->_discovery_event, passkey);
                         }
                     }
                 }
                });
    }

    if (_pair_status_handler.empty()) {
        _pair_status_handler = _receiver->addEventHandler(
                {[](const hidpp::Report& report) -> bool {
                    return report.subId() == Receiver::DiscoveryStatus ||
                           report.subId() == Receiver::PairStatus ||
                           report.subId() == Receiver::BoltPairStatus;
                },
                 [self_weak = _self](const hidpp::Report& report) {
                     auto self = self_weak.lock();
                     if (!self)
                         return;

                     std::lock_guard lock(self->_pair_mutex);
                     // TODO: forward status to user
                     if (report.subId() == Receiver::DiscoveryStatus) {
                         auto event = Receiver::discoveryStatusEvent(report);

                         if (self->_pair_state == Discovering && !event.discovering)
                             self->_pair_state = NotPairing;
                     } else if (report.subId() == Receiver::PairStatus) {
                         auto event = Receiver::pairStatusEvent(report);

                         if ((self->_pair_state == FindingPasskey ||
                              self->_pair_state == Pairing) && !event.pairing)
                             self->_pair_state = NotPairing;
                     } else if (report.subId() == Receiver::BoltPairStatus) {
                         auto event = Receiver::boltPairStatusEvent(report);

                         if ((self->_pair_state == FindingPasskey ||
                              self->_pair_state == Pairing) && !event.pairing)
                             self->_pair_state = NotPairing;
                     }
                 }
                });
    }

    enumerate();
}

void ReceiverMonitor::enumerate() {
    _receiver->enumerate();
    if (_receiver->rawDevice()->busType() != logid::backend::raw::RawDevice::USB)
        return;

    run_task_after([self_weak = _self]() {
        if (auto self = self_weak.lock())
            self->_enumeratePairedDevices();
    }, std::chrono::milliseconds(ready_backoff));
}

void ReceiverMonitor::waitForDevice(hidpp::DeviceIndex index) {
    const std::lock_guard lock(_wait_mutex);
    if (!_waiters.count(index)) {
        _waiters.emplace(index, _receiver->rawDevice()->addEventHandler(
                {[index](const std::vector<uint8_t>& report) -> bool {
                    /* Connection events should be handled by connect_ev_handler */
                    auto sub_id = report[Offset::SubID];
                    return report[Offset::DeviceIndex] == index &&
                           sub_id != Receiver::DeviceConnection &&
                           sub_id != Receiver::DeviceDisconnection;
                },
                 [self_weak = _self, index](
                         [[maybe_unused]] const std::vector<uint8_t>& report) {
                     hidpp::DeviceConnectionEvent event{};
                     event.withPayload = false;
                     event.linkEstablished = true;
                     event.index = index;
                     event.fromTimeoutCheck = true;

                     run_task([self_weak, event]() {
                         if (auto self = self_weak.lock())
                             self->_addHandler(event);
                     });
                 }
                }));
    }
}

std::shared_ptr<Receiver> ReceiverMonitor::receiver() const {
    return _receiver;
}

void ReceiverMonitor::_startPair(uint8_t timeout) {
    {
        std::lock_guard lock(_pair_mutex);
        _pair_state = _receiver->bolt() ? Discovering : Pairing;
        _discovery_event = {};
    }

    if (_receiver->bolt())
        receiver()->startDiscover(timeout);
    else
        receiver()->startPairing(timeout);
}

void ReceiverMonitor::_stopPair() {
    PairState last_state;
    {
        std::lock_guard lock(_pair_mutex);
        last_state = _pair_state;
        _pair_state = NotPairing;
    }

    if (last_state == Discovering)
        receiver()->stopDiscover();
    else if (last_state == Pairing || last_state == FindingPasskey)
        receiver()->stopPairing();
}

void ReceiverMonitor::_addHandler(const hidpp::DeviceConnectionEvent& event, int tries) {
    if (event.fromTimeoutCheck && tries == 0) {
        const std::lock_guard lock(_wait_mutex);
        if (_pending_adds.contains(event.index))
            return;
        _pending_adds.insert(event.index);
    }

    auto device_path = _receiver->devicePath();
    try {
        addDevice(event);
        const std::lock_guard lock(_wait_mutex);
        _waiters.erase(event.index);
        _pending_adds.erase(event.index);
    } catch (DeviceNotReady& e) {
        if (tries == max_tries) {
            if (event.fromTimeoutCheck) {
                logPrintf(DEBUG, "Device %s:%d is not ready after %d tries;"
                                 " waiting for input.",
                          device_path.c_str(), event.index, max_tries);
                {
                    const std::lock_guard lock(_wait_mutex);
                    _pending_adds.erase(event.index);
                }
                waitForDevice(event.index);
            } else {
                logPrintf(WARN, "Failed to add device %s:%d after %d tries."
                                "Treating as failure.",
                          device_path.c_str(), event.index, max_tries);
            }
        } else {
            /* Do exponential backoff for 2^tries * backoff ms. */
            std::chrono::milliseconds wait((1 << tries) * ready_backoff);
            logPrintf(DEBUG, "Failed to add device %s:%d on try %d, backing off for %dms",
                      device_path.c_str(), event.index, tries + 1, wait.count());
            run_task_after([self_weak = _self, event, tries]() {
                if (auto self = self_weak.lock())
                    self->_addHandler(event, tries + 1);
            }, wait);
        }
    } catch (std::exception& e) {
        {
            const std::lock_guard lock(_wait_mutex);
            _pending_adds.erase(event.index);
        }
        logPrintf(ERROR, "Failed to add device %d to receiver on %s: %s",
                  event.index, device_path.c_str(), e.what());
    }
}

void ReceiverMonitor::_removeHandler(hidpp::DeviceIndex index) {
    try {
        removeDevice(index);
    } catch (std::exception& e) {
        logPrintf(ERROR, "Failed to remove device %d from receiver on %s: %s",
                  index, _receiver->devicePath().c_str(), e.what());
    }
}

void ReceiverMonitor::_enumeratePairedDevices() {
    uint8_t paired_devices = 0;
    try {
        paired_devices = _receiver->getDeviceCount();
    } catch (hidpp10::Error& e) {
        logPrintf(DEBUG, "Failed to get device count on %s: %s",
                  _receiver->devicePath().c_str(), e.what());
    } catch (TimeoutError& e) {
        logPrintf(DEBUG, "Timed out getting device count on %s",
                  _receiver->devicePath().c_str());
    }

    uint8_t found_devices = 0;
    for (uint8_t i = hidpp::WirelessDevice1; i <= hidpp::WirelessDevice6; i++) {
        if (paired_devices && found_devices >= paired_devices)
            break;

        auto index = static_cast<hidpp::DeviceIndex>(i);

        try {
            auto pair_info = _receiver->getPairingInfo(index);

            hidpp::DeviceConnectionEvent event{};
            event.index = index;
            event.pid = pair_info.pid;
            event.deviceType = pair_info.deviceType;
            event.linkEstablished = true;
            event.withPayload = false;
            event.fromTimeoutCheck = true;

            _addHandler(event);
            found_devices++;
        } catch (hidpp10::Error& e) {
            switch (e.code()) {
                case hidpp10::Error::UnknownDevice:
                case hidpp10::Error::InvalidAddress:
                case hidpp10::Error::InvalidValue:
                case hidpp10::Error::InvalidParameterValue:
                    break;
                default:
                    logPrintf(DEBUG, "Failed to enumerate receiver slot %d on %s: %s",
                              index, _receiver->devicePath().c_str(), e.what());
                    break;
            }
        } catch (TimeoutError& e) {
            logPrintf(DEBUG, "Timed out enumerating receiver slot %d on %s",
                      index, _receiver->devicePath().c_str());
        } catch (std::exception& e) {
            logPrintf(DEBUG, "Failed to enumerate receiver slot %d on %s: %s",
                      index, _receiver->devicePath().c_str(), e.what());
        }
    }
}
