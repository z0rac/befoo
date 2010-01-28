/*
 * Copyright (C) 2009-2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
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
    int _verifylevel;
  public:
    tcpstream(int verifylevel) : _verifylevel(verifylevel) {}
    ~tcpstream() { _socket.shutdown(); }
    void connect(const string& host, const string& port, int domain);
    size_t read(char* buf, size_t size);
    size_t write(const char* data, size_t size);
    int tls() const;
    mailbox::backend::stream* starttls(const string& host);
  };
}

void
tcpstream::connect(const string& host, const string& port, int domain)
{
  assert(_socket == INVALID_SOCKET);
  _socket.connect(host, port, domain).timeout(TCP_TIMEOUT);
}

size_t
tcpstream::read(char* buf, size_t size)
{
  return _socket.recv(buf, size);
}

size_t
tcpstream::write(const char* data, size_t size)
{
  return _socket.send(data, size);
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
    typedef struct { int x509; } X509;
    typedef struct { int name; } X509NAME;

    typedef int (*SSL_library_init)(void);
    typedef SSL_CTX* (*SSL_CTX_new)(SSL_METHOD*);
    typedef void (*SSL_CTX_free)(SSL_CTX*);
    typedef SSL_METHOD* (*SSLv23_client_method)(void);
    typedef long (*SSL_CTX_set_options)(SSL_CTX*, long);
    typedef SSL* (*SSL_new)(SSL_CTX*);
    typedef void (*SSL_free)(SSL*);
    typedef int (*SSL_set_fd)(SSL*, int);
    typedef int (*SSL_connect)(SSL*);
    typedef int (*SSL_read)(SSL*, void*, int);
    typedef int (*SSL_write)(SSL*, const void*, int);
    typedef int (*SSL_shutdown)(SSL*);
    typedef int (*SSL_get_error)(const SSL*, int);
    typedef int (*SSL_get_verify_result)(const SSL*);
    typedef X509* (*SSL_get_peer_certificate)(const SSL*);

    typedef unsigned long (*ERR_get_error)(void);
    typedef char* (*ERR_error_string)(unsigned long, char*);
    typedef void (*X509_free)(X509*);
    typedef int (*X509_get_ext_by_NID)(X509*, int, int);
    typedef void* (*X509_get_ext)(X509*, int);
    typedef void* (*X509V3_EXT_d2i)(void*);
    typedef int (*sk_num)(const void*);
    typedef void* (*sk_value)(const void*, int);
    typedef void (*sk_free)(void*);
    typedef char* (*ASN1_STRING_data)(void*);
    typedef int (*ASN1_STRING_length)(void*);
    typedef X509NAME* (*X509_get_subject_name)(X509*);
    typedef int (*X509_NAME_get_text_by_NID)(X509NAME*, int, char*, int);
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

  class sslstream : public mailbox::backend::stream {
    SSL_CTX* _ctx;
    SSL* _ssl;
    SSL_read _read;
    SSL_write _write;
    winsock::tcpclient _socket;
    int _verifylevel;
    int _want(int rc);
    void _connect();
    void _shutdown();
    bool _verify(const string& host);
    static bool _match(const char* host, const char* subj, int len);
  public:
    sslstream(int verifylevel);
    ~sslstream();
    void connect(SOCKET socket, const string& host);
    void connect(const string& host, const string& port, int domain);
    size_t read(char* buf, size_t size);
    size_t write(const char* data, size_t size);
    int tls() const { return 1; }
    mailbox::backend::stream* starttls(const string&) { return NULL; }
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

sslstream::sslstream(int verifylevel)
  : _ssl(NULL), _read(SSL(SSL_read)), _write(SSL(SSL_write)),
    _verifylevel(verifylevel)
{
  _ctx = SSL(SSL_CTX_new)(SSL(SSLv23_client_method)());
  if (!_ctx) throw error();
#if !USE_SSL2
  XSSL(SSL_CTX_set_options)(_ctx, 0x01000000L /* SSL_OP_NO_SSLv2 */);
#endif
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
  case 2: /* SSL_ERROR_WANT_READ */ return rc;
  case 3: /* SSL_ERROR_WANT_WRITE */ return rc;
  }
  throw error();
}

void
sslstream::_connect()
{
  assert(_socket != INVALID_SOCKET && !_ssl);
  try {
    _ssl = SSL(SSL_new)(_ctx);
    if (!_ssl ||
	SSL(SSL_set_fd)(_ssl, _socket) != 1 ||
	SSL(SSL_connect)(_ssl) != 1) throw error();
  } catch (...) {
    if (_ssl) XSSL(SSL_free)(_ssl), _ssl = NULL;
    throw;
  }
}

