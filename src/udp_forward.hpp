#pragma once

#include <stdafx.h>
#include <config.h>

namespace boost {
    namespace system {
        namespace errc {
            #ifdef EHOSTDOWN 
            enum {
                host_down = EHOSTDOWN,
            };
            #endif
        }
    }
}

class udp_forward : public std::enable_shared_from_this<udp_forward> {
private:
    class udp_tunnel : public std::enable_shared_from_this<udp_tunnel> {
    public:
        inline udp_tunnel(std::shared_ptr<udp_forward>& owner_, boost::asio::ip::udp::endpoint& local_ep_) 
            : enable_shared_from_this()
            , owner_(owner_)
            , socket_(owner_->context_)
            , local_ep_(local_ep_) {
            last_ts_ = GetTickCount(false);
        }
        inline ~udp_tunnel() {
            abort();
        }
        inline bool                                         run() {
            const boost::asio::ip::udp::endpoint& server_ = owner_->server_;
            try {
                socket_.open(server_.protocol());
                if (server_.protocol() == boost::asio::ip::udp::v6()) {
                    boost::asio::ip::udp::endpoint bindEP(boost::asio::ip::address_v6::any(), 0);
                    socket_.bind(bindEP);
                }
                else {
                    boost::asio::ip::udp::endpoint bindEP(boost::asio::ip::address_v4::any(), 0);
                    socket_.bind(bindEP);
                }
                syssocket_setsockopt(socket_);

                next_msg();
                return true;
            }
            catch (std::exception&) {
                return false;
            }
        }
        inline bool                                         send_to(
            char*                                           buf, 
            size_t                                          size) {
            int by = udp_forward::send_to(socket_, buf, size, owner_->server_);
            if (by < 0) {
                this->abort();
                return false;
            }
            if (by > 0) {
                last_ts_ = GetTickCount(false);
                return true;
            }
            return false;
        }
        inline bool                                         next_msg() {
            if (!socket_.is_open()) {
                return false;
            }
            std::shared_ptr<udp_tunnel> self = shared_from_this();
            socket_.async_receive_from(boost::asio::buffer(owner_->buf_, UINT16_MAX), owner_->udp_ep_, 
                [self, this] (const boost::system::error_code& ec, uint32_t sz) {
                    int by = bytes_transferred(ec.value(), sz);
                    if (by < 0) {
                        this->abort();
                        return;
                    }
                    if (by > 0) {
                        owner_->send_to(owner_->buf_, sz, local_ep_);
                    }
                    next_msg();
                });
            return true;
        }
        inline bool                                         is_port_aging(uint64_t now) {
            if (last_ts_ > now || !socket_.is_open()) {
                return true;
            }
            uint64_t delta_ts_ = (now - last_ts_) / 1000;
            return delta_ts_ >= RINETD_DEFAULT_UDP_TIMEOUT;
        }
        inline void                                         abort() {
            close_socket(socket_);
        }

