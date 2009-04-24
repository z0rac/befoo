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

#define TCP_TIMEOUT 60

/** tcpstream - stream of TCP session.
 * This instance should be created by the function mailbox::backend::tcp.
 */
namespace {
  class tcpstream : public mailbox::backend::stream {
    SOCKET _fd;
  protected:
    void _wait(int op = -1) const;
  public:
    tcpstream(SOCKET fd = INVALID_SOCKET) : _fd(fd) {}
    ~tcpstream();
    int open(const string& host, const string& port);
    void close();
    size_t read(char* buf, size_t size);
    size_t write(const char* data, size_t size);
    int tls() const;
    mailbox::backend::stream* starttls();
  };
}

tcpstream::~tcpstream()
{
  close();
}

void
tcpstream::_wait(int op) const
{
  fd_set fds[2];
  fd_set* fdsp[2] = { NULL, NULL };
  for (int i = 0; i < 2; ++i) {
    if (op >> 1 || op == i) {
      fdsp[i] = fds + i;
      FD_ZERO(fdsp[i]);
      FD_SET(_fd, fdsp[i]);
    }
  }
  timeval tv = { TCP_TIMEOUT, 0 };
  int n = select(_fd + 1, fdsp[0], fdsp[1], NULL, &tv);
  if (n <= 0) throw winsock::error(n ? h_errno : WSAETIMEDOUT);
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
    u_long nb = 1;
    if (connect(fd, p->ai_addr, p->ai_addrlen) == 0 &&
	ioctlsocket(fd, FIONBIO, &nb) == 0) {
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

size_t
tcpstream::read(char* buf, size_t size)
{
  assert(_fd != INVALID_SOCKET);
  for (;;) {
    int n = recv(_fd, buf, size, 0);
    if (n > 0) return size_t(n);
    else if (n == 0 || h_errno != WSAEWOULDBLOCK) throw winsock::error();
    _wait(0);
  }
}

size_t
tcpstream::write(const char* data, size_t size)
{
  assert(_fd != INVALID_SOCKET);
  for (size_t done = 0; done < size;) {
    int n = send(_fd, data + done, size - done, 0);
    if (n > 0) done += n;
    else if (n == 0 || h_errno != WSAEWOULDBLOCK) throw winsock::error();
    else _wait(1);
  }
  return size;
}

/** sslstream - stream of SSL session.
 * This instance should be created by the function mailbox::backend::ssl().
 */
#if USE_OPENSSL
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
    int _connect(int fd);
  public:
    sslstream(int fd = -1);
    ~sslstream();
    int open(const string& host, const string& port);
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

sslstream::sslstream(int fd)
  : tcpstream(fd), _ssl(NULL),
    _read(SSL(SSL_read)), _write(SSL(SSL_write))
{
#if USE_SSL2
  _ctx = SSL(SSL_CTX_new)(SSL(SSLv23_client_method)());
#else
  _ctx = SSL(SSL_CTX_new)(SSL(TLSv1_client_method)());
#endif
  if (!_ctx) throw error();
  if (fd != -1) _connect(fd);
}

sslstream::~sslstream()
{
  close();
  XSSL(SSL_CTX_free)(_ctx);
}

int
sslstream::_want(int rc)
{
  if (rc > 0) return rc;
  switch (SSL(SSL_get_error)(_ssl, rc)) {
  case 2: /* SSL_ERROR_WANT_READ */ _wait(0); return rc;
  case 3: /* SSL_ERROR_WANT_WRITE */ _wait(1); return rc;
  }
  throw error();
}

int
sslstream::_connect(int fd)
{
  assert(fd != -1 && !_ssl);
  try {
    _ssl = SSL(SSL_new)(_ctx);
    if (!_ssl || !SSL(SSL_set_fd)(_ssl, fd)) throw error();
    while (_want(SSL(SSL_connect)(_ssl)) < 0) continue;
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

#undef LIB
#undef XSSL
#undef SSL
#else // !USE_OPENSSL

#define SECURITY_WIN32
#include <security.h>
#include <sspi.h>
#include <schannel.h>

#ifndef SEC_E_CONTEXT_EXPIRED
#define SEC_E_CONTEXT_EXPIRED (-2146893033)
#define SEC_I_CONTEXT_EXPIRED 590615
#endif

namespace {
  class sslstream : public tcpstream {
    CredHandle _cred;
    CtxtHandle _ctx;
    SecPkgContext_StreamSizes _sizes;
    bool _avail;
    struct buf {
      char* data;
      buf() : data(NULL) {}
      ~buf() { delete [] data; }
      char* operator()(size_t n)
      { delete [] data, data = NULL; return data = new char[n]; }
    } _buf;
    string _rbuf;
    string::size_type _rest;
    string _extra;
    size_t _copyextra(size_t i, size_t size)
    {
      size_t n = min<size_t>(_extra.size(), size - i);
      memcpy(_buf.data + i, _extra.data(), n);
      _extra.erase(0, n);
      return n;
    }
    SECURITY_STATUS _ok(SECURITY_STATUS ss) const
    { if (FAILED(ss)) throw winsock::error(ss); return ss; }
    SECURITY_STATUS _token(SecBufferDesc* inb = NULL);
    void _handshake();
  public:
    sslstream(int fd = -1);
    ~sslstream();
    int open(const string& host, const string& port);
    void close();
    size_t read(char* buf, size_t size);
    size_t write(const char* data, size_t size);
    int tls() const { return 1; }
    mailbox::backend::stream* starttls() { return NULL; }
    static bool avail() { return true; }
  };
}

sslstream::sslstream(int fd)
  : tcpstream(fd), _avail(false)
{
  SCHANNEL_CRED auth = { SCHANNEL_CRED_VERSION };
#if USE_SSL2
  auth.grbitEnabledProtocols = (SP_PROT_SSL2 | SP_PROT_SSL3);
#else
  auth.grbitEnabledProtocols = (SP_PROT_SSL3 | SP_PROT_TLS1);
#endif
  auth.dwFlags = (SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION);
  _ok(AcquireCredentialsHandle(NULL, UNISP_NAME_A, SECPKG_CRED_OUTBOUND,
			       NULL, &auth, NULL, NULL, &_cred, NULL));
  if (fd != -1) _handshake();
}

sslstream::~sslstream()
{
  close();
  FreeCredentialsHandle(&_cred);
}

SECURITY_STATUS
sslstream::_token(SecBufferDesc* inb)
{
  const ULONG req = (ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT |
		     ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR |
		     ISC_REQ_STREAM | ISC_REQ_ALLOCATE_MEMORY |
		     ISC_REQ_USE_SUPPLIED_CREDS); // for Win2kPro
  ULONG attr;
  struct obuf : public SecBuffer {
    obuf() { cbBuffer = 0, BufferType = SECBUFFER_TOKEN, pvBuffer = 0; }
    ~obuf() { if (pvBuffer) FreeContextBuffer(pvBuffer); }
  } out;
  SecBufferDesc outb = { SECBUFFER_VERSION, 1, &out };
  SECURITY_STATUS ss =
    InitializeSecurityContext(&_cred, _avail ? &_ctx : NULL,
			      NULL, req, 0, SECURITY_NATIVE_DREP,
			      inb, 0, &_ctx, &outb, &attr, NULL);
  _avail = true;
  switch (ss) {
  case SEC_I_COMPLETE_AND_CONTINUE: ss = SEC_I_CONTINUE_NEEDED;
  case SEC_I_COMPLETE_NEEDED: _ok(CompleteAuthToken(&_ctx, &outb));
  }
  tcpstream::write((const char*)out.pvBuffer, out.cbBuffer);
  return ss;
}

void
sslstream::_handshake()
{
  try {
    SECURITY_STATUS ss = _extra.empty() ? _token() : SEC_I_CONTINUE_NEEDED;
    const size_t buflen = 16 * 1024;
    _buf(buflen);
    size_t n = 0;
    while (ss == SEC_I_CONTINUE_NEEDED) {
      n += _copyextra(n, buflen);
      if (n == 0) n = tcpstream::read(_buf.data, buflen);
      SecBuffer in[2] = { { n, SECBUFFER_TOKEN, _buf.data } };
      SecBufferDesc inb = { SECBUFFER_VERSION, 2, in };
      ss = _token(&inb);
      if (ss == SEC_E_INCOMPLETE_MESSAGE) {
	if (n < buflen) {
	  n += tcpstream::read(_buf.data + n, buflen - n);
	  ss = SEC_I_CONTINUE_NEEDED;
	}
      } else {
	n = 0;
	if (in[1].BufferType == SECBUFFER_EXTRA) {
	  n = in[1].cbBuffer;
	  memmove(_buf.data, _buf.data + in[0].cbBuffer - n, n);
	}
      }
      _ok(ss);
    }
    _extra.assign(_buf.data, n);
    _ok(QueryContextAttributes(&_ctx, SECPKG_ATTR_STREAM_SIZES, &_sizes));
    _buf(_sizes.cbHeader + _sizes.cbMaximumMessage + _sizes.cbTrailer);
  } catch (...) {
    close();
    throw;
  }
}

int
sslstream::open(const string& host, const string& port)
{
  int fd = tcpstream::open(host, port);
  _handshake();
  return fd;
}

void
sslstream::close()
{
  if (_avail) {
    _rbuf.clear(), _extra.clear();
    try {
#if defined(__MINGW32__) // for MinGWs bug.
      win32::dll secur32("secur32.dll");
      typedef SECURITY_STATUS WINAPI (*act)(PCtxtHandle,PSecBufferDesc);
      act ApplyControlTokenA = act(secur32("ApplyControlToken"));
#endif
      DWORD value = SCHANNEL_SHUTDOWN;
      SecBuffer in = { sizeof(value), SECBUFFER_TOKEN, &value };
      SecBufferDesc inb = { SECBUFFER_VERSION, 1, &in };
      _ok(ApplyControlToken(&_ctx, &inb));
      while (_token() == SEC_I_CONTINUE_NEEDED) continue;
    } catch (...) {
      LOG("SSPI: shutdown error." << endl);
    }
    DeleteSecurityContext(&_ctx);
    _avail = false;
  }
  tcpstream::close();
}

size_t
sslstream::read(char* buf, size_t size)
{
  assert(_avail);
  if (!_rbuf.empty()) {
    size_t n = min<size_t>(size, _rbuf.size() - _rest);
    memcpy(buf, _rbuf.data() + _rest, n);
    _rest += n;
    if (_rest == _rbuf.size()) _rbuf.clear();
    return n;
  }
  _rest = 0;
  size_t done = 0;
  size_t n = 0;
  while (!done) {
    n += !_extra.empty() ? _copyextra(n, _sizes.cbMaximumMessage) :
      tcpstream::read(_buf.data + n, _sizes.cbMaximumMessage - n);
    SecBuffer dec[4] = { { n, SECBUFFER_DATA, _buf.data } };
    SecBufferDesc decb = { SECBUFFER_VERSION, 4, dec };
    SECURITY_STATUS ss = DecryptMessage(&_ctx, &decb, 0, NULL);
    if (ss == SEC_E_INCOMPLETE_MESSAGE &&
	n < _sizes.cbMaximumMessage) continue;
    _ok(ss), n = 0;
    string extra;
    for (int i = 0; i < 4; ++i) {
      switch(dec[i].BufferType) {
      case SECBUFFER_DATA:
	if (dec[i].cbBuffer) {
	  size_t n = 0;
	  if (done < size) {
	    n = min<size_t>(size - done, dec[i].cbBuffer);
	    memcpy(buf + done, dec[i].pvBuffer, n);
	    done += n;
	  }
	  _rbuf.append((char*)dec[i].pvBuffer + n, dec[i].cbBuffer - n);
	} else if (ss == SEC_E_OK && _buf.data[0] == 0x15) {
	  ss = SEC_I_CONTEXT_EXPIRED; // for Win2kPro
	}
	break;
      case SECBUFFER_EXTRA:
	extra.append((char*)dec[i].pvBuffer, dec[i].cbBuffer);
	break;
      }
    }
    _extra = extra + _extra;
    if (ss == SEC_I_CONTEXT_EXPIRED && !done) {
      throw winsock::error(SEC_E_CONTEXT_EXPIRED);
    }
    if (ss == SEC_I_RENEGOTIATE) _handshake();
  }
  return done;
}

size_t
sslstream::write(const char* data, size_t size)
{
  assert(_avail);
  size_t done = 0;
  while (done < size) {
    size_t n = min<size_t>(size - done, _sizes.cbMaximumMessage);
    SecBuffer enc[4] = {
      { _sizes.cbHeader, SECBUFFER_STREAM_HEADER, _buf.data },
      { n, SECBUFFER_DATA, _buf.data + _sizes.cbHeader },
      { _sizes.cbTrailer, SECBUFFER_STREAM_TRAILER,
	_buf.data + _sizes.cbHeader + n }
    };
    SecBufferDesc encb = { SECBUFFER_VERSION, 4, enc };
    memcpy(_buf.data + _sizes.cbHeader, data + done, n);
    _ok(EncryptMessage(&_ctx, 0, &encb, 0));
    tcpstream::write(_buf.data,
		     enc[0].cbBuffer + enc[1].cbBuffer + enc[2].cbBuffer);
    done += n;
  }
  return done;
}
#endif // !USE_OPENSSL

int
tcpstream::tls() const
{
  return sslstream::avail() ? 0 : -1;
}

mailbox::backend::stream*
tcpstream::starttls()
{
  assert(_fd != INVALID_SOCKET);
  SOCKET fd = _fd;
  _fd = INVALID_SOCKET;
  sslstream* st = new(nothrow) sslstream(fd);
  if (st) return st;
  _fd = fd;
  throw bad_alloc();
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
