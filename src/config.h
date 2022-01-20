#pragma once

#include <stdio.h>
#include <limits.h>
#include <stdafx.h>

static int const RINETD_BUFFER_SIZE                     = 16384;
static int const RINETD_LISTEN_BACKLOG                  = 511;
static int const RINETD_DEFAULT_UDP_TIMEOUT             = 72;

typedef struct {
    bool                                                tcp_or_udp;
    uint32_t                                            local_host;
    uint32_t                                            local_port;
    uint32_t                                            remote_host;
    uint32_t                                            remote_port;
} listen_port;

typedef struct {
    std::vector<listen_port>                            listen_ports;
    std::string                                         log_var;
} rinetd_config;

bool                                                    write_log(const std::string& path_, const std::string& msg_);
bool                                                    write_log(boost::asio::posix::stream_descriptor& log_, const std::string& msg_);
std::shared_ptr<boost::asio::posix::stream_descriptor>  open_log(boost::asio::io_context& context_, const std::string& path_);
bool                                                    load_config(rinetd_config& config_, int argc, const char** argv);