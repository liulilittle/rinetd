#pragma once

#include <stdafx.h>
#include <config.h>

class tcp_forward : public std::enable_shared_from_this<tcp_forward> {
public:
    class tcp_connection : public std::enable_shared_from_this<tcp_connection> {
    public:
        inline tcp_connection(const std::shared_ptr<tcp_forward>& forward_, const std::shared_ptr<boost::asio::ip::tcp::socket>& socket_) 
            : enable_shared_from_this()
            , forward_(forward_)
            , timeout_(socket_->get_executor())
            , local_socket_(socket_)
            , remote_socket_(socket_->get_executor()) {
            syssocket_setsockopt(*socket_);
        }
        inline ~tcp_connection() {
            abort();
        }

    public:
        inline bool                                         run() {
            std::shared_ptr<tcp_connection> self = shared_from_this();
            try {
                listen_port& connect_dst_ = forward_->forward_;
                boost::asio::ip::tcp::endpoint connectEP = to_endpoint<boost::asio::ip::tcp>(connect_dst_.remote_host, connect_dst_.remote_port);

                boost::system::error_code ec;
                remote_socket_.open(connectEP.protocol(), ec);
                if (ec) {
                    return false;
                }

                remote_socket_.set_option(boost::asio::detail::socket_option::boolean<IPPROTO_TCP, TCP_FASTOPEN>(true), ec);
                syssocket_setsockopt(remote_socket_.native_handle(), connect_dst_.remote_host.bv6 ? false : true);

                timeout_.expires_from_now(boost::posix_time::seconds(RINETD_TCP_CONNECT_TIMEOUT));
                timeout_.async_wait([self, this](const boost::system::error_code& ec) {
                    if (ec != boost::system::errc::operation_canceled) {
                        abort();
                    }
                });
                remote_socket_.async_connect(connectEP, [self, this](boost::system::error_code ec_) {
                    if (ec_) {
                        abort();
                        return;
                    }

                    timeout_.cancel(ec_);
                    socket_to_destination(local_socket_.get(), &remote_socket_, local_socket_buf);
                    socket_to_destination(&remote_socket_, local_socket_.get(), remote_socket_buf);

                    if (forward_->log_ || !forward_->config_.log_var.empty()) {
                        wirte_log(2);
                    }
                });

                if (forward_->log_ || !forward_->config_.log_var.empty()) {
                    wirte_log(1);
                }
                return remote_socket_.is_open();
            }
            catch (std::exception&) {
                return false;
            }
        }   
        inline void                                         abort() {
            boost::system::error_code ec_;
            timeout_.cancel(ec_);
            
            close_socket(remote_socket_);
            close_socket(*local_socket_.get());
        }

    private:    
        inline bool                                         socket_to_destination(
            boost::asio::ip::tcp::socket*                   socket, 
            boost::asio::ip::tcp::socket*                   to,
            char*                                           buf) {
            if (!socket->is_open()) {
                return false;
            }
            std::shared_ptr<tcp_connection> self = shared_from_this();
            socket->async_receive(boost::asio::buffer(buf, RINETD_BUFFER_SIZE), 
                [self, this, socket, to, buf](const boost::system::error_code& ec, uint32_t sz) {
                    int by = std::max<int>(-1, ec ? -1 : sz);
                    if (by < 1) {
                        abort();
                        return;
                    }

                    boost::asio::async_write(*to, boost::asio::buffer(buf, sz), 
                        [self, this, socket, to, buf](const boost::system::error_code& ec, uint32_t sz) {
                            if (ec) {
                                abort();
                            }
                            else {
                                socket_to_destination(socket, to, buf);
                            }
                        });
                });
            return true;
        }
        inline void                                         wirte_log(int m_) {
            boost::asio::ip::tcp::endpoint socket_ep_;
            boost::system::error_code ec;
            try {
                socket_ep_ = local_socket_->remote_endpoint(ec);
                if (ec) {
                    abort();
                    return;
                }
            }
            catch (std::exception&) {
                abort();
                return;
            }

            boost::asio::ip::tcp::endpoint nat_ep_;
            try {
                nat_ep_ = remote_socket_.local_endpoint(ec);
                if (ec) {
                    abort();
                    return;
                }
            }
            catch (std::exception&) {
                abort();
                return;
            }

            listen_port& connect_dst_ = forward_->forward_;

            std::string sb;
            sb.append(PaddingRight(to_address(socket_ep_), 21, '\x20'));
            switch (m_) {
            case 1:
                sb.append("syn  ");
                break;
            case 2:
                sb.append("open ");
                break;
            default:
                throw std::runtime_error("This operand is not supported.");
                break;
            }
            sb.append(PaddingRight(to_address(connect_dst_.remote_host, connect_dst_.remote_port), 46, '\x20'));
            sb.append("nat ");
            sb.append(PaddingRight(to_address(nat_ep_), 46, '\x20'));
            sb.append(to_address(connect_dst_.local_host, connect_dst_.local_port));

            if (forward_->log_) {
                write_log(*forward_->log_.get(), sb);
            }
            else {
                std::string& log_var_ = forward_->config_.log_var;
                write_log(log_var_, sb);
            }
        }