void
sslstream::_shutdown()
{
  if (_ssl) {
    XSSL(SSL_shutdown)(_ssl);
    XSSL(SSL_free)(_ssl);
    _ssl = NULL;
  }
}

bool
sslstream::_verify(const string& host)
{
  switch (_verifylevel) {
  case 0: return true;
  case 1: break;
  default:
    switch(SSL(SSL_get_verify_result)(_ssl)) {
    case 18: /* X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT */
      if (_verifylevel > 2) return false;
    case 0: /* X509_V_OK */
    case 20: /* X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY */
      break;
    default:
      return false;
    }
  }

  int hostn = host.size();
  const char* hostp = host.c_str();
  struct buf {
    char* data;
    buf(size_t n) : data(new char[n]) {}
    ~buf() { delete [] data; }
  } buf(hostn + 1);

  X509* x509 = SSL(SSL_get_peer_certificate)(_ssl);
  if (!x509) return false;
  bool cn = true, ok = false;
  int id = LIB(X509_get_ext_by_NID)(x509, 85 /*NID_subject_alt_name*/, -1);
  if (id >= 0) {
    void* alt = LIB(X509V3_EXT_d2i)(LIB(X509_get_ext)(x509, id));
    if (alt) {
      sk_value value = LIB(sk_value);
      ASN1_STRING_data data = LIB(ASN1_STRING_data);
      ASN1_STRING_length length = LIB(ASN1_STRING_length);
      int n = LIB(sk_num)(alt);
      for (int i = 0; i < n && !ok; ++i) {
	typedef struct { int type; union { void* ia5; } d; } GENERAL_NAME;
	GENERAL_NAME* p = (GENERAL_NAME*)value(alt, i);
	if (p->type == 2 /*GEN_DNS*/) {
	  ok = _match(hostp, data(p->d.ia5), length(p->d.ia5));
	  cn = false;
	}
      }
      LIB(sk_free)(alt);
    }
  }
  if (cn) {
    X509NAME* name = LIB(X509_get_subject_name)(x509);
    if (name) {
      X509_NAME_get_text_by_NID get = LIB(X509_NAME_get_text_by_NID);
      int len = get(name, 13 /*NID_commonName*/, NULL, 0);
      if (len > 0 && len <= hostn &&
	  get(name, 13 /*NID_commonName*/, buf.data, len + 1) > 0) {
	ok = _match(hostp, buf.data, len);
      }
    }
  }
  LIB(X509_free)(x509);
  return ok;
}

bool
sslstream::_match(const char* host, const char* subj, int len)
{
  LOG(host << " : " << string(subj, len) << endl);
  for (int i = 0; i < len; ++i) {
    if (subj[i] == '*' && i + 1 < len && subj[i + 1] == '.') {
      while (*host && *host != '.') ++host;
      ++i;
    } else {
      if (tolower(*host) != tolower(subj[i])) return false;
    }
    if (!*host++) return false;
  }
  return !*host;
}

void
sslstream::connect(SOCKET socket, const string& host)
{
  assert(_socket == INVALID_SOCKET);
  _socket(socket).timeout(TCP_TIMEOUT);
  _connect();
  if (!_verify(host)) throw mailbox::error("invalid host");
}

void
sslstream::connect(const string& host, const string& port, int domain)
{
  assert(_socket == INVALID_SOCKET);
  _socket.connect(host, port, domain).timeout(TCP_TIMEOUT);
  _connect();
  if (!_verify(host)) throw mailbox::error("invalid host");
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
  for (;;) {
    int n = _want(_write(_ssl, data, size));
    if (n > 0) return size_t(n);
  }
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
  class sslstream : public mailbox::backend::stream {
    struct tls : public winsock::tlsclient {
      winsock::tcpclient socket;
#if USE_SSL2
      tls() : winsock::tlsclient(SP_PROT_SSL2 | SP_PROT_SSL3) {}
#endif
      ~tls() { shutdown(); }
      size_t _recv(char* buf, size_t size);
      size_t _send(const char* data, size_t size);
    };
    tls _tls;
    int _verifylevel;
    void _connect(const string& host);
  public:
    sslstream(int verifylevel) : _verifylevel(verifylevel) {}
    void connect(SOCKET socket, const string& host);
    void connect(const string& host, const string& port, int domain);
    size_t read(char* buf, size_t size);
    size_t write(const char* data, size_t size);
    int tls() const { return 1; }
    mailbox::backend::stream* starttls(const string&) { return NULL; }
  };
}

