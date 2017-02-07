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

#include "nuclear_bits/extension/network/NUClearNetwork.hpp"

#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <algorithm>
#include <cerrno>

#warning "This drop percentage needs to be removed"
#define DROP_PERCENT 0

namespace NUClear {
namespace extension {
    namespace network {

        size_t socket_size(const util::network::sock_t& s) {
            switch (s.sock.sa_family) {
                case AF_INET: return sizeof(sockaddr_in);
                case AF_INET6: return sizeof(sockaddr_in6);
                default: return -1;
            }
        }

        NUClearNetwork::NUClearNetwork()
            : multicast_target()
            , unicast_fd(-1)
            , multicast_fd(-1)
            , announce_packet()
            , packet_id_source(0)
            , packet_callback()
            , join_callback()
            , leave_callback()
            , targets()
            , name_target()
            , udp_target() {}


        NUClearNetwork::~NUClearNetwork() {
            shutdown();
        }

        void NUClearNetwork::set_packet_callback(
            std::function<void(const NetworkTarget&, const std::array<uint64_t, 2>&, std::vector<char>&&)> f) {
            packet_callback = f;
        }


        void NUClearNetwork::set_join_callback(std::function<void(const NetworkTarget&)> f) {
            join_callback = f;
        }


        void NUClearNetwork::set_leave_callback(std::function<void(const NetworkTarget&)> f) {
            leave_callback = f;
        }

        std::array<uint16_t, 9> NUClearNetwork::udp_key(const sock_t& address) {

            // Get our keys for our maps, it will be the ip and then port
            std::array<uint16_t, 9> key;
            key.fill(0);

            switch (address.sock.sa_family) {
                case AF_INET:
                    // The first chars are 0 (ipv6) and after that is our address and then port
                    std::memcpy(&key[6], &address.ipv4.sin_addr, sizeof(address.ipv4.sin_addr));
                    key[8] = address.ipv4.sin_port;
                    break;

                case AF_INET6:
                    // IPv6 address then port
                    std::memcpy(key.data(), &address.ipv6.sin6_addr, sizeof(address.ipv6.sin6_addr));
                    key[8] = address.ipv6.sin6_port;
                    break;
            }

            return key;
        }


        void NUClearNetwork::remove_target(const std::shared_ptr<NetworkTarget>& target) {

            // Lock our mutex
            std::lock_guard<std::mutex> lock(target_mutex);
            
            // Erase udp
            auto key = udp_key(target->target);
            if (udp_target.find(key) != udp_target.end()) {
                udp_target.erase(udp_target.find(key));
            }

            // Erase name
            auto range = name_target.equal_range(target->name);
            for (auto it = range.first; it != range.second; ++it) {
                if (it->second == target) {
                    name_target.erase(it);
                    break;
                }
            }

            // Erase target
            auto t = std::find(targets.begin(), targets.end(), target);
            if (t != targets.end()) {
                targets.erase(t);
            }
        }


        void NUClearNetwork::open_unicast() {

            // Create the "join any" address for this address family
            sock_t address = multicast_target;

            // IPv4
            if (address.sock.sa_family == AF_INET) {
                address.ipv4.sin_family      = AF_INET;
                address.ipv4.sin_addr.s_addr = htonl(INADDR_ANY);
                address.ipv4.sin_port        = 0;
            }
            // IPv6
            else if (address.sock.sa_family == AF_INET6) {
                address.ipv6.sin6_family = AF_INET6;
                address.ipv6.sin6_addr   = IN6ADDR_ANY_INIT;
                address.ipv6.sin6_port   = 0;
            }

            // Open a socket with the same family as our multicast target
            unicast_fd = ::socket(address.sock.sa_family, SOCK_DGRAM, IPPROTO_UDP);
            if (unicast_fd < 0) {
                throw std::system_error(network_errno, std::system_category(), "Unable to open the UDP socket");
            }

            // Bind to the address, and if we fail throw an error
            if (::bind(unicast_fd, &address.sock, socket_size(address))) {
                throw std::system_error(
                    network_errno, std::system_category(), "Unable to bind the UDP socket to the port");
            }
        }


