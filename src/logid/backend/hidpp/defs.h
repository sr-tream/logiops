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

#ifndef LOGID_BACKEND_HIDPP_DEFS_H
#define LOGID_BACKEND_HIDPP_DEFS_H

#include <cstdint>

namespace logid::backend::hidpp {
    static constexpr uint16_t logitechVendorID = 0x046d;

    namespace ReportType {
        enum ReportType : uint8_t {
            Short = 0x10,
            Long = 0x11
        };
    }

    enum DeviceIndex : uint8_t {
        DefaultDevice = 0xff,
        CordedDevice = 0,
        WirelessDevice1 = 1,
        WirelessDevice2 [[maybe_unused]] = 2,
        WirelessDevice3 [[maybe_unused]] = 3,
        WirelessDevice4 [[maybe_unused]] = 4,
        WirelessDevice5 [[maybe_unused]] = 5,
        WirelessDevice6 = 6,
    };

    enum DeviceType : uint8_t {
        DeviceUnknown = 0x00,
        DeviceKeyboard = 0x01,
        DeviceMouse = 0x02,
        DeviceNumpad = 0x03,
        DevicePresenter = 0x04,
        /* 0x05-0x07 is reserved */
        DeviceTrackball = 0x08,
        DeviceTouchpad = 0x09
    };

    static constexpr uint8_t softwareID = 2;
    /* For sending reports with no response, use a different SW ID */
    static constexpr uint8_t noAckSoftwareID = 3;

    static constexpr std::size_t ShortParamLength = 3;
    static constexpr std::size_t LongParamLength = 16;
}

#endif //LOGID_BACKEND_HIDPP_DEFS_H