size_t
sslstream::tls::_recv(char* buf, size_t size)
{
  assert(socket != INVALID_SOCKET);
  for (;;) {
    size_t n = socket.recv(buf, size);
    if (n) return n;
    socket.wait(0, TCP_TIMEOUT);
  }
}

size_t
sslstream::tls::_send(const char* data, size_t size)
{
  assert(socket != INVALID_SOCKET);
  for (;;) {
    size_t n = socket.send(data, size);
    if (n) return n;
    socket.wait(1, TCP_TIMEOUT);
  }
}

void
sslstream::_connect(const string& host)
{
  _tls.connect();
  if (_verifylevel) {
    DWORD ignore = 0;
    switch (_verifylevel) {
    case 1: ignore |= (SECURITY_FLAG_IGNORE_REVOCATION |
		       SECURITY_FLAG_IGNORE_WRONG_USAGE |
		       SECURITY_FLAG_IGNORE_CERT_DATE_INVALID);
    case 2: ignore |= SECURITY_FLAG_IGNORE_UNKNOWN_CA;
    }
    if (!_tls.verify(host, ignore)) throw mailbox::error("invalid host");
  }
}

void
sslstream::connect(SOCKET socket, const string& host)
{
  assert(_tls.socket == INVALID_SOCKET);
  _tls.socket(socket).timeout(-1); // non-blocking
  _connect(host);
}

void
sslstream::connect(const string& host, const string& port, int domain)
{
  assert(_tls.socket == INVALID_SOCKET);
  _tls.socket.connect(host, port, domain).timeout(-1); // non-blocking
  _connect(host);
}

size_t
sslstream::read(char* buf, size_t size)
{
  return _tls.recv(buf, size);
}

size_t
sslstream::write(const char* data, size_t size)
{
  return _tls.send(data, size);
}

int
tcpstream::tls() const
{
  return 0;
}
#endif // !USE_OPENSSL

mailbox::backend::stream*
tcpstream::starttls(const string& host)
{
  assert(_socket != INVALID_SOCKET);
  auto_ptr<sslstream> st(new sslstream(_verifylevel));
  st->connect(_socket.release(), host);
  return st.release();
}

/*
 * Functions of the class mailbox::backend
 */
void
mailbox::backend::tcp(const string& host, const string& port, int domain, int verify)
{
  auto_ptr<tcpstream> st(new tcpstream(verify));
  st->connect(host, port, domain);
  _st = st;
}

void
mailbox::backend::ssl(const string& host, const string& port, int domain, int verify)
{
  auto_ptr<sslstream> st(new sslstream(verify));
  st->connect(host, port, domain);
  _st = st;
}

void
mailbox::backend::starttls(const string& host)
{
  stream* st = _st->starttls(host);
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
mailbox::backend::write(const char* data, size_t size)
{
  while (size) {
    size_t n = _st->write(data, size);
    data += n, size -= n;
  }
}

void
mailbox::backend::write(const string& data)
{
  string ln(data);
  ln.append("\015\012");
  write(ln.data(), ln.size());
}

/*
 * Functions of the class mailbox
 */
mailbox&
mailbox::uripasswd(const string& uri, const string& passwd)
{
  _uri = ::uri(uri);
  _passwd = passwd;
  return *this;
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
  extern backend* backendIMAP4();
  extern backend* backendPOP3();

  static const struct {
    const char* scheme;
    backend* (*make)();
    void (backend::*stream)(const string&, const string&, int, int);
    const char* port;
  } backends[] = {
    { "imap",     backendIMAP4, &backend::tcp, "143" },
    { "imap+ssl", backendIMAP4, &backend::ssl, "993" },
    { "pop",      backendPOP3,  &backend::tcp, "110" },
    { "pop+ssl",  backendPOP3,  &backend::ssl, "995" },
  };

  _recent = -1;
  int i = sizeof(backends) / sizeof(*backends);
  while (i-- && _uri[uri::scheme] != backends[i].scheme) continue;
  if (i < 0) throw error("invalid scheme");

  uri u(_uri);
  string pw(_passwd);
  if (u[uri::port].empty()) u[uri::port] = backends[i].port;
  if (u[uri::user].empty()) {
    u[uri::user] = "ANONYMOUS";
    if (pw.empty()) pw = "befoo@";
  }
  auto_ptr<backend> be(backends[i].make());
  ((*be).*backends[i].stream)(u[uri::host], u[uri::port], _domain, _verify);
  be->login(u, pw);
  _recent = be->fetch(*this, u);
  be->logout();
}
