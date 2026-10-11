/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <condition_variable>
#include <functional>
#include <string>
#include <string_view>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <unistd.h>

#include <Util/Defer.h>
#include <Util/Error.h>
#include <Util/Logging.h>

namespace Util {

class ReadPipes {
public:
    constexpr static int const StdOut = 0;
    constexpr static int const StdErr = 1;
    using Channel = int;

    using OnRead = std::function<void(ReadPipes &)>;
    ReadPipes() = default;
    ReadPipes(ReadPipes const &) = delete;
    ReadPipes(ReadPipes &&) = delete;
    ReadPipes(std::optional<OnRead> on_stdout, std::optional<OnRead> on_stderr = {});
    ~ReadPipes();

    CError initialize(std::optional<OnRead> on_stdout = {}, std::optional<OnRead> on_stderr = {});
    void   connect_parent();
    void   connect_child(int out, int err);
    void   close();

    template<uint8_t N>
        requires(N <= StdErr)
    bool expect()
    {
        return expect(N);
    }

    template<uint8_t N>
        requires(N <= StdErr)
    std::string current()
    {
        return current(N);
    }

private:
    void        read();
    CError      drain(Channel channel);
    bool        expect(Channel channel);
    std::string current(Channel channel);

    struct Pipe {
        int                   pipe[2];
        int                   fd;
        std::optional<OnRead> on_read;
        std::string           current;

        CError initialize(std::optional<OnRead> on_read_fnc = {});
        CError connect(int fd);
        void   connect_parent();
        void   connect_child(int fd);
        void   close();
    };

    std::array<Pipe, 2>     pipes;
    std::mutex              m_mutex {};
    std::condition_variable m_condition {};
    bool                    m_debug { false };
};

class WritePipe {
public:
    WritePipe() = default;
    WritePipe(WritePipe const &) = delete;
    WritePipe(WritePipe &&from) = delete;
    ~WritePipe();

    CError         initialize();
    void           close();
    void           connect_parent();
    void           connect_child(int fd) const;
    Result<size_t> write(std::string_view sv) const;
    Result<size_t> write_chars(char const *buf, size_t num) const;

private:
    int m_pipe[2];
    int m_fd;
};

}
