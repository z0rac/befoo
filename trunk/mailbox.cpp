/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "mailbox.h"
#include "win32.h"
#include <cassert>

#ifdef _DEBUG
#include <iostream>
#define DBG(s) s
#define LOG(s) (cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

/** tcpstream - stream of TCP session.
 * This instance should be created by the function mailbox::backend::tcp.
 */
namespace {
  class tcpstream : public mailbox::backend::stream {
    SOCKET _fd;
  public:
    tcpstream() : _fd(INVALID_SOCKET) {}
    ~tcpstream();
    int open(const string& host, const string& port);
    void close();
    void read(char* buf, size_t size);
    void write(const char* data, size_t size);
    virtual bool tls() const { return false; }
    mailbox::backend::stream* starttls();
    int setfd(int fd) { assert(_fd == INVALID_SOCKET); return _fd = fd; }
  };
}

tcpstream::~tcpstream()
{
  close();
}

int
tcpstream::open(const string& host, const string& port)
{
  assert(_fd == INVALID_SOCKET);
  LOG("Connect: " << host << ":" << port << endl);
  struct addrinfo* ai = winsock::getaddrinfo(host, port);
  for (struct addrinfo* p = ai; p; p = p->ai_next) {
    SOCKET fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd == INVALID_SOCKET) continue;
    if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
      _fd = fd;
      break;
    }
    closesocket(fd);
  }
  winsock::freeaddrinfo(ai);
  if (_fd == INVALID_SOCKET) throw winsock::error();
  return _fd;
}

void
tcpstream::close()
{
  if (_fd != INVALID_SOCKET) {
    shutdown(_fd, SD_BOTH);
    char buf[32];
    while (recv(_fd, buf, sizeof(buf), 0) > 0) continue;
    closesocket(_fd);
    _fd = INVALID_SOCKET;
  }
}

void
tcpstream::read(char* buf, size_t size)
{
  assert(_fd != INVALID_SOCKET);
  if (size_t(recv(_fd, buf, size, 0)) != size) throw winsock::error();
}

void
tcpstream::write(const char* data, size_t size)
{
  assert(_fd != INVALID_SOCKET);
  if (size_t(send(_fd, data, size, 0)) != size) throw winsock::error();
}

/** sslstream - stream of SSL session.
 * This instance should be created by the function mailbox::backend::ssl().
 */
namespace {
  extern "C" {
    typedef struct { int ssl; } SSL;
    typedef struct { int ctx; } SSL_CTX;
    typedef struct { int method; } SSL_METHOD;

    typedef int (*SSL_library_init)(void);
    typedef SSL_CTX* (*SSL_CTX_new)(SSL_METHOD*);
    typedef void (*SSL_CTX_free)(SSL_CTX*);
    typedef SSL_METHOD* (*SSLv23_client_method)(void);
    typedef SSL* (*SSL_new)(SSL_CTX*);
    typedef void (*SSL_free)(SSL*);
    typedef int (*SSL_set_fd)(SSL*, int);
    typedef int (*SSL_connect)(SSL*);
    typedef int (*SSL_read)(SSL*, void*, int);
    typedef int (*SSL_write)(SSL*, const void*, int);
    typedef int (*SSL_shutdown)(SSL*);

    typedef unsigned long (*ERR_get_error)(void);
    typedef char* (*ERR_error_string)(unsigned long, char*);
  }
#define SSL(name) name(_ssleay(#name))
#define XSSL(name) name(_ssleay(#name, FARPROC(_dummy)))
#define LIB(name) name(_libeay(#name))

  class sslstream : public tcpstream {
    static win32::module _ssleay;
    static win32::module _libeay;
    SSL_CTX* _ctx;
    SSL* _ssl;
    SSL_read _read;
    SSL_write _write;
    static void _dummy() {};
    int _connect(int fd);
  public:
    sslstream();
    ~sslstream();
    int open(const string& host, const string& port);
    void close();
    void read(char* buf, size_t size);
    void write(const char* data, size_t size);
    bool tls() const { return true; }
    int setfd(int fd) { return _connect(tcpstream::setfd(fd)); }
    static bool avail() { return sslstream::_ssleay != NULL; }
  public:
    struct error : public mailbox::error {
      error() : mailbox::error(emsg()) {}
      static const char* emsg();
    };
    friend struct error;

    class openssl {
      win32::dll _ssleay;
      win32::dll _libeay;
    public:
      openssl();
    };
    friend class openssl;
  };
  win32::module sslstream::_ssleay;
  win32::module sslstream::_libeay;
}

const char*
sslstream::error::emsg()
{
  return LIB(ERR_error_string)(LIB(ERR_get_error)(), NULL);
}

sslstream::sslstream()
  : _ssl(NULL), _read(SSL(SSL_read)), _write(SSL(SSL_write))
{
  _ctx = SSL(SSL_CTX_new)(SSL(SSLv23_client_method)());
  if (!_ctx) throw error();
}

sslstream::~sslstream()
{
  close();
  XSSL(SSL_CTX_free)(_ctx);
}

