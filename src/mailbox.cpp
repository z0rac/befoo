/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "mailbox.h"
#include "winsock.h"
#include <cassert>

#ifdef _DEBUG
#include <iostream>
#define DBG(s) s
#define LOG(s) (cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

#define TCP_TIMEOUT 60

/** tcpstream - stream of TCP session.
 * This instance should be created by the function mailbox::backend::tcp.
 */
namespace {
  class tcpstream : public mailbox::backend::stream {
    winsock::tcpclient _socket;
  protected:
    SOCKET socket() const { return _socket; }
    SOCKET socket(SOCKET s)
    { assert(_socket == INVALID_SOCKET); return _socket(s); }
    void wait(int op);
  public:
    ~tcpstream() { _socket.shutdown(); }
    void open(const string& host, const string& port);
    void close();
    size_t read(char* buf, size_t size);
    size_t write(const char* data, size_t size);
    int tls() const;
    mailbox::backend::stream* starttls();
  };
}

void
tcpstream::wait(int op)
{
  if (!_socket.wait(op, TCP_TIMEOUT)) throw mailbox::error("timed out!");
}

void
tcpstream::open(const string& host, const string& port)
{
  assert(_socket == INVALID_SOCKET);
  LOG("Connect: " << host << ":" << port << endl);
  _socket.connect(host, port, false);
}

void
tcpstream::close()
{
  _socket.shutdown();
}

size_t
tcpstream::read(char* buf, size_t size)
{
  assert(_socket != INVALID_SOCKET);
  for (;;) {
    size_t n = _socket.recv(buf, size);
    if (n) return n;
    wait(0);
  }
}

size_t
tcpstream::write(const char* data, size_t size)
{
  assert(_socket != INVALID_SOCKET);
  for (size_t done = 0; done < size;) {
    size_t n = _socket.send(data + done, size - done);
    done += n;
    if (!n) wait(1);
  }
  return size;
}

/** sslstream - stream of SSL session.
 * This instance should be created by the function mailbox::backend::ssl().
 */
#if USE_OPENSSL
#include "win32.h"
namespace {
  extern "C" {
    typedef struct { int ssl; } SSL;
    typedef struct { int ctx; } SSL_CTX;
    typedef struct { int method; } SSL_METHOD;

    typedef int (*SSL_library_init)(void);
    typedef SSL_CTX* (*SSL_CTX_new)(SSL_METHOD*);
    typedef void (*SSL_CTX_free)(SSL_CTX*);
    typedef SSL_METHOD* (*SSLv23_client_method)(void);
    typedef SSL_METHOD* (*TLSv1_client_method)(void);
    typedef SSL* (*SSL_new)(SSL_CTX*);
    typedef void (*SSL_free)(SSL*);
    typedef int (*SSL_set_fd)(SSL*, int);
    typedef int (*SSL_connect)(SSL*);
    typedef int (*SSL_read)(SSL*, void*, int);
    typedef int (*SSL_write)(SSL*, const void*, int);
    typedef int (*SSL_shutdown)(SSL*);
    typedef int (*SSL_get_error)(SSL*, int);

    typedef unsigned long (*ERR_get_error)(void);
    typedef char* (*ERR_error_string)(unsigned long, char*);
  }

  struct openssl {
    win32::dll ssleay;
    win32::dll libeay;
    openssl();
    static void np() {}
  };
  openssl _openssl;

#define SSL(name) name(_openssl.ssleay(#name))
#define XSSL(name) name(_openssl.ssleay(#name, FARPROC(openssl::np)))
#define LIB(name) name(_openssl.libeay(#name))

  class sslstream : public tcpstream {
    SSL_CTX* _ctx;
    SSL* _ssl;
    SSL_read _read;
    SSL_write _write;
    int _want(int rc);
    void _connect();
    void _shutdown();
  public:
    sslstream();
    ~sslstream();
    void open(SOCKET socket);
    void open(const string& host, const string& port);
    void close();
    size_t read(char* buf, size_t size);
    size_t write(const char* data, size_t size);
    int tls() const { return 1; }
    mailbox::backend::stream* starttls() { return NULL; }
    static bool avail() { return _openssl.ssleay && _openssl.libeay; }
  public:
    struct error : public mailbox::error {
      error() : mailbox::error(emsg()) {}
      static string emsg();
    };
    friend struct error;
  };
}

