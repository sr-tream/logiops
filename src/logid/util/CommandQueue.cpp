// SPDX-License-Identifier: GPL-3.0-or-later
#include <util/CommandQueue.h>
#include <util/log.h>
#include <cerrno>
#include <cstring>
#include <deque>
#include <mutex>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>

extern char** environ;

struct logid::CommandQueue::State {
    struct Job {
        std::vector<std::string> command;
        std::string context;
    };
    std::mutex mutex;
    std::deque<Job> jobs;
    bool running = false;
};

logid::CommandQueue::CommandQueue() : _state(std::make_shared<State>()) {}

void logid::CommandQueue::run(std::vector<std::string> command, std::string context) {
    if (command.empty() || command.front().empty())
        return;
    std::lock_guard lock(_state->mutex);
    _state->jobs.push_back({std::move(command), std::move(context)});
    if (_state->running)
        return;
    _state->running = true;
    try {
        std::thread([state = _state] {
            for (;;) {
                State::Job job;
                {
                    std::lock_guard guard(state->mutex);
                    if (state->jobs.empty()) {
                        state->running = false;
                        return;
                    }
                    job = std::move(state->jobs.front());
                    state->jobs.pop_front();
                }
                std::vector<char*> argv;
                for (auto& arg : job.command)
                    argv.push_back(arg.data());
                argv.push_back(nullptr);
                pid_t pid = 0;
                int error = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ);
                if (error) {
                    logPrintf(WARN, "%s: cannot run %s: %s", job.context.c_str(),
                              argv[0], std::strerror(error));
                    continue;
                }
                int status = 0;
                pid_t waited;
                do {
                    waited = waitpid(pid, &status, 0);
                } while (waited == -1 && errno == EINTR);
                if (waited == -1)
                    logPrintf(WARN, "%s: waitpid failed: %s", job.context.c_str(),
                              std::strerror(errno));
                else if (WIFSIGNALED(status))
                    logPrintf(WARN, "%s: %s terminated by signal %d", job.context.c_str(),
                              argv[0], WTERMSIG(status));
                else if (WIFEXITED(status) && WEXITSTATUS(status))
                    logPrintf(WARN, "%s: %s exited with status %d", job.context.c_str(),
                              argv[0], WEXITSTATUS(status));
            }
        }).detach();
    } catch (...) {
        _state->running = false;
        throw;
    }
}
