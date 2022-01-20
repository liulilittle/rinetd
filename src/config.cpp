#include <config.h>

static std::string 
read_config(int argc, const char* argv[]) {
    std::string path;
    if (argc > 1) {
        char buf[PATH_MAX + 1];
        for (int i = 1; i < argc; i++) {
            if (sscanf(argv[i], "-c %s", buf) > 0) {
                path = buf;
                break;
            }
            else if (sscanf(argv[i], "--conf-file %s", buf) > 0) {
                path = buf;
                break;
            }
        }
        if (path.empty()) {
            int f_ = 0;
            for (int i = 1; i < argc; i++) {
                if (strcmp("-c", argv[i]) == 0 || strcmp("--conf-file", argv[i]) == 0) {
                    f_ = 1;
                    continue;
                }
                if (!f_) {
                    continue;
                }
                std::string str = RTrim(LTrim(argv[i]));
                if (str.empty()) {
                    continue;
                }
                f_ = 0;
                path = str;
            }
        }
    }

    path = RTrim(LTrim(path));
    if (path.empty()) {
        #ifdef _WIN32
        char cwd_[PATH_MAX + 1];
        GetCurrentDirectoryA(PATH_MAX, cwd_);

        char sz_[PATH_MAX + 1];
        sprintf(sz_, "%s\\rinetd.conf", cwd_);

        path = sz_;
        #else
        path = "/etc/rinetd.conf";
        #endif
    }

    FILE* f = fopen(path.data(), "rb");
    if (!f) {
        return "";
    }

    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::shared_ptr<char> buf = make_shared_alloc<char>(sz + 1);
    if (sz != fread(buf.get(), 1, sz, f)) {
        fclose(f);
        throw std::runtime_error("Can't from the specified file path of the file object to read all of its data into memory.");
    }
    else {
        fclose(f);
    }
    return std::string(buf.get(), sz);
}

static bool
parse_config(std::vector<listen_port>& out_, std::string& log_path_, const std::string& config_str) {
    log_path_.clear();
    out_.clear();
    if (config_str.empty()) {
        return false;
    }
    std::vector<std::string> lines_;
    if (Tokenize(config_str, lines_, "\r\n") < 1) {
        return false;
    }
    for (size_t i = 0, l = lines_.size(); i < l; i++) {
        std::string& line_ = lines_[i];
        if (line_.empty()) {
            continue;
        }
        size_t sz_ = line_.find_first_of('#');
        if (sz_ != std::string::npos) {
            if (sz_ == 0) {
                continue;
            }
            line_ = line_.substr(0, sz_);
        }
        char     local_host[128]; // fmt: 0.0.0.0 11111/tcp 1.1.1.1 22222/tcp
        char     remote_host[128];
        uint32_t local_port        = 0;
        uint32_t remote_port       = 0;
        bool     tcp_or_udp        = false;
        if (sscanf(line_.data(), "%s %u/tcp %s %u/tcp", local_host, &local_port, remote_host, &remote_port) >= 4) {
            tcp_or_udp = true;
        }
        else if (sscanf(line_.data(), "%s %u/udp %s %u/udp", local_host, &local_port, remote_host, &remote_port) >= 4) {
            tcp_or_udp = false;
        }
        else {
            char log_file[PATH_MAX + 1];
            if (sscanf(line_.data(), "logfile %s", log_file) >= 1) {
                uint32_t sz = strlen(log_file);
                if (!sz) {
                    continue;
                }
                log_path_ = std::string(log_file, sz);
            }
            continue;
        }
        if ((local_port == 0 || local_port > 65535) || (remote_port == 0 || remote_port > 65535)) {
            continue;
        }
        listen_port listen_port_;
        listen_port_.tcp_or_udp  = tcp_or_udp;
        listen_port_.local_host  = inet_addr(local_host);
        listen_port_.local_port  = local_port;
        listen_port_.remote_host = inet_addr(remote_host);
        listen_port_.remote_port = remote_port;
        if (listen_port_.local_host == INADDR_NONE || listen_port_.remote_host == INADDR_NONE) {
            continue;
        }
        out_.push_back(std::move(listen_port_));
    }
    return out_.size() > 0;
}

bool write_log(const std::string& path_, const std::string& msg_) {
    if (path_.empty() || msg_.empty()) {
        return false;
    }
    FILE* f = fopen(path_.data(), "ab+");
    if (!f) {
        return "";
    }
    std::string line_("[" + GetCurrentTimeText() + "]" + msg_ + "\r\n");
    fwrite(line_.data(), 1, line_.size(), f);
    fflush(f);
    fclose(f);
    return true;
}

bool write_log(boost::asio::posix::stream_descriptor& log_, const std::string& msg_) {
    if (!log_.is_open() || msg_.empty()) {
        return false; 
    }
    std::shared_ptr<std::string> line_ = make_shared_object<std::string>("[" + GetCurrentTimeText() + "]" + msg_ + "\r\n");
    log_.async_write_some(boost::asio::buffer(line_->data(), line_->size()), 
        [line_](const boost::system::error_code& ec, std::size_t sz){});
    return true;
}

std::shared_ptr<boost::asio::posix::stream_descriptor> open_log(boost::asio::io_context& context_, const std::string& path_) {
    if (path_.empty()) {
        return NULL;
    }
    #ifdef _WIN32
    HANDLE handle_ = CreateFileA(path_.data(), 
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, 
        NULL, 
        OPEN_ALWAYS, 
        FILE_FLAG_OVERLAPPED, 
        NULL);
    if (handle_ == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    LARGE_INTEGER li_ = { 0 };
    SetFilePointer(handle_, li_.LowPart, &li_.HighPart, FILE_END);
    return make_shared_object<boost::asio::posix::stream_descriptor>(context_, handle_);
    #else
    int handle_ = open(path_.data(), O_RDWR | O_CREAT, S_IRWXU);
    if (handle_ == -1) {
        return NULL;
    }
    lseek64(handle_, 0, SEEK_END);
    return make_shared_object<boost::asio::posix::stream_descriptor>(context_, handle_);
    #endif
}

bool load_config(rinetd_config& config_, int argc, const char* argv[]) {
    std::string config_str = read_config(argc, argv);
    if (config_str.empty()) {
        return false;
    }
    return parse_config(config_.listen_ports, config_.log_var, config_str);
}

std::string get_cmd_arg_str(const char* name, int argc, const char** argv) {
    if (argc <= 1) {
        return "";
    }
    for (int i = 1; i < argc; i++) {
        char* p = (char*)strstr(argv[i], name);
        if (!p) {
            continue;
        }
        p = strchr(p, '=');
        if (!p) {
            continue;
        }
        else {
            p = 1 + p;
        }
        char* l = strchr(p, ' ');
        if (!l) {
            l = strchr(p, '\t');
        }
        if (!l) {
            return p;
        }
        return std::string(p, l - p);
    }
    return "";
}