        void NUClearNetwork::open_multicast() {

            // Rather than listen on the multicast address directly, we join everything so
            // our traffic isn't filtered allowing us to get multicast traffic from multiple devices
            sock_t address = multicast_target;

            // IPv4
            if (address.sock.sa_family == AF_INET) {
                address.ipv4.sin_addr.s_addr = htonl(INADDR_ANY);
            }
            // IPv6
            else if (address.sock.sa_family == AF_INET6) {
                address.ipv6.sin6_addr = IN6ADDR_ANY_INIT;
            }

            // Make our socket
            multicast_fd = ::socket(address.sock.sa_family, SOCK_DGRAM, IPPROTO_UDP);
            if (multicast_fd < 0) {
                throw std::system_error(network_errno, std::system_category(), "Unable to open the UDP socket");
            }

            // Set that we reuse the address so more than one application can bind
            int yes = true;
            if (::setsockopt(multicast_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&yes), sizeof(yes)) < 0) {
                throw std::system_error(network_errno, std::system_category(), "Unable to reuse address on the socket");
            }

// If SO_REUSEPORT is available set it too
#ifdef SO_REUSEPORT
            if (::setsockopt(multicast_fd, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<char*>(&yes), sizeof(yes)) < 0) {
                throw std::system_error(network_errno, std::system_category(), "Unable to reuse port on the socket");
            }
#endif

            // Bind to the address
            if (::bind(multicast_fd, &address.sock, socket_size(address))) {
                throw std::system_error(network_errno, std::system_category(), "Unable to bind the UDP socket");
            }

            // Our multicast join request will depend on protocol version
            if (multicast_target.sock.sa_family == AF_INET) {

                // Set the multicast address we are listening on
                ip_mreq mreq;
                mreq.imr_multiaddr = multicast_target.ipv4.sin_addr;

                // Join the multicast group on all the interfaces that support it
                for (auto& iface : util::network::get_interfaces()) {

                    if (iface.ip.sock.sa_family == AF_INET && iface.flags.multicast) {

                        // Set our interface address
                        mreq.imr_interface = iface.ip.ipv4.sin_addr;

                        // Join our multicast group
                        if (::setsockopt(multicast_fd,
                                         IPPROTO_IP,
                                         IP_ADD_MEMBERSHIP,
                                         reinterpret_cast<char*>(&mreq),
                                         sizeof(ip_mreq))
                            < 0) {
                            throw std::system_error(network_errno,
                                                    std::system_category(),
                                                    "There was an error while attempting to join the multicast group");
                        }
                    }
                }
            }
            else if (multicast_target.sock.sa_family == AF_INET6) {

                // Set the multicast address we are listening on
                ipv6_mreq mreq;
                mreq.ipv6mr_multiaddr = multicast_target.ipv6.sin6_addr;

                std::set<unsigned int> added_interfaces;

                // Join the multicast group on all the interfaces that support it
                for (auto& iface : util::network::get_interfaces()) {

                    if (iface.ip.sock.sa_family == AF_INET6 && iface.flags.multicast) {

                        // Get the interface for this
                        mreq.ipv6mr_interface = if_nametoindex(iface.name.c_str());

                        // Only add each interface index once
                        if (added_interfaces.find(mreq.ipv6mr_interface) == added_interfaces.end()) {
                            added_interfaces.insert(mreq.ipv6mr_interface);

                            // Join our multicast group
                            if (::setsockopt(multicast_fd,
                                             IPPROTO_IPV6,
                                             IPV6_JOIN_GROUP,
                                             reinterpret_cast<char*>(&mreq),
                                             sizeof(ipv6_mreq))
                                < 0) {
                                throw std::system_error(
                                    network_errno,
                                    std::system_category(),
                                    "There was an error while attempting to join the multicast group");
                            }
                        }
                    }
                }
            }
        }


        void NUClearNetwork::shutdown() {

            // If we have an fd, send a shutdown message
            if (unicast_fd > 0) {
                // Make a leave packet from our announce packet
                LeavePacket packet;
                packet.type = LEAVE;

                // Send the packet
                ::sendto(unicast_fd, &packet, sizeof(packet), 0, &multicast_target.sock, socket_size(multicast_target));
            }

            // Close our existing FDs if they exist
            if (unicast_fd > 0) {
                ::close(unicast_fd);
                unicast_fd = -1;
            }
            if (multicast_fd > 0) {
                ::close(multicast_fd);
                multicast_fd = -1;
            }
        }