int
sslstream::_connect(int fd)
{
  assert(!_ssl);
  try {
    if (!(_ssl = SSL(SSL_new)(_ctx)) ||
	!SSL(SSL_set_fd)(_ssl, fd) ||
	SSL(SSL_connect)(_ssl) != 1) throw error();
  } catch (...) {
    tcpstream::close();
    if (_ssl) XSSL(SSL_free)(_ssl), _ssl = NULL;
    throw;
  }
  return fd;
}

int
sslstream::open(const string& host, const string& port)
{
  return _connect(tcpstream::open(host, port));
}

void
sslstream::close()
{
  if (_ssl) {
    XSSL(SSL_shutdown)(_ssl);
    XSSL(SSL_free)(_ssl);
    _ssl = NULL;
    tcpstream::close();
  }
}

void
sslstream::read(char* buf, size_t size)
{
  assert(_ssl);
  if (_read(_ssl, buf, size) != int(size)) throw error();
}

void
sslstream::write(const char* data, size_t size)
{
  assert(_ssl);
  if (_write(_ssl, data, size) != int(size)) throw error();
}

mailbox::backend::stream*
tcpstream::starttls()
{
  assert(_fd != INVALID_SOCKET);
  auto_ptr<sslstream> st(new sslstream);
  SOCKET fd = _fd;
  _fd = INVALID_SOCKET;
  st->setfd(fd);
  return st.release();
}

sslstream::openssl::openssl()
  : _ssleay("ssleay32.dll", false),
    _libeay("libeay32.dll", false)
{
  if (_ssleay && _libeay) {
    SSL(SSL_library_init)();
    sslstream::_ssleay = _ssleay;
    sslstream::_libeay = _libeay;
  }
}

#undef LIB
#undef XSSL
#undef SSL

/*
 * Functions of the class mailbox::backend
 */
void
mailbox::backend::tcp(const string& host, const string& port)
{
  _st.reset(new tcpstream);
  _st->open(host, port);
}

void
mailbox::backend::ssl(const string& host, const string& port)
{
  _st.reset(new sslstream);
  _st->open(host, port);
}

int
mailbox::backend::tls() const
{
  return static_cast<tcpstream*>(_st.get())->tls() ? 1 :
    sslstream::avail() ? 0 : -1;
}

void
mailbox::backend::starttls()
{
  _st.reset(static_cast<tcpstream*>(_st.get())->starttls());
}

string
mailbox::backend::read(size_t size)
{
  string result;
  char buf[256];
  for (; size > sizeof(buf); size -= sizeof(buf)) {
    _st->read(buf, sizeof(buf));
    result.append(buf, sizeof(buf));
  }
  if (size) {
    _st->read(buf, size);
    result.append(buf, size);
  }
  return result;
}

string
mailbox::backend::readline()
{
  string result;
  char c;
  do {
    _st->read(&c, 1);
    result += c;
  } while (c != '\n');
  return result;
}

/*
 * Functions of the class uri
 */
void
uri::parse(const string& uri)
{
  for (int i = scheme; i <= fragment; ++i) _part[i].clear();

  string::size_type i = uri.find_first_of('#');
  if (i != string::npos) _part[fragment] = uri.substr(i + 1);

  string t(uri, 0, min(uri.find_first_of('?'), i));
  i = t.find("://");
  if (i != string::npos) {
    _part[scheme].assign(t, 0, i);
    t.erase(0, i + 3);
  }
  i = t.find_first_of('/');
  if (i != string::npos) {
    _part[path] = t.substr(i + 1);
    t.erase(i);
  }
  i = t.find_first_of('@');
  if (i != string::npos) {
    _part[user].assign(t, 0, i);
    t.erase(0, i + 1);
  }
  i = t.find_last_not_of("0123456789");
  if (i != string::npos && t[i] == ':') {
    _part[port] = t.substr(i + 1);
    t.erase(i);
  }
  _part[host] = t;
}

/*
 * Functions of the class mailbox
 */
void
mailbox::uripasswd(const string& uri, const string& passwd)
{
  static sslstream::openssl openssl;
  _uri.parse(uri);
  _passwd = passwd;
}

const mail*
mailbox::find(const string& uid) const
{
  list<mail>::const_iterator p = _mails.begin();
  for (; p != _mails.end(); ++p) {
    if (p->uid() == uid) return &*p;
  }
  return NULL;
}

void
mailbox::fetchmail()
{
  extern backend* imap4tcp(const string&, const string&);
  extern backend* imap4ssl(const string&, const string&);
  extern backend* pop3tcp(const string&, const string&);
  extern backend* pop3ssl(const string&, const string&);

  static const struct {
    const char* scheme;
    backend* (*make)(const string&, const string&);
  } backends[] = {
    { "imap", imap4tcp },
    { "imap+ssl", imap4ssl },
    { "pop", pop3tcp },
    { "pop+ssl", pop3ssl }
  };

  _recent = -1;
  auto_ptr<backend> be;
  for (int i = 0; i < int(sizeof(backends) / sizeof(*backends)); ++i) {
    if (_uri[uri::scheme] == backends[i].scheme) {
      be.reset(backends[i].make(_uri[uri::host], _uri[uri::port]));
      break;
    }
  }
  if (!be.get()) throw error("Invalid scheme.");
  be->login(_uri[uri::user], _passwd);
  _recent = be->fetch(*this, _uri);
  be->logout();
}