    private:    
        std::shared_ptr<tcp_forward>                        forward_;
        boost::asio::deadline_timer                         timeout_;
        std::shared_ptr<boost::asio::ip::tcp::socket>       local_socket_;
        boost::asio::ip::tcp::socket                        remote_socket_;
        char                                                local_socket_buf[RINETD_BUFFER_SIZE];
        char                                                remote_socket_buf[RINETD_BUFFER_SIZE];
    };  
    inline tcp_forward(boost::asio::io_context& context_, rinetd_config& config_, listen_port& forward_, const std::shared_ptr<boost::asio::posix::stream_descriptor>& log_)
        : enable_shared_from_this()
        , context_(context_)
        , config_(config_)
        , forward_(forward_)
        , server_(context_)
        , log_(log_) {
        
    }
    inline ~tcp_forward() { 
        if (server_.is_open()) {  
            boost::system::error_code ec;
            try {
                server_.close(ec);
            }
            catch (std::exception&) {}
        }
    }
    inline bool                                             run() {
        boost::asio::ip::tcp::endpoint bindEP = to_endpoint<boost::asio::ip::tcp>(forward_.local_host, forward_.local_port);
        try {
            server_.open(bindEP.protocol());

            boost::system::error_code ec;
            server_.set_option(boost::asio::detail::socket_option::boolean<IPPROTO_TCP, TCP_FASTOPEN>(true), ec);

            server_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
            server_.bind(bindEP);
            server_.listen(RINETD_LISTEN_BACKLOG);
            syssocket_setsockopt(server_);

            accept_socket();
            return true;
        }
        catch (std::exception&) {
            return false;
        }
    }

private:    
    inline bool                                             accept_socket() {
        if (!server_.is_open()) {
            return false;
        }
        std::shared_ptr<boost::asio::ip::tcp::socket> socket = make_shared_object<boost::asio::ip::tcp::socket>(context_);
        std::shared_ptr<tcp_forward> self = shared_from_this();
        server_.async_accept(*socket.get(), [self, this, socket](boost::system::error_code ec) {
            if (ec) {
                close_socket(*socket.get());
            }
            else {
                std::shared_ptr<tcp_connection> connection_ = make_shared_object<tcp_connection>(self, socket);
                if (!connection_->run()) {
                    connection_->abort();
                }
            }
            accept_socket();
        });
        return true;
    }
    inline static void                                      close_socket(boost::asio::ip::tcp::socket& s) {
        if (s.is_open()) {  
            boost::system::error_code ec;
            try {
                s.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
            }
            catch (std::exception&) {}
            try {
                s.close(ec);
            }
            catch (std::exception&) {}
        }
    }

private:
    boost::asio::io_context&                                context_;
    rinetd_config&                                          config_;
    listen_port&                                            forward_;
    boost::asio::ip::tcp::acceptor                          server_;
    std::shared_ptr<boost::asio::posix::stream_descriptor>  log_;
};