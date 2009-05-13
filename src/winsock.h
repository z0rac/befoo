#ifndef H_WINSOCK /* -*- mode: c++ -*- */
/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#define H_WINSOCK

#include <exception>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>

using namespace std;

// winsock - winsock handler
class winsock {
  typedef int (WSAAPI *_get_t)(const char*, const char*,
			       const struct addrinfo*, struct addrinfo**);
  typedef void (WSAAPI *_free_t)(struct addrinfo*);
  static _get_t _get;
  static _free_t _free;
public:
  winsock();
  ~winsock() { WSACleanup(); }
  static struct addrinfo* getaddrinfo(const string& host, const string& port);
  static void freeaddrinfo(struct addrinfo* info) { _free(info); }
public:
  // tls - transport layer security
  class tls {
    CredHandle _cred;
    CtxtHandle _ctx;
    SecPkgContext_StreamSizes _sizes;
    bool _avail;
    string _readq;
    string::size_type _rest;
    string _extra;
    struct buf {
      char* data;
      buf() : data(NULL) {}
      ~buf() { delete [] data; }
      char* operator()(size_t n)
      { delete [] data, data = NULL; return data = new char[n]; }
    } _buf;
    static string _emsg(SECURITY_STATUS ss);
    SECURITY_STATUS _ok(SECURITY_STATUS ss) const;
    SECURITY_STATUS _token(SecBufferDesc* inb = NULL);
    size_t _copyextra(size_t i, size_t size);
  public:
    tls(DWORD proto = SP_PROT_SSL3 | SP_PROT_TLS1);
    virtual ~tls();
    void connect();
    void shutdown();
    size_t read(char* buf, size_t size);
    size_t write(const char* data, size_t size);
  protected:
    virtual size_t _recv(char* buf, size_t size) = 0;
    virtual size_t _send(const char* data, size_t size) = 0;
  };

  // error - exception type
  class error : public exception {
    string _msg;
  public:
    error() : _msg(emsg()) {}
    error(const string& msg) : _msg(msg) {}
    ~error() throw() {}
    const char* what() const throw() { return _msg.c_str(); }
    static string emsg();
  };
};

#endif
