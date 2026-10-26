// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LOGID_FEATURE_HOSTSWITCH_H
#define LOGID_FEATURE_HOSTSWITCH_H

#include <Device.h>
#include <backend/hidpp20/features/ChangeHost.h>
#include <features/DeviceFeature.h>

namespace logid::features {
    // D-Bus uses the same zero-based host indices as HID++.
    class HostSwitch : public DeviceFeature {
    public:
        void configure() final {}
        void listen() final;
        void setProfile(config::Profile&) final {}
        void onWakeup() final;

    protected:
        explicit HostSwitch(Device* device);

    private:
        class IPC : public ipcgull::interface {
        public:
            explicit IPC(HostSwitch* parent);
            std::tuple<uint8_t, uint8_t> getHostInfo() const;
            void setHost(uint8_t host);
            void switched(uint8_t previous, uint8_t next) const;
            void connected() const;
        private:
            HostSwitch& _parent;
        };

        std::shared_ptr<backend::hidpp20::ChangeHost> _change_host;
        std::shared_ptr<IPC> _ipc_interface;
        EventHandlerLock<backend::hidpp::Device> _event_handler;
    };
}
#endif