openssl::openssl()
  : ssleay("ssleay32.dll", false), libeay("libeay32.dll", false)
{
  if (ssleay && libeay) XSSL(SSL_library_init)();
}

string
sslstream::error::emsg()
{
  char buf[256];
  return LIB(ERR_error_string)(LIB(ERR_get_error)(), buf);
}

sslstream::sslstream()
  : _ssl(NULL), _read(SSL(SSL_read)), _write(SSL(SSL_write))
{
#if USE_SSL2
  _ctx = SSL(SSL_CTX_new)(SSL(SSLv23_client_method)());
#else
  _ctx = SSL(SSL_CTX_new)(SSL(TLSv1_client_method)());
#endif
  if (!_ctx) throw error();
}

sslstream::~sslstream()
{
  _shutdown();
  XSSL(SSL_CTX_free)(_ctx);
}

int
sslstream::_want(int rc)
{
  if (rc > 0) return rc;
  switch (SSL(SSL_get_error)(_ssl, rc)) {
  case 2: /* SSL_ERROR_WANT_READ */ wait(0); return rc;
  case 3: /* SSL_ERROR_WANT_WRITE */ wait(1); return rc;
  }
  throw error();
}

void
sslstream::_connect()
{
  assert(socket() != INVALID_SOCKET && !_ssl);
  try {
    _ssl = SSL(SSL_new)(_ctx);
    if (!_ssl || !SSL(SSL_set_fd)(_ssl, socket())) throw error();
    while (_want(SSL(SSL_connect)(_ssl)) < 0) continue;
  } catch (...) {
    if (_ssl) XSSL(SSL_free)(_ssl), _ssl = NULL;
    throw;
  }
}

void
sslstream::_shutdown()
{
  if (_ssl) {
    try {
      int rc;
      do {
	rc = SSL(SSL_shutdown)(_ssl);
      } while (rc < 0 && _want(rc));
    } catch (...) {}
    XSSL(SSL_free)(_ssl);
    _ssl = NULL;
  }
}

void
sslstream::open(SOCKET socket)
{
  tcpstream::socket(socket);
  _connect();
}

void
sslstream::open(const string& host, const string& port)
{
  tcpstream::open(host, port);
  _connect();
}

void
sslstream::close()
{
  _shutdown();
  tcpstream::close();
}

size_t
sslstream::read(char* buf, size_t size)
{
  assert(_ssl);
  for (;;) {
    int n = _want(_read(_ssl, buf, size));
    if (n > 0) return size_t(n);
  }
}

size_t
sslstream::write(const char* data, size_t size)
{
  assert(_ssl);
  for (size_t done = 0; done < size;) {
    int n = _want(_write(_ssl, data + done, size - done));
    if (n > 0) done += n;
  }
  return size;
}

int
tcpstream::tls() const
{
  return sslstream::avail() ? 0 : -1;
}

#undef LIB
#undef XSSL
#undef SSL
#else // !USE_OPENSSL
namespace {
  class sslstream : public tcpstream, winsock::tlsclient {
    size_t _recv(char* buf, size_t size)
    { return tcpstream::read(buf, size); }
    size_t _send(const char* data, size_t size)
    { return tcpstream::write(data, size); }
  public:
#if USE_SSL2
    sslstream() : winsock::tlsclient(SP_PROT_SSL2 | SP_PROT_SSL3) {}
#endif
    ~sslstream() { winsock::tlsclient::shutdown(); }
    void open(SOCKET socket);
    void open(const string& host, const string& port);
    void close();
    size_t read(char* buf, size_t size) { return recv(buf, size); }
    size_t write(const char* data, size_t size) { return send(data, size); }
    int tls() const { return 1; }
    mailbox::backend::stream* starttls() { return NULL; }
  };
}

void
sslstream::open(SOCKET socket)
{
  tcpstream::socket(socket);
  winsock::tlsclient::connect();
}

void
sslstream::open(const string& host, const string& port)
{
  tcpstream::open(host, port);
  winsock::tlsclient::connect();
}

void
sslstream::close()
{
  winsock::tlsclient::shutdown();
  tcpstream::close();
}

int
tcpstream::tls() const
{
  return 0;
}
#endif // !USE_OPENSSL

