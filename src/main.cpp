#include <stdio.h>
#include <signal.h>
#include <limits.h>
#include <stdafx.h>
#include <config.h>
#include <tcp_forward.hpp>
#include <udp_forward.hpp>

static std::vector<std::shared_ptr<tcp_forward>> g_tcp_forwards;
static std::vector<std::shared_ptr<udp_forward>> g_udp_forwards;
static boost::asio::io_context context_;

#ifndef _WIN32
static void
do_signal(int signo) {
    if (signo != SIGHUP) {
        context_.stop();
        exit(0);
    }
}
#endif

static void 
run_all_ports(rinetd_config& config_, const std::shared_ptr<boost::asio::posix::stream_descriptor>& log_) {
    std::vector<listen_port>& list = config_.listen_ports;
    for (size_t i = 0, l = list.size(); i < l; i++) {
        listen_port& listen_port_ = list[i];
        if (listen_port_.tcp_or_udp) {
            std::shared_ptr<tcp_forward> forward_ = make_shared_object<tcp_forward>(context_, config_, listen_port_, log_);
            if (!forward_->run()) {
                continue;
            }
            g_tcp_forwards.push_back(forward_);
        }
        else {
            std::shared_ptr<udp_forward> forward_ = make_shared_object<udp_forward>(context_, config_, listen_port_);
            if (!forward_->run()) {
                continue;
            }
            g_udp_forwards.push_back(forward_);
        }
    }
}

static bool 
do_cli(int argc, const char** argv) {
    if (argc <= 1) {
        return false;
    }
    int h_cli = 0;
    int v_cli = 0;
    for (int i = 0; i < argc; i++) {
        const char* str = argv[i];
        if (strstr(str, "-h") || strstr(str, "-help")) {
            h_cli = 1;
            break;
        }
        if (strstr(str, "-v") || strstr(str, "--version")) {
            v_cli = 1;
            break;
        }
    }
    if (h_cli) {
        printf("%s\n", 
            "Usage: rinetd [OPTION]\n"
            "  -c, --conf-file FILE   read configuration from FILE\n"
            "  -h, --help             display this help\n"
            "  -v, --version          display version number\n"
            "\n"
            "Most options are controlled through the\n"
            "configuration file. See the rinetd(8)\n"
            "manpage for more information.");
        return true;
    }
    if (v_cli) {
        printf("%s\n", "rinetd 0.73 by supersocksr");
        return true;
    }
    return false;
}

static void
adjust_2_max_priority() {
    #ifdef _WIN32
    SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentProcess(), THREAD_PRIORITY_LOWEST);
    #else
    char path_[260];
    snprintf(path_, sizeof(path_), "/proc/%d/oom_adj", getpid());
    
    FILE* f = fopen(path_, "ab+");
    if (f) {
        char level_[] = "-17";
        fwrite(level_, 1, sizeof(level_), f);
        fflush(f);
        fclose(f);
    }

    /* Processo pai deve ter prioridade maior que os filhos. */
    setpriority(PRIO_PROCESS, 0, -20);
    
    /* ps -eo state,uid,pid,ppid,rtprio,time,comm */
    struct sched_param param_; 
    param_.sched_priority = sched_get_priority_max(SCHED_FIFO); // SCHED_RR
    sched_setscheduler(getpid(), SCHED_RR, &param_);
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param_);
    #endif
}

int main(int argc, const char* argv[]) {
    adjust_2_max_priority();
    if (do_cli(argc, argv)) {
        return 0;
    }

    #if !(_DEBUG || _WIN32)
    if (daemon(1, 1) < 0){
        return 1;
    }
    #endif

    rinetd_config config_;
    if (!load_config(config_, argc, argv)) {
        return -1;
    }
    
    #ifndef _WIN32
    signal(SIGHUP, do_signal);
    signal(SIGINT, do_signal);
    signal(SIGABRT, do_signal);
    signal(SIGKILL, do_signal);
    signal(SIGTERM, do_signal);
    #endif
    run_all_ports(config_, open_log(context_, config_.log_var));

    boost::asio::io_context::work work_(context_);
    boost::system::error_code ec_;
    context_.run(ec_);
    return ec_.value();
}

#pragma warning(disable: 5043)
extern void* 
operator new(size_t _Size) throw() {
    return Malloc(_Size);
}
 
extern void 
operator delete(void* _Block) throw() {
    Mfree(_Block);
}
 
extern void 
operator delete(void*  _Block, size_t _Size) throw() {
    if (_Size > 0) {
        ::operator delete(_Block);
    }
}
 
extern void* 
operator new[](size_t _Size) throw() {
    return ::operator new(_Size);
}

extern void 
operator delete[](void* _Block, size_t _Size) throw() {
    return ::operator delete(_Block, _Size);
}
#pragma warning(once: 5043)