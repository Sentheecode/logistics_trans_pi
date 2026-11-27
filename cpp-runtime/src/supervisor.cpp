#include "supervisor.hpp"
#include <stdexcept>
#ifdef __linux__
#include <cerrno>
#include <chrono>
#include <deque>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#endif

namespace sim {
#ifdef __linux__
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

class Child {
public:
    explicit Child(const std::function<int(const Pulse&)>& run) {
        int pipe[2];
        if (pipe2(pipe,O_CLOEXEC|O_NONBLOCK)!=0) throw std::runtime_error("heartbeat pipe failed");
        const pid_t parent=getpid();
        pid_=fork();
        if (pid_==0) {
            close(pipe[0]);
            if (prctl(PR_SET_PDEATHSIG,SIGKILL)!=0 || getppid()!=parent) _exit(125);
            try {
                const int result=run([fd=pipe[1]]{
                    char pulse=1;
                    (void)write(fd,&pulse,1); // A full pipe already contains evidence.
                });
                _exit(result);
            } catch (const std::exception& e) {
                std::cerr<<"supervised collector error: "<<e.what()<<'\n';
                _exit(1);
            }
        }
        close(pipe[1]);
        if (pid_<0) { close(pipe[0]); throw std::runtime_error("collector fork failed"); }
        fd_=pipe[0];
        std::cout<<"collector_started pid="<<pid_<<std::endl;
    }
    ~Child() { kill_and_reap(); close(fd_); }
    Child(const Child&)=delete;
    bool reaped() {
        if (pid_<0) return true;
        int status;
        const auto result=waitpid(pid_,&status,WNOHANG);
        if (result==pid_ || (result<0 && errno==ECHILD)) { pid_=-1; return true; }
        if (result<0 && errno!=EINTR) throw std::runtime_error("waitpid failed");
        return false;
    }
    bool pulse() {
        // Pipe buffers carry process-local evidence; never trust persisted wall-clock timestamps.
        char bytes[4096];
        return read(fd_,bytes,sizeof bytes)>0;
    }
    void stop() {
        if (reaped()) return;
        kill(pid_,SIGTERM);
        const auto end=Clock::now()+1s;
        while (Clock::now()<end) {
            if (reaped()) return;
            std::this_thread::sleep_for(20ms);
        }
        kill_and_reap();
    }
private:
    void kill_and_reap() noexcept {
        if (pid_<0) return;
        kill(pid_,SIGKILL);
        while (waitpid(pid_,nullptr,0)<0 && errno==EINTR) {}
        pid_=-1;
    }
    pid_t pid_=-1;
    int fd_=-1;
};
}

int supervise_collector(const std::function<int(const Pulse&)>& run, const volatile std::sig_atomic_t& stopping) {
    std::deque<Clock::time_point> restarts;
    while (!stopping) {
        {
            Child child(run);
            auto deadline=Clock::now()+5s;
            while (!stopping && !child.reaped()) {
                const auto now=Clock::now();
                if (child.pulse()) deadline=now+2s;
                if (now>=deadline) {
                    std::cerr<<"collector_watchdog_timeout\n";
                    break; // Destructor kills and reaps this owned child before restarting.
                }
                std::this_thread::sleep_for(50ms);
            }
            if (stopping) { child.stop(); return 0; }
        }
        const auto now=Clock::now();
        while (!restarts.empty() && now-restarts.front()>=60s) restarts.pop_front();
        if (restarts.size()>=3) {
            std::cerr<<"collector_restart_budget_exhausted\n";
            return 1;
        }
        restarts.push_back(now);
        for (int i=0;i<20 && !stopping;++i) std::this_thread::sleep_for(50ms);
    }
    return 0;
}
#else
int supervise_collector(const std::function<int(const Pulse&)>&,const volatile std::sig_atomic_t&) {
    throw std::runtime_error("supervised collector is currently supported only on Linux");
}
#endif
}