        void NUClearNetwork::reset(std::string name, std::string group, in_port_t port, uint16_t network_mtu) {

            // Close our existing FDs if they exist
            shutdown();
            if (unicast_fd > 0) {
                ::close(unicast_fd);
                unicast_fd = -1;
            }
            if (multicast_fd > 0) {
                ::close(multicast_fd);
                multicast_fd = -1;
            }

            // Setup some hints for what our address is
            addrinfo hints;
            memset(&hints, 0, sizeof hints);  // make sure the struct is empty
            hints.ai_family   = AF_UNSPEC;    // don't care about IPv4 or IPv6
            hints.ai_socktype = SOCK_DGRAM;   // using udp datagrams

            // Get our info on this address
            addrinfo* servinfo = nullptr;
            ::getaddrinfo(group.c_str(), std::to_string(port).c_str(), &hints, &servinfo);

            // Check if we have any addresses to work with
            if (servinfo == nullptr) {
                throw std::runtime_error(std::string("The multicast address provided (") + group + ") was invalid");
            }

            // Clear our struct
            std::memset(&multicast_target, 0, sizeof(multicast_target));

            // The list is actually a linked list of valid addresses
            // The address we choose is in the following priority, IPv4, IPv6, Other
            for (addrinfo* p = servinfo; p != nullptr; p = p->ai_next) {

                // If we find an IPv4 address, prefer that
                if (servinfo->ai_family == AF_INET) {
                    auto& addr = *reinterpret_cast<const sockaddr_in*>(servinfo->ai_addr);

                    // Check this address is multicast in the Administratively-scoped group
                    // (starts with 1110 1111)
                    if ((htonl(addr.sin_addr.s_addr) & 0xEF000000) == 0xEF000000) {

                        // Clear and set our struct
                        std::memset(&multicast_target, 0, sizeof(multicast_target));
                        std::memcpy(&multicast_target, servinfo->ai_addr, servinfo->ai_addrlen);

                        // We prefer IPv4 so use it and stop looking
                        break;
                    }
                }
                // If we find an IPv6 address now we just use that
                else if (servinfo->ai_family == AF_INET6) {
                    auto& addr = *reinterpret_cast<const sockaddr_in6*>(servinfo->ai_addr);

                    // Check the address is multicast (starts with 0xFF)
                    if (addr.sin6_addr.s6_addr[0] == 0xFF) {

                        // Clear and set our struct
                        std::memset(&multicast_target, 0, sizeof(multicast_target));
                        std::memcpy(&multicast_target, servinfo->ai_addr, servinfo->ai_addrlen);
                    }
                }
            }

            ::freeaddrinfo(servinfo);

            // If we couldn't find a useable address die
            if (multicast_target.sock.sa_family != AF_INET && multicast_target.sock.sa_family != AF_INET6) {
                throw std::runtime_error(std::string("The network address provided (") + group
                                         + ") was not a valid multicast address");
            }

            // Work out our MTU for udp packets
            packet_data_mtu = network_mtu;              // Start with the total mtu
            packet_data_mtu -= sizeof(DataPacket) - 1;  // Now remove data packet header size
            packet_data_mtu -= 40;                      // Remove size of an IPv4 header or IPv6 header
            // IPv6 headers are always 40 bytes, and IPv4 can be 20-60
            // but if we assume 40 for all cases it should be safe enough
            packet_data_mtu -= 8;  // Size of a UDP packet header

            // Build our announce packet
            announce_packet.resize(sizeof(AnnouncePacket) + name.size(), 0);
            AnnouncePacket& pkt = *reinterpret_cast<AnnouncePacket*>(announce_packet.data());
            pkt                 = AnnouncePacket();
            pkt.type            = ANNOUNCE;
            std::memcpy(&pkt.name, name.c_str(), name.size());

            // Open our unicast socket and then our multicast one
            open_unicast();
            open_multicast();
        }


