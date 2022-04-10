#pragma once

#include <stdio.h>
#include <limits.h>
#include <stdafx.h>

static int const RINETD_BUFFER_SIZE                     = 16384;
static int const RINETD_LISTEN_BACKLOG                  = 511;
static int const RINETD_DEFAULT_UDP_TIMEOUT             = 72;
static int const RINETD_TCP_CONNECT_TIMEOUT             = 5;

typedef struct {
    bool                                                bv6;
    uint32_t                                            in4;
    uint8_t                                             in6[16];
} ip_address;
typedef struct {
    bool                                                tcp_or_udp;
    ip_address                                          local_host;
    uint16_t                                            local_port;
    ip_address                                          remote_host;
    uint16_t                                            remote_port;
} listen_port;

typedef struct {
    std::vector<listen_port>                            listen_ports;
    std::string                                         log_var;
} rinetd_config;

template<class TProtocol>
inline boost::asio::ip::basic_endpoint<TProtocol>       to_endpoint(const ip_address& ip, uint16_t port) {
    typedef boost::asio::ip::basic_endpoint<TProtocol> protocol_endpoint;

    if (ip.bv6) {
        boost::asio::ip::address_v6::bytes_type host_; // IN6ADDR_ANY_INIT; IN6ADDR_LOOPBACK_INIT
        memcpy(host_.data(), &ip.in6, host_.size());

        return protocol_endpoint(boost::asio::ip::address_v6(host_), port);
    }
    else {
        boost::asio::ip::address_v4::uint_type host_ = htonl(ip.in4);
        return protocol_endpoint(boost::asio::ip::address_v4(host_), port);
    }
}
template<class TProtocol>
inline std::string                                      to_address(const boost::asio::ip::basic_endpoint<TProtocol>& ep) {
    return std::move(ep.address().to_string() + ":" + std::to_string(ep.port()));
}
inline std::string                                      to_address(const ip_address& ip, uint16_t port) {
    return to_address(to_endpoint<boost::asio::ip::tcp>(ip, port));
}
inline bool                                             parse_address(ip_address& dst, const char* src) {
    if (!src) {
        return false;
    }
    boost::system::error_code ec;
    boost::asio::ip::address host;
    try {
        host = boost::asio::ip::address::from_string(src, ec);
    } 
    catch (std::exception&) {
        ec = boost::asio::error::invalid_argument;
    }
    if (ec) {
        return false;
    }
    if (host.is_v6()) {
        boost::asio::ip::address_v6::bytes_type buf = host.to_v6().to_bytes();
        dst.bv6 = true;
        memcpy(&dst.in6, buf.data(), buf.size());
    }
    else if (host.is_v4()) {
        dst.bv6 = false;
        dst.in4 = *(uint32_t*)host.to_v4().to_bytes().data();
        if (dst.in4 == INADDR_NONE || dst.in4 == INADDR_NONE) {
            return false;
        }
    }
    else {
        return false;
    }
    return true;
}
void                                                    syssocket_setsockopt(int sockfd, bool v4_or_v6);
template<typename T>
inline void                                             syssocket_setsockopt(T& socket) {
    boost::system::error_code ec_;
    auto localEP = socket.local_endpoint(ec_);

    bool v4_or_v6 = false;
    if (!ec_) {
        v4_or_v6 = localEP.address().is_v4();
    }

    int sockfd = socket.native_handle();
    return syssocket_setsockopt(sockfd, v4_or_v6);
}
bool                                                    write_log(const std::string& path_, const std::string& msg_);
bool                                                    write_log(boost::asio::posix::stream_descriptor& log_, const std::string& msg_);
std::shared_ptr<boost::asio::posix::stream_descriptor>  open_log(boost::asio::io_context& context_, const std::string& path_);
bool                                                    load_config(rinetd_config& config_, int argc, const char** argv);