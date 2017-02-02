/*
 * Copyright (C) 2013-2016 Trent Houliston <trent@houliston.me>, Jake Woods <jake.f.woods@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "nuclear_bits/extension/NetworkController.hpp"
#include "nuclear_bits/extension/network/wire_protocol.hpp"
#include "nuclear_bits/util/platform.hpp"

#include <algorithm>
#include <cerrno>

namespace NUClear {
namespace extension {

    using dsl::word::emit::NetworkEmit;

    void NetworkController::udp_send(const NetworkEmit& emit) {

        // Make our message struct
        msghdr message;
        std::memset(&message, 0, sizeof(msghdr));

        // Create our iovec
        iovec data[2];
        message.msg_iov    = data;
        message.msg_iovlen = 2;

        // The first element in our iovec is the header
        network::DataPacket header;
        data[0].iov_base = reinterpret_cast<char*>(&header);
        data[0].iov_len  = sizeof(network::DataPacket) - 1;

        // Some references for easy access to our data memory
        auto& base = data[1].iov_base;
        auto& len  = data[1].iov_len;

        // Set some common information for the header
        header.type         = network::DATA;
        header.packet_id    = ++packet_id_source;
        header.packet_no    = 0;
        header.packet_count = uint16_t((emit.payload.size() / MAX_UDP_PAYLOAD_LENGTH) + 1);
        header.multicast    = emit.target.empty();
        header.hash         = emit.hash;

        // Loop through our chunks
        for (size_t i = 0; i < emit.payload.size(); i += MAX_UDP_PAYLOAD_LENGTH) {

            // Store our payload information for this chunk
            base = const_cast<char*>(emit.payload.data() + i);
            len  = (i + MAX_UDP_PAYLOAD_LENGTH) < emit.payload.size() ? MAX_UDP_PAYLOAD_LENGTH
                                                                     : emit.payload.size() % MAX_UDP_PAYLOAD_LENGTH;

            // Work out our header length
            header.length = uint32_t(len + sizeof(network::DataPacket) - sizeof(network::PacketHeader) - 1);

            // Send multicast
            if (emit.target.empty()) {

                // Multicast address
                sockaddr_in target;
                std::memset(&target, 0, sizeof(sockaddr_in));
                target.sin_family = AF_INET;
                inet_pton(AF_INET, multicast_group.c_str(), &target.sin_addr);
                target.sin_port = htons(multicast_port);

                message.msg_name    = reinterpret_cast<sockaddr*>(&target);
                message.msg_namelen = sizeof(sockaddr_in);

                // Send the packet
                sendmsg(udp_server_fd, &message, 0);
            }
            // Send unicast
            else {
                auto send_to = name_target.equal_range(emit.target);

                for (auto it = send_to.first; it != send_to.second; ++it) {

                    // Unicast address
                    sockaddr_in target;
                    std::memset(&target, 0, sizeof(sockaddr_in));
                    target.sin_family      = AF_INET;
                    target.sin_addr.s_addr = htonl(it->second->address);
                    target.sin_port        = htons(it->second->udp_port);

                    message.msg_name    = reinterpret_cast<sockaddr*>(&target);
                    message.msg_namelen = sizeof(sockaddr_in);

                    // Send the packet
                    sendmsg(udp_server_fd, &message, 0);
                }
            }

            // Increment to send the next packet
            ++header.packet_no;
        }
    }
}
}