        std::pair<util::network::sock_t, std::vector<char>> NUClearNetwork::read_socket(int fd) {

            // Allocate a vector that can hold a datagram
            std::vector<char> payload(1500);
            iovec iov;
            iov.iov_base = payload.data();
            iov.iov_len  = payload.size();

            // Who we are receiving from
            sock_t from;
            std::memset(&from, 0, sizeof(from));

            // Setup our message header to receive
            msghdr mh;
            memset(&mh, 0, sizeof(msghdr));
            mh.msg_name    = &from;
            mh.msg_namelen = sizeof(from);
            mh.msg_iov     = &iov;
            mh.msg_iovlen  = 1;

            // Now read the data for real
            ssize_t received = recvmsg(fd, &mh, 0);
            payload.resize(received);

            return std::make_pair(from, std::move(payload));
        }


        void NUClearNetwork::process() {

            // Check if we have a packet available on the multicast socket
            int count = 0;
            ioctl(multicast_fd, FIONREAD, &count);

            if (count > 0) {
                auto packet = read_socket(multicast_fd);
                process_packet(std::move(packet.first), std::move(packet.second));
            }

            // Check if we have a packet available on the unicast socket
            count = 0;
            ioctl(unicast_fd, FIONREAD, &count);

            if (count > 0) {
                auto packet = read_socket(unicast_fd);
                process_packet(std::move(packet.first), std::move(packet.second));
            }
        }


        void NUClearNetwork::announce() {

            // Check if any of our existing connections have timed out
            /* Mutex scope */ {
                std::lock_guard<std::mutex> lock(target_mutex);
                auto now = std::chrono::steady_clock::now();
                for (auto it = targets.begin(); it != targets.end();) {

                    auto ptr = *it;
                    ++it;

                    if (now - ptr->last_update > std::chrono::seconds(2)) {

                        // Remove this, it timed out
                        leave_callback(*ptr);
                        remove_target(ptr);
                    }
                }

                for (auto qit = send_queue.begin(); qit != send_queue.end();) {
                    for (auto it = qit->second.targets.begin(); it != qit->second.targets.end();) {

                        // Get the pointer to our target
                        auto ptr = it->target.lock();

                        // If our pointer is valid (they haven't disconnected)
                        if (ptr) {

                            // Check if we should have expected an ack by now for some packets
                            if (it->last_send + (2 * ptr->round_trip_time) < std::chrono::steady_clock::now()) {
                                
                                // Resend this packet via unicast
                                msghdr message;
                                std::memset(&message, 0, sizeof(msghdr));
                                
                                iovec data[2];
                                message.msg_iov    = data;
                                message.msg_iovlen = 2;
                                
                                // The first element in our iovec is the header
                                DataPacket header = qit->second.header;
                                data[0].iov_base = reinterpret_cast<char*>(&header);
                                data[0].iov_len  = sizeof(DataPacket) - 1;
                                
                                // Some references for easy access to our data memory
                                auto& base = data[1].iov_base;
                                auto& len  = data[1].iov_len;
                                
                                
                                // Work out which packets to resend
                                for (int i = 0; i < qit->second.header.packet_count; ++i) {
                                    if((it->acked[i / 8] & uint8_t(1 << (i % 8))) == 0) {
                                        
                                        // Fill in our packet data
                                        header.packet_no = i;
                                        
                                        base = qit->second.payload.data() + packet_data_mtu * i;
                                        len = i + 1 < header.packet_count ? packet_data_mtu : qit->second.payload.size() % packet_data_mtu;
                                        
                                        message.msg_name    = &ptr->target;
                                        message.msg_namelen = socket_size(ptr->target);
                                        
                                        ::sendmsg(unicast_fd, &message, 0);
                                    }
                                }
                            }

                            ++it;
                        }
                        // Remove them from the list
                        else {
                            it = qit->second.targets.erase(it);
                        }
                    }

                    if (qit->second.targets.empty()) {
                        qit = send_queue.erase(qit);
                    }
                    else {
                        ++qit;
                    }
                }
            }

            // Send the packet
            if (::sendto(unicast_fd,
                         announce_packet.data(),
                         announce_packet.size(),
                         0,
                         &multicast_target.sock,
                         socket_size(multicast_target))
                < 0) {
                throw std::system_error(
                    network_errno, std::system_category(), "Network error when sending the announce packet");
            }
        }


