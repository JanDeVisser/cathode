/*
 * Copyright (c) 2023, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cerrno>
#include <unistd.h>

#include <Util/Error.h>
#include <Util/Process.h>

// FIXME Courtesy Dr Claude:
//
// Claude found three problems here:
//
// 1. ReadPipes::read() stops at the first hangup, src/Util/Pipe.cpp:148-155
//
// for (auto ix = 0; ix < 2; ++ix) {
//      if (poll_fd[ix].revents & POLLIN) { MUST(drain(ix)); }
//      if (poll_fd[ix].revents & POLLHUP) { done = true; break; }
// }
//
// When the child aborts, the kernel closes stdout and stderr together. On the
// next poll, stdout (index 0) reports POLLHUP. You set done and break before
// reaching index 1, so the stderr data still in the pipe is never drained.
// This is almost certainly your bug.
//
// Fix it like this:
//
// Treat each fd separately.
// - On POLLHUP (or POLLIN|POLLHUP), drain that fd until read() returns 0.
// - Then set its poll_fd[ix].fd = -1, which makes poll ignore it.
// - Stop only once both fds are finished, and call notify_all() at that point.
//
// 2. Process::wait() doesn’t wait for the reader thread, src/Util/Process.cpp:100-115
//
// It returns as soon as waitpid does. The reader thread is detached and may not
// have drained anything yet. stderr() → current() only blocks while the buffer
// is empty. With a partial read, you get the partial string.
//
// Fix: keep the thread joinable instead of detaching it, and join() it in
// wait() after waitpid. Alternatively, block until both pipe fds have reached
// EOF. Either way, the output is complete by the time the exit status comes
// back.
//
// 3. Crashes skip the stderr output entirely
//
// When the child dies from SIGABRT, WIFEXITED is false and wait() returns an
// error. Callers like src/Lang/QBE/QBE.cpp:932-935 only read link.stderr() on a
// non-zero exit, not in the crash branch, so the message is dropped even when
// it was captured. Include stderr() in the crash error as well.
//
// A smaller issue, drain() at src/Util/Pipe.cpp:169-184: a non-blocking read
// that returns -1 with EAGAIN goes to make_error(), and MUST then kills the
// parent. Treat EAGAIN/EWOULDBLOCK the same as “no more data” and break out of
// the loop.
//
// Quick check: run the compiled program directly in a terminal, with no
// harness. If the full message shows up there, it confirms the bug is in
// ReadPipes/Process, not in the runtime.
//

extern "C" {

void sigchld(int)
{
}
}

namespace Util {

Process::Process(std::string_view const &cmd) noexcept
    : m_command(cmd)
{
}

Process::Process(std::string_view const &cmd, StringList args) noexcept
    : m_command(cmd)
    , m_arguments(std::move(args))
{
}

pid_t Process::pid() const
{
    return m_pid;
}

Process &Process::on_stdout_read(OnRead const &on_read)
{
    m_on_stdout_read = on_read;
    return *this;
}

Process &Process::on_stderr_read(OnRead const &on_read)
{
    m_on_stderr_read = on_read;
    return *this;
}

CError Process::start()
{
    info("[CMD] {} {}", m_command, join(m_arguments, ' '));
    signal(SIGCHLD, sigchld);
    size_t sz = m_arguments.size();
    size_t bufsz = m_command.length() + 1;
    for (size_t ix = 0u; ix < sz; ++ix) {
        bufsz += m_arguments[ix].length() + 1;
    }
    char        buf[bufsz];
    char const *argv[sz + 2];
    strcpy(buf, m_command.c_str());
    argv[0] = buf;
    char *bufptr = buf + m_command.length() + 1;
    for (size_t ix = 0u; ix < sz; ++ix) {
        strcpy(bufptr, m_arguments[ix].c_str());
        argv[ix + 1] = bufptr;
        bufptr = bufptr + m_arguments[ix].length() + 1;
    }
    argv[sz + 1] = NULL;

    // signal(SIGCHLD, SIG_IGN);
    if (auto err = m_in.initialize(); !err.has_value()) {
        return std::unexpected(err.error());
    }
    if (auto err = m_out_pipes.initialize(m_on_stdout_read, m_on_stderr_read); !err.has_value()) {
        return std::unexpected(err.error());
    }

    m_pid = fork();
    if (m_pid == -1) {
        return std::unexpected<LibCError>(errno);
    }
    if (m_pid == 0) {
        m_in.connect_child(STDIN_FILENO);
        m_out_pipes.connect_child(STDOUT_FILENO, STDERR_FILENO);
        assert(argv[0] != nullptr);
        ::execvp(argv[0], (char *const *) argv);
        fatal("execvp({}) failed: {}", m_command, LibCError(errno).description);
    }
    m_in.connect_parent();
    m_out_pipes.connect_parent();
    return { };
}

CError Process::background()
{
    return start();
}

ProcessResult Process::wait()
{
    if (m_pid == 0) {
        return { 0 };
    }
    int exit_code;
    if (waitpid(m_pid, &exit_code, 0) == -1 && errno != ECHILD && errno != EINTR) {
        return std::unexpected<LibCError>(errno);
    }
    m_pid = 0;
    m_in.close();
    if (!WIFEXITED(exit_code)) {
        return std::unexpected(LibCError { "Child program {} crashed due to signal {}", m_command, WTERMSIG(exit_code) });
    }
    return { WEXITSTATUS(exit_code) };
}

ProcessResult Process::execute()
{
    TRY(start());
    return wait();
}

CError Process::write_to(std::string_view const &msg)
{
    TRY(m_in.write(msg));
    return { };
}

void Process::set_arguments(StringList const &cmd_args)
{
    m_arguments = cmd_args;
}

}
