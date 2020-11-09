#include "Connection.h"

#include <cassert>
#include <unistd.h>

#include <iostream>

namespace Afina {
namespace Network {
namespace STnonblock {

// See Connection.h
void Connection::Start() {
    started = true;
    begin_read = finish_read = 0;
    trans = 0;
    _event.events = EPOLLIN;
}

// See Connection.h
void Connection::OnError() {
    started = false;
    _event.events = 0;
}

// See Connection.h
void Connection::OnClose() {
    started = false;
    _event.events = 0;
}

// See Connection.h
void Connection::DoRead() {
    try {
        if ((readed_bytes = read(_socket, read_buf + finish_read, buf_size - finish_read)) > 0) {
            finish_read += readed_bytes;
            while (finish_read - begin_read > 0) {
                if (!command_to_execute) {
                    std::size_t parsed = 0;
                    if (parser.Parse(read_buf + begin_read, finish_read - begin_read, parsed)) {
                        command_to_execute = parser.Build(arg_remains);
                        if (arg_remains > 0) {
                            arg_remains += 2;
                        }
                    }

                    if (parsed == 0) {
                        break;
                    } else {
                        begin_read += parsed;
                    }
                }
                if (command_to_execute && arg_remains > 0) {
                    std::size_t read_arguments = std::min(arg_remains, finish_read - begin_read);
                    argument_for_command.append(read_buf + begin_read, read_arguments);

                    arg_remains -= read_arguments;
                    begin_read += read_arguments;
                }
                if (command_to_execute && arg_remains == 0) {
                    std::string result;
                    if (argument_for_command.size()) {
                        argument_for_command.resize(argument_for_command.size() - 2);
                    }
                    command_to_execute->Execute(*pStorage, argument_for_command, result);

                    result.append("\r\n");
                    responses.push_back(std::move(result));
                    if (!(_event.events & EPOLLOUT)) {
                        _event.events |= EPOLLOUT;
                    }

                    command_to_execute.reset();
                    argument_for_command.resize(0);
                    parser.Reset();
                }
            }
            if (begin_read == finish_read) {
                begin_read = finish_read = 0;
            } else if (finish_read == buf_size) {
                std::memmove(read_buf, read_buf + begin_read, finish_read - begin_read);
            }
        } else {
            started = false;
        }
    } catch (std::runtime_error &ex) {
        responses.push_back("ERROR\r\n");
        if (!(_event.events & EPOLLOUT)) {
            _event.events |= EPOLLOUT;
        }
    }
}

// See Connection.h
void Connection::DoWrite() {
    static constexpr size_t written_vec_size = 64;
    iovec written_vec[written_vec_size];
    size_t written_vec_v = 0;
    {
        auto it = responses.begin();
        assert(trans < it->size());
        written_vec[written_vec_v].iov_base = &((*it)[0]) + trans;
        written_vec[written_vec_v].iov_len = it->size() - trans;
        it++;
        written_vec_v++;
        for (; it != responses.end(); it++) {
            written_vec[written_vec_v].iov_base = &((*it)[0]);
            written_vec[written_vec_v].iov_len = it->size();
            if (++written_vec_v >= written_vec_size) {
                break;
            }
        }
    }

    int writed;
    if ((writed = writev(_socket, written_vec, written_vec_v)) > 0) {
        size_t i = 0;
        while (i < written_vec_v && writed >= written_vec[i].iov_len) {
            assert(responses.front().c_str() <= written_vec[i].iov_base &&
                   written_vec[i].iov_base < responses.front().c_str() + responses.front().size());
            responses.pop_front();
            writed -= written_vec[i].iov_len;
            i++;
        }
        trans = writed;
    } else {
        started = false;
    }

    if (responses.empty()) {
        _event.events &= ~EPOLLOUT;
    }
}

} // namespace STnonblock
} // namespace Network
} // namespace Afina