mailbox::backend::stream*
tcpstream::starttls()
{
  assert(_socket != INVALID_SOCKET);
  auto_ptr<sslstream> st(new sslstream);
  st->open(_socket.release());
  return st.release();
}

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

void
mailbox::backend::starttls()
{
  stream* st = _st->starttls();
  if (st) _st.reset(st);
}

string
mailbox::backend::read(size_t size)
{
  string result;
  while (size) {
    char buf[1024];
    size_t n = _st->read(buf, min(size, sizeof(buf)));
    result.append(buf, n);
    size -= n;
  }
  return result;
}

string
mailbox::backend::read()
{
  char c;
  _st->read(&c, 1);
  string result(1, c);
  do {
    do {
      _st->read(&c, 1);
      result.push_back(c);
    } while (c != '\012');
  } while (result[result.size() - 2] != '\015');
  return result.erase(result.size() - 2); // remove CRLF
}

void
mailbox::backend::write(const string& data)
{
  string ln(data);
  ln.append("\015\012");
  write(ln.data(), ln.size());
}

/*
 * Functions of the class uri
 */
string
uri::_encode(const string& s, bool path)
{
  string result;
  int ps = path ? 0 : '/';
  string::size_type i = 0;
  while (i < s.size()) {
    const char* const t = s.c_str();
    const char* p = t + i;
    while (*p > 32 && *p < 127 && *p != ps && !strchr(":?#[]@;%", *p)) ++p;
    if (!*p) break;
    string::size_type n = p - t;
    result.append(s, i, n - i);
    char hex[4] = { '%' };
    _ltoa(s[n] & 255, hex + 1, 16);
    result.append(hex);
    i = n + 1;
  }
  if (i < s.size()) result.append(s.c_str() + i);
  return result;
}

string
uri::_decode(const string& s)
{
  string result;
  string::size_type i = 0;
  while (i < s.size()) {
    string::size_type n = s.find_first_of('%', i);
    if (n == string::npos || s.size() - n < 3) break;
    result.append(s, i, n - i);
    char hex[3] = { s[n + 1], s[n + 2] };
    char* e;
    char c = char(strtoul(hex, &e, 16));
    i = n + (*e ? (c = '%', 1) : 3);
    result.push_back(c);
  }
  if (i < s.size()) result.append(s.c_str() + i);
  return result;
}

void
uri::parse(const string& uri)
{
  for (int ui = scheme; ui <= fragment; ++ui) _part[ui].clear();

  string::size_type i = uri.find_first_of('#');
  if (i != string::npos) _part[fragment] = _decode(uri.substr(i + 1));

  string t(uri, 0, min(uri.find_first_of('?'), i));
  i = t.find("://");
  if (i != string::npos) {
    _part[scheme] = _decode(t.substr(0, i));
    t.erase(0, i + 3);
  }
  i = t.find_first_of('/');
  if (i != string::npos) {
    _part[path] = _decode(t.substr(i + 1));
    t.erase(i);
  }
  i = t.find_first_of('@');
  if (i != string::npos) {
    _part[user] = _decode(t.substr(0, min(t.find_first_of(';'), i)));
    t.erase(0, i + 1);
  }
  i = t.find_last_not_of("0123456789");
  if (i != string::npos && t[i] == ':') {
    _part[port] = t.substr(i + 1);
    t.erase(i);
  }
  _part[host] = _decode(t);

  _uri = _encode(_part[scheme]) + "://";
  if (!_part[user].empty()) _uri += _encode(_part[user]) + '@';
  _uri += _encode(_part[host]);
  if (!_part[port].empty()) _uri += ':' + _encode(_part[port]);
  _uri += '/' + _encode(_part[path], true);
  if (!_part[fragment].empty()) _uri += '#' + _encode(_part[fragment]);
}

/*
 * Functions of the class mailbox
 */
void
mailbox::uripasswd(const string& uri, const string& passwd)
{
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
  if (!be.get()) throw error("invalid scheme");
  string u(_uri[uri::user]), pw(_passwd);
  if (u.empty()) {
    u = "ANONYMOUS";
    if (pw.empty()) pw = "befoo@";
  }
  be->login(u, pw);
  _recent = be->fetch(*this, _uri);
  be->logout();
}