        void NUClearNetwork::process_packet(sock_t&& address, std::vector<char>&& payload) {

            // First validate this is a NUClear network packet we can read (a version 2 NUClear packet)
            if (payload[0] == '\xE2' && payload[1] == '\x98' && payload[2] == '\xA2' && payload[3] == 0x02) {

                // This is a real packet! get our header information
                const PacketHeader& header = *reinterpret_cast<const PacketHeader*>(payload.data());

                // Get the map key for this device
                auto key = udp_key(address);

                // From here on, we are doing things with our target lists that if changed would make us sad
                std::shared_ptr<NetworkTarget> remote;
                /* Mutex scope */ {
                    std::lock_guard<std::mutex> lock(target_mutex);
                    auto r = udp_target.find(key);
                    remote = r == udp_target.end() ? nullptr : r->second;
                }

                switch (header.type) {

                    // A packet announcing that a user is on the network
                    case ANNOUNCE: {
                        // This is an announce packet!
                        const AnnouncePacket& announce = *reinterpret_cast<const AnnouncePacket*>(payload.data());

                        // They're new!
                        if (!remote) {
                            std::string name(&announce.name, payload.size() - sizeof(AnnouncePacket));

                            // Add them into our list
                            auto ptr = std::make_shared<NetworkTarget>(name, std::move(address));
                            
                            /* Mutex scope */ {
                                std::lock_guard<std::mutex> lock(target_mutex);
                            targets.push_front(ptr);
                            udp_target.insert(std::make_pair(key, ptr));
                            name_target.insert(std::make_pair(name, ptr));
                            }

                            // Say hi back!
                            ::sendto(unicast_fd,
                                     announce_packet.data(),
                                     announce_packet.size(),
                                     0,
                                     &ptr->target.sock,
                                     socket_size(ptr->target));

                            join_callback(*ptr);
                        }
                        // They're old but at least they're not timing out
                        else {
                            remote->last_update = std::chrono::steady_clock::now();
                        }
                    } break;
                    case LEAVE: {

                        // Goodbye!
                        if (remote) {

                            // Remove from our list
                            remove_target(remote);
                            leave_callback(*remote);
                        }

                    } break;

                    // A packet containing data
                    case DATA: {

                        // It's a data packet
                        const DataPacket& packet = *reinterpret_cast<const DataPacket*>(payload.data());

                        // If the packet is obviously corrupt, drop it and since we didn't ack it it'll be resent if
                        // it's important
                        if (packet.packet_no > packet.packet_count) {
                            return;
                        }

                        // Check if we know who this is and if we don't know them, ignore
                        if (remote) {

                            // We got a packet from them recently
                            remote->last_update = std::chrono::steady_clock::now();

                            // If this is a solo packet (in a single chunk)
                            if (packet.packet_count == 1) {

                                // Copy our data into a vector
                                std::vector<char> out(&packet.data,
                                                      &packet.data + payload.size() - sizeof(DataPacket) + 1);

                                // If this is a reliable packet, send an ack back
                                if (packet.reliable) {
                                    // This response is easy since there is only one packet
                                    ACKPacket response;
                                    response.type         = ACK;
                                    response.packet_id    = packet.packet_id;
                                    response.packet_no    = packet.packet_no;
                                    response.packet_count = packet.packet_count;
                                    response.packets      = 1;

                                    // Make who we are sending it to into a useable address
                                    sock_t& to = remote->target;

                                    if ((rand() % 100) >= DROP_PERCENT)  // TODO TEMP 10% packet loss
                                        ::sendto(unicast_fd, &response, sizeof(response), 0, &to.sock, socket_size(to));
                                }

                                packet_callback(*remote, packet.hash, std::move(out));
                            }
                            else {
                                std::lock_guard<std::mutex> lock(remote->assemblers_mutex);

                                // Grab the payload and put it in our list of assemblers targets
                                auto& assemblers = remote->assemblers;
                                auto& assembler  = assemblers[packet.packet_id];

                                // First check that our cache isn't super corrupted by ensuring that our last packet in
                                // our list isn't after the number of packets we have
                                if (!assembler.second.empty()
                                    && std::next(assembler.second.end(), -1)->first >= packet.packet_count) {

                                    // If so, we need to purge our cache and if this was a reliable packet, send a NACK
                                    // back for all the packets we thought we had
                                    // We don't know if we have any packets except the one we just got
                                    if (packet.reliable) {

                                        // A basic ack has room for 8 packets and we need 1 extra byte for each 8
                                        // additional
                                        // packets
                                        std::vector<char> r(sizeof(NACKPacket) + (packet.packet_count / 8), 0);
                                        NACKPacket& response  = *reinterpret_cast<NACKPacket*>(r.data());
                                        response              = NACKPacket();
                                        response.type         = NACK;
                                        response.packet_id    = packet.packet_id;
                                        response.packet_count = packet.packet_count;

                                        // Set the bits for the packets we thought we received
                                        for (auto& p : assembler.second) {
                                            (&response.packets)[p.first / 8] |= uint8_t(1 << (p.first % 8));
                                        }

                                        // Ensure the bit for this packet isn't NACKed
                                        (&response.packets)[packet.packet_no / 8] &=
                                            ~uint8_t(1 << (packet.packet_no % 8));

                                        // Make who we are sending it to into a useable address
                                        sock_t& to = remote->target;

                                        // Send the packet
                                        if ((rand() % 100) >= DROP_PERCENT)  // TODO TEMP 10% packet loss
                                            ::sendto(unicast_fd, r.data(), r.size(), 0, &to.sock, socket_size(to));
                                    }

                                    assembler.second.clear();
                                }

                                assembler.first                    = std::chrono::steady_clock::now();
                                assembler.second[packet.packet_no] = std::move(payload);

                                // Create and send our ACK packet if this is a reliable transmission
                                if (packet.reliable) {
                                    // A basic ack has room for 8 packets and we need 1 extra byte for each 8 additional
                                    // packets
                                    std::vector<char> r(sizeof(ACKPacket) + (packet.packet_count / 8), 0);
                                    ACKPacket& response   = *reinterpret_cast<ACKPacket*>(r.data());
                                    response              = ACKPacket();
                                    response.type         = ACK;
                                    response.packet_id    = packet.packet_id;
                                    response.packet_no    = packet.packet_no;
                                    response.packet_count = packet.packet_count;

                                    // Set the bits for the packets we have received
                                    for (auto& p : assembler.second) {
                                        (&response.packets)[p.first / 8] |= uint8_t(1 << (p.first % 8));
                                    }

                                    // Make who we are sending it to into a useable address
                                    sock_t& to = remote->target;

                                    // Send the packet
                                    if ((rand() % 100) >= DROP_PERCENT)  // TODO TEMP 10% packet loss
                                        ::sendto(unicast_fd, r.data(), r.size(), 0, &to.sock, socket_size(to));
                                }

                                // Check to see if we have enough to assemble the whole thing
                                if (assembler.second.size() == packet.packet_count) {

                                    // Work out exactly how much data we will need first so we only need one allocation
                                    size_t payload_size = 0;
                                    int packet_index    = 0;
                                    for (auto& packet : assembler.second) {
                                        if (packet.first != packet_index++) {
                                            // TODO this data was bad, erase it or something
                                            return;
                                        }
                                        else {
                                            payload_size += packet.second.size() - sizeof(DataPacket) + 1;
                                        }
                                    }

                                    // Read in our data
                                    std::vector<char> out;
                                    out.reserve(payload_size);
                                    for (auto& packet : assembler.second) {
                                        const DataPacket& p = *reinterpret_cast<DataPacket*>(packet.second.data());
                                        out.insert(out.end(),
                                                   &p.data,
                                                   &p.data + packet.second.size() - sizeof(DataPacket) + 1);
                                    }

                                    // Send our assembled data packet
                                    packet_callback(*remote, packet.hash, std::move(out));

                                    // We have completed this packet, discard the data
                                    assemblers.erase(assemblers.find(packet.packet_id));
                                }
                            }
                        }
                    } break;

                    // Packet acknowledging the receipt of a packet of data
                    case ACK: {

                        // It's an ack packet
                        const ACKPacket& packet = *reinterpret_cast<const ACKPacket*>(payload.data());

                        // Check if we know who this is and if we don't know them, ignore
                        if (remote) {

                            // We got a packet from them recently
                            remote->last_update = std::chrono::steady_clock::now();

                            // Check for our packet id in the send queue
                            if (send_queue.count(packet.packet_id) > 0) {
                                // TODO LOCK A MUTEX!!
                                
                                auto& queue = send_queue[packet.packet_id];

                                // Find this target in the send queue
                                auto s = std::find_if(queue.targets.begin(),
                                                      queue.targets.end(),
                                                      [&](const PacketQueue::PacketTarget& target) {
                                                          return target.target.lock() == remote;
                                                      });

                                // We know who this is
                                if (s != queue.targets.end()) {

                                    // Now we need to validate that the ack is relevant and valid
                                    if (packet.packet_count != queue.header.packet_count
                                        || payload.size() < (sizeof(ACKPacket) + (queue.header.packet_count / 8))) {

                                        // TODO this is an invalid ack we should handle it somehow!
                                    }
                                    else {
                                        // Work out about how long our round trip time is
                                        auto now        = std::chrono::steady_clock::now();
                                        auto round_trip = now - s->last_send;

                                        // Approximate how long the round trip is to this remote so we can work out how
                                        // long before retransmitting
                                        // We use a baby kalman filter to help smooth out jitter
                                        remote->measure_round_trip(round_trip);

                                        // Update our acks
                                        bool all_acked = true;
                                        for (unsigned i = 0; i < s->acked.size(); ++i) {

                                            // Update our bitset
                                            s->acked[i] |= (&packet.packets)[i];

                                            // Work out what a "fully acked" packet would look like
                                            uint8_t expected = i + 1 < s->acked.size() || packet.packet_count % 8 == 0
                                                                   ? 0xFF
                                                                   : 0xFF >> (8 - (packet.packet_count % 8));

                                            all_acked &= (s->acked[i] & expected) == expected;
                                        }

                                        // The remote has received this entire packet we can erase our one
                                        if (all_acked) {
                                            queue.targets.erase(s);

                                            // If we're all done remove the whole thing
                                            if (queue.targets.empty()) {
                                                send_queue.erase(packet.packet_id);
                                            }
                                        }
                                    }
                                }
                                // Someone else is ACKing that we didn't expect
                                else {
                                    // TODO should we add and start sending to them?
                                }
                            }
                        }
                    } break;

                    // Packet requesting a retransmission of some corrupt data
                    case NACK: {
                        // It's a nack packet
                        const NACKPacket& packet = *reinterpret_cast<const NACKPacket*>(payload.data());

                        // Check if we know who this is and if we don't know them, ignore
                        if (remote) {

                            // We got a packet from them recently
                            remote->last_update = std::chrono::steady_clock::now();

                            // Find this packet in our sending queue

                            // Check for our packet id in the send queue
                            if (send_queue.count(packet.packet_id) > 0) {

                                auto& queue = send_queue[packet.packet_id];

                                // Find this target in the send queue
                                auto s = std::find_if(queue.targets.begin(),
                                                      queue.targets.end(),
                                                      [&](const PacketQueue::PacketTarget& target) {
                                                          return target.target.lock() == remote;
                                                      });

                                // We know who this is
                                if (s != queue.targets.end()) {

                                    // Now we need to validate that the ack is relevant and valid
                                    if (packet.packet_count != queue.header.packet_count
                                        || payload.size() < (sizeof(NACKPacket) + (queue.header.packet_count / 8))) {

                                        // TODO this is an invalid nack we should handle it somehow!
                                    }
                                    else {
                                        // Store the time as we are now sending new packets
                                        s->last_send = std::chrono::steady_clock::now();

                                        // Update our acks with the nacked data
                                        for (unsigned i = 0; i < s->acked.size(); ++i) {

                                            // Update our bitset
                                            s->acked[i] &= ~(&packet.packets)[i];
                                        }

                                        // Now we have to retransmit the nacked packets
                                        for (int i = 0; i < packet.packet_count * 8; ++i) {

                                            // Check if this packet needs to be sent
                                            uint8_t bit = 1 << (i % 8);
                                            if (((&packet.packets)[i] & bit) == bit) {

                                                // TODO resend this packet
                                            }
                                        }
                                    }
                                }
                                // Someone else is NACKing that we didn't expect
                                else {
                                    // TODO should we add and start sending to them?
                                }

                                // Resend the packets that are NACKed
                                std::cout << remote->name << " NACK Packet: " << packet.packet_id << " ";
                                for (int i = 0; i < packet.packet_count; ++i) {
                                    std::cout << (((&packet.packets)[i / 8] & uint8_t(1 << (i % 8))) != 0);
                                }
                                std::cout << std::endl;
                            }


                            // Lookup our send queue for this packet id

                            // TODO if this packet is not in the list should we send back a packet to say it's never
                            // coming?

                            // Lookup this client as the recipient

                            // NAND in the packets that have been received

                            // Explicitly resend all of the packets that were in the NACK
                        }
                    }
                }
            }
        }


