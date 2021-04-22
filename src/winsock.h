/* -*- mode: c++ -*-
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#pragma once

#include <exception>
#include <string>
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
    SOCKET _socket;
  public:
    tcpclient(SOCKET socket = INVALID_SOCKET) : _socket(socket) {}
    tcpclient(tcpclient const&) = delete;
    ~tcpclient() { shutdown(); }
    void operator=(tcpclient const&) = delete;
    tcpclient& operator()(SOCKET s) { _socket = s; return *this; }
    SOCKET release() { SOCKET s = _socket; _socket = INVALID_SOCKET; return s; }
    operator SOCKET() const { return _socket; }
    tcpclient& connect(std::string const& host, std::string const& port, int domain = AF_UNSPEC);
    tcpclient& shutdown();
    size_t recv(char* buf, size_t size);
    size_t send(char const* data, size_t size);
    tcpclient& timeout(int sec);
    bool wait(int op, int sec = -1);
  };

  // tlsclient - transport layer security
  class tlsclient {
    CredHandle _cred;
    CtxtHandle _ctx;
    SecPkgContext_StreamSizes _sizes;
    bool _avail = false;
    std::string _recvq;
    std::string::size_type _rest;
    std::string _extra;
    std::unique_ptr<char[]> _buf;
    static std::string _emsg(SECURITY_STATUS ss);
    SECURITY_STATUS _ok(SECURITY_STATUS ss) const;
    SECURITY_STATUS _token(SecBufferDesc* inb = {});
    void _sendtoken(char const* data, size_t size);
    size_t _copyextra(size_t i, size_t size);
  public:
    tlsclient();
    virtual ~tlsclient();
    bool avail() const { return _avail; }
    tlsclient& connect();
    tlsclient& shutdown();
    bool verify(std::string const& cn, DWORD ignore = 0);
    size_t recv(char* buf, size_t size);
    size_t send(char const* data, size_t size);
  protected:
    virtual size_t _recv(char* buf, size_t size) = 0;
    virtual size_t _send(char const* data, size_t size) = 0;
  };

  // error - exception type
  class error : public std::exception {
    std::string _msg;
  public:
    error() : _msg(emsg()) {}
    error(std::string const& msg) : _msg(msg) {}
    char const* what() const noexcept { return _msg.c_str(); }
    static std::string emsg();
  };
};
