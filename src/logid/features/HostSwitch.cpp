// SPDX-License-Identifier: GPL-3.0-or-later
#include <features/HostSwitch.h>
#include <ipc_defs.h>
#include <util/log.h>
#include <util/task.h>

using namespace logid::features;
using namespace logid::backend;

HostSwitch::HostSwitch(Device* device) : DeviceFeature(device) {
    try {
        _change_host = std::make_shared<hidpp20::ChangeHost>(&device->hidpp20());
    } catch (hidpp20::UnsupportedFeature&) {
        throw UnsupportedFeature();
    }
    logPrintf(INFO, "%s: CHANGE_HOST v%u", device->name().c_str(),
              _change_host->featureVersion());
    _ipc_interface = device->ipcNode()->make_interface<IPC>(this);
}

void HostSwitch::listen() {
    if (!_event_handler.empty())
        return;
    _event_handler = _device->hidpp20().addEventHandler({
        [index = _change_host->featureIndex()](const hidpp::Report& report) {
            // Event 0: previousHost, newHost. Replies have a nonzero software ID.
            return report.feature() == index && report.function() == 0 &&
                   report.swId() == 0 &&
                   std::distance(report.paramBegin(), report.paramEnd()) >= 2;
        },
        [weak = self<HostSwitch>()](const hidpp::Report& report) {
            const auto params = report.paramBegin();
            const uint8_t previous = params[0], next = params[1];
            if (previous == next)
                return;
            run_task([weak, previous, next] {
                if (auto self = weak.lock()) {
                    logPrintf(INFO, "%s: host switch %u -> %u",
                              self->_device->name().c_str(), previous + 1, next + 1);
                    self->_ipc_interface->switched(previous, next);
                }
            });
        }
    });
}

void HostSwitch::onWakeup() {
    // Also covers reconnection broadcasts when the receiver missed a link-down.
    _ipc_interface->connected();
}

HostSwitch::IPC::IPC(HostSwitch* parent) : ipcgull::interface(
    SERVICE_ROOT_NAME ".HostSwitch", {
        {"GetHostInfo", {this, &IPC::getHostInfo, {}, {"hostCount", "currentHost"}}},
        {"SetHost", {this, &IPC::setHost, {"host"}}}
    }, {
        {"Version", ipcgull::property<uint8_t>(ipcgull::property_readable,
                                               parent->_change_host->featureVersion())}
    }, {
        {"Switched", ipcgull::signal::make_signal<uint8_t, uint8_t>({"previous", "next"})},
        {"Connected", ipcgull::signal::make_signal<>({})}
    }), _parent(*parent) {}

std::tuple<uint8_t, uint8_t> HostSwitch::IPC::getHostInfo() const {
    const auto info = _parent._change_host->getHostInfo();
    return {info.hostCount, info.currentHost};
}

void HostSwitch::IPC::setHost(uint8_t host) {
    // Refresh the host count and verify that the device is still reachable.
    _parent._change_host->getHostInfo();
    _parent._change_host->setHost(host);
}

void HostSwitch::IPC::switched(uint8_t previous, uint8_t next) const {
    emit_signal("Switched", previous, next);
}

void HostSwitch::IPC::connected() const {
    emit_signal("Connected");
}