        std::vector<int> NUClearNetwork::listen_fds() {
            return std::vector<int>({unicast_fd, multicast_fd});
        }


        void NUClearNetwork::send(const std::array<uint64_t, 2>& hash,
                                  const std::vector<char>& payload,
                                  const std::string& target,
                                  bool reliable) {

            // Our packet we are sending
            msghdr message;
            std::memset(&message, 0, sizeof(msghdr));

            iovec data[2];
            message.msg_iov    = data;
            message.msg_iovlen = 2;

            // The first element in our iovec is the header
            DataPacket header;
            data[0].iov_base = reinterpret_cast<char*>(&header);
            data[0].iov_len  = sizeof(DataPacket) - 1;

            // Some references for easy access to our data memory
            auto& base = data[1].iov_base;
            auto& len  = data[1].iov_len;

            // Set some common information for the header
            header.type = DATA;
            // For the packet id we ensure that it's not currently used for retransmission
            while (send_queue.count(header.packet_id = ++packet_id_source) > 0)
                ;
            header.packet_no    = 0;
            header.packet_count = uint16_t((payload.size() / packet_data_mtu) + 1);
            header.reliable     = reliable;
            header.hash         = hash;

            // If this was a reliable packet we need to cache it in case it needs to be resent
            if (reliable) {

                std::lock_guard<std::mutex> lock(target_mutex);

                auto& queue = send_queue[header.packet_id];

                queue.header  = header;
                queue.payload = std::move(payload);
                std::vector<uint8_t> acks((header.packet_count / 8) + 1, 0);

                // Find interested parties or if multicast it's everyone we are connected to
                auto range = target.empty() ? std::make_pair(name_target.begin(), name_target.end())
                                            : name_target.equal_range(target);
                for (auto it = range.first; it != range.second; ++it) {

                    // Add this guy to the queue
                    PacketQueue::PacketTarget target;
                    target.last_send = std::chrono::steady_clock::now();
                    target.acked     = acks;
                    target.target    = it->second;
                    queue.targets.push_back(target);
                }
            }

            // Loop through our chunks
            for (size_t i = 0; i < payload.size(); i += packet_data_mtu) {

                // Store our payload information for this chunk
                base = const_cast<char*>(payload.data() + i);
                len  = (i + packet_data_mtu) < payload.size() ? packet_data_mtu : payload.size() % packet_data_mtu;

                // Send multicast
                if (target.empty()) {
                    message.msg_name    = &multicast_target;
                    message.msg_namelen = socket_size(multicast_target);

                    // Send the packet
                    if ((rand() % 100) >= DROP_PERCENT)  // TODO TEMP 10% packet loss
                        ::sendmsg(unicast_fd, &message, 0);
                }
                // Send unicast
                else {
                    std::lock_guard<std::mutex> lock(target_mutex);
                    auto send_to = name_target.equal_range(target);

                    for (auto it = send_to.first; it != send_to.second; ++it) {

                        message.msg_name    = &it->second->target;
                        message.msg_namelen = socket_size(it->second->target);

                        // Send the packet
                        if ((rand() % 100) >= DROP_PERCENT)  // TODO TEMP 10% packet loss
                            ::sendmsg(unicast_fd, &message, 0);
                    }
                }

                // Increment to send the next packet
                ++header.packet_no;
            }
        }

    }  // namespace network
}  // namespace extension
}  // namespace NUClear