    private:
        std::shared_ptr<udp_forward>                        owner_;
        boost::asio::ip::udp::socket                        socket_;
        boost::asio::ip::udp::endpoint                      local_ep_;
        uint64_t                                            last_ts_;
    };
    typedef std::shared_ptr<udp_tunnel>                     udp_tunnel_ptr;
    typedef std::unordered_map<std::string, udp_tunnel_ptr> udp_tunnel_map;

public:
    inline udp_forward(boost::asio::io_context& context_, rinetd_config& config_, listen_port& forward_) 
        : enable_shared_from_this()
        , context_(context_)
        , forward_(forward_)
        , config_(config_)
        , socket_(context_)
        , check_timer_(context_) {
        static char s_buf_[UINT16_MAX];
        
        buf_ = s_buf_;
        server_ = to_endpoint<boost::asio::ip::udp>(forward_.remote_host, forward_.remote_port);
    }
    inline ~udp_forward() {
        boost::system::error_code ec;
        try {
            check_timer_.cancel(ec);
        }
        catch(std::exception&) {}
        udp_tunnel_map::iterator tail = tunnel_map_.begin();
        udp_tunnel_map::iterator endl = tunnel_map_.end();
        for (; tail != endl; tail++) {
            std::shared_ptr<udp_tunnel> tunntel_ = std::move(tail->second);
            if (tunntel_) {
                tunntel_->abort();
            }
        }
        tunnel_map_.clear();
        close_socket(socket_);
    }
    inline bool                                             run() {
        boost::asio::ip::udp::endpoint bindEP = 
            to_endpoint<boost::asio::ip::udp>(forward_.local_host, forward_.local_port);
        try {
            socket_.open(bindEP.protocol());
            socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
            socket_.bind(bindEP);
            syssocket_setsockopt(socket_);

            check_timer();
            accept_socket();
            return true;
        }
        catch (std::exception&) {
            return false;
        }
    }
    inline bool                                             send_to(
        char*                                               buf, 
        size_t                                              size, 
        boost::asio::ip::udp::endpoint&                     endpoint_) {
        return udp_forward::send_to(socket_, buf, size, endpoint_) > 0;
    }
    inline static int                                       send_to(
        boost::asio::ip::udp::socket&                       socket_, 
        char*                                               buf, 
        size_t                                              size, 
        boost::asio::ip::udp::endpoint&                     endpoint_) {
        if (!socket_.is_open()) {
            return -1;
        }

        if (!buf || !size) {
            return 0;
        }

        boost::system::error_code ec;
        #ifdef _WIN32
        size_t sz = socket_.send_to(boost::asio::buffer(buf, size), endpoint_, 0, ec);
        #else
        size_t sz = socket_.send_to(boost::asio::buffer(buf, size), endpoint_, MSG_NOSIGNAL, ec);
        #endif
        return bytes_transferred(ec.value(), sz);
    }
    inline static bool                                      release_error(int err) {
        return err == boost::system::errc::bad_file_descriptor        || // EBADF
                err == boost::system::errc::no_such_file_or_directory || // ENOENT
                err == boost::system::errc::not_a_socket              || // ENOTSOCK
                err == boost::system::errc::no_such_device            || // ENODEV
                err == boost::system::errc::io_error                  || // EIO
                err == boost::system::errc::network_down              || // ENETDOWN
                err == boost::system::errc::network_unreachable       || // ENETUNREACH
                #ifdef EHOSTDOWN 
                err == boost::system::errc::host_down                 || // EHOSTDOWN
                #endif
                err == boost::system::errc::host_unreachable;            // EHOSTUNREACH
    }
    inline static int                                       bytes_transferred(int err, size_t sz) {
        bool b = release_error(err);
        if (b) {
            return -1;
        }
        return std::max<int>(0, sz);
    }

private:
    inline void                                             accept_socket() {
        if (!socket_.is_open()) {
            return;    
        }
        std::shared_ptr<udp_forward> self = shared_from_this();
        socket_.async_receive_from(boost::asio::buffer(buf_, UINT16_MAX), udp_ep_, 
            [self, this] (const boost::system::error_code& ec, uint32_t sz) {
                do {
                    if (ec || sz == 0) {
                        break;
                    }
                    std::shared_ptr<udp_tunnel> tunnel_ = get_or_add_tunnel(udp_ep_);
                    if (tunnel_) {
                        tunnel_->send_to(buf_, sz);
                    }
                } while (0);
                accept_socket();
            });
    }
    inline std::shared_ptr<udp_tunnel>                      get_or_add_tunnel(boost::asio::ip::udp::endpoint& endpoint_) {
        std::string key = to_address(endpoint_);
        udp_tunnel_map::iterator it = tunnel_map_.find(key);
        if (it != tunnel_map_.end()) {
            return it->second;
        }
        std::shared_ptr<udp_forward> self_ = shared_from_this();
        std::shared_ptr<udp_tunnel> tunnel_ = make_shared_object<udp_tunnel>(self_, endpoint_);
        if (!tunnel_->run()) {
            return NULL;
        }
        tunnel_map_.insert(std::make_pair(key, tunnel_));
        return tunnel_;
    }
    inline void                                             check_timer() {
        std::shared_ptr<udp_forward> self = shared_from_this();
        boost::asio::deadline_timer::duration_type duration_time_ = boost::posix_time::seconds(10);
        
        check_timer_.expires_from_now(duration_time_);
        check_timer_.async_wait([self, this](const boost::system::error_code& ec) {
            next_tick(GetTickCount(false));
            check_timer();
        });
    }
    inline void                                             next_tick(uint64_t now) {
        std::vector<std::string> releases;
        udp_tunnel_map::iterator tail = tunnel_map_.begin();
        udp_tunnel_map::iterator endl = tunnel_map_.end();
        for (; tail != endl; tail++) {
            std::shared_ptr<udp_tunnel> tunntel_ = tail->second;
            if (!tunntel_ || tunntel_->is_port_aging(now)) {
                releases.push_back(tail->first);
            }
        }
        for (size_t i = 0, l = releases.size(); i < l; i++) {
            tail = tunnel_map_.find(std::move(releases[i]));
            if (tail != endl) {
                std::shared_ptr<udp_tunnel> tunntel_ = std::move(tail->second);
                if (tunntel_) {
                    tunntel_->abort();
                }
                tunnel_map_.erase(tail);
            }
        }
    }
    inline static void                                      close_socket(boost::asio::ip::udp::socket& s) {
        if (s.is_open()) {  
            boost::system::error_code ec;
            try {
                s.close(ec);
            }
            catch (std::exception&) {}
        }
    }

private:
    boost::asio::io_context&                                context_;
    listen_port&                                            forward_;
    rinetd_config&                                          config_;
    boost::asio::ip::udp::socket                            socket_;
    boost::asio::ip::udp::endpoint                          server_;
    boost::asio::ip::udp::endpoint                          udp_ep_;
    char*                                                   buf_;
    udp_tunnel_map                                          tunnel_map_;
    boost::asio::deadline_timer                             check_timer_;
};