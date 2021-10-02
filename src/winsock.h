/* -*- mode: c++ -*-
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#pragma once

#include <exception>
#include <string>
#include <vector>
#include <memory>
#include <winsock2.h>
#include <wininet.h>
#include <ws2tcpip.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>

// winsock - winsock handler
class winsock {
public:
  winsock();
  ~winsock() { WSACleanup(); }
  static std::string idn(std::string_view domain);
  static std::string idn(std::wstring_view domain);
public:
  // tcpclient - TCP client socket
  class tcpclient {
    SOCKET _socket = INVALID_SOCKET;
  public:
    tcpclient() noexcept {}
    tcpclient(SOCKET socket) noexcept : _socket(socket) {}
    tcpclient(tcpclient const&) = delete;
    tcpclient(tcpclient&& a) noexcept { std::swap(_socket, a._socket); }
    ~tcpclient() { shutdown(); }
    tcpclient& operator=(tcpclient const&) = delete;
    tcpclient& operator=(tcpclient&& a) noexcept { return std::swap(_socket, a._socket), *this; }
    SOCKET release() noexcept { SOCKET s = _socket; _socket = INVALID_SOCKET; return s; }
    explicit operator bool() const noexcept { return _socket != INVALID_SOCKET; }
    tcpclient& connect(std::string const& host, std::string const& port, int domain = AF_UNSPEC);
    tcpclient& shutdown() noexcept;
    size_t recv(char* buf, size_t size);
    size_t send(char const* data, size_t size);
    tcpclient& timeout(int sec);
  };

  // tlsclient - transport layer security
  class tlsclient {
    CredHandle _cred;
    CtxtHandle _ctxb;
    CtxtHandle* _ctx = {};
    SecPkgContext_StreamSizes _sizes;
    std::string _recvq;
    size_t _rest = 0;
    std::vector<char> _rbuf;
    size_t _remain = 0;
    class error;
    SECURITY_STATUS _ok(SECURITY_STATUS ss) const;
    SECURITY_STATUS _init(SecBufferDesc* inb = {});
    void _send(char const* data, size_t size);
  public:
    tlsclient();
    virtual ~tlsclient();
    explicit operator bool() const noexcept { return _ctx != nullptr; }
    tlsclient& connect();
    tlsclient& shutdown() noexcept;
    bool verify(std::string const& cn, DWORD ignore = 0);
    size_t recv(char* buf, size_t size);
    size_t send(char const* data, size_t size);
  protected:
    virtual bool availlo() const noexcept = 0;
    virtual size_t recvlo(char* buf, size_t size) = 0;
    virtual size_t sendlo(char const* data, size_t size) = 0;
  };

  // error - exception type
  class error : public std::exception {
    std::string _msg;
  public:
    error() : _msg(emsg()) {}
    error(char const* msg) : _msg(msg) {}
    error(std::string const& msg) : _msg(msg) {}
    char const* what() const noexcept { return _msg.c_str(); }
    static std::string emsg();
  };
  class timedout : public error {
  public:
    timedout() : error("Timed out") {}
  };
};
