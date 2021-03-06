/*
 * Copyright (C) 2009-2016 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "winsock.h"
#include "win32.h"
#include <cassert>
#include <algorithm>
#include <climits>

#ifdef _DEBUG
#include <iostream>
#define DBG(s) s
#define LOG(s) (cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

#ifndef SEC_E_CONTEXT_EXPIRED
#define SEC_E_CONTEXT_EXPIRED (-2146893033)
#define SEC_I_CONTEXT_EXPIRED 590615
#endif

/*
 * Functions of the class winsock
 */
namespace {
  int WSAAPI
  _getaddrinfo(const char* node, const char* service,
	       const struct addrinfo* hints, struct addrinfo** res)
  {
    assert(hints && hints->ai_flags == 0 &&
	   hints->ai_socktype == SOCK_STREAM && hints->ai_protocol == 0);

    LOG("Call _getaddrinfo()" << endl);

    struct sockaddr_in sa = { AF_INET };
    struct addrinfo ai = {
      0, hints->ai_family != AF_UNSPEC ? hints->ai_family : AF_INET, SOCK_STREAM, IPPROTO_TCP
    };
    if (ai.ai_family != AF_INET) return EAI_FAMILY;

    if (service) { // service to port number
      char* end;
      unsigned n = strtoul(service, &end, 10);
      if (*end || n > 65535) {
	struct servent* ent = getservbyname(service, "tcp");
	if (!ent) return h_errno;
	sa.sin_port = ent->s_port;
      } else {
	sa.sin_port = htons(static_cast<u_short>(n));
      }
    }

    if (node && !*node) node = NULL;
    sa.sin_addr.s_addr = inet_addr(node);
    if (sa.sin_addr.s_addr == INADDR_NONE) {
      struct hostent* ent = gethostbyname(node);
      if (!ent) return h_errno;
      if (ent->h_addrtype != AF_INET ||
	  ent->h_length != sizeof(sa.sin_addr)) return EAI_FAMILY;
      char** hal = ent->h_addr_list;
      int n = 0;
      while (hal && hal[n]) ++n;
      if (n == 0) return EAI_NONAME;

      *res = reinterpret_cast<struct addrinfo*>(new char[(sizeof(ai) + sizeof(sa)) * n]);
      struct addrinfo* ail = *res;
      struct sockaddr_in* sal = reinterpret_cast<struct sockaddr_in*>(*res + n);
      for (int i = 0; i < n; ++i) {
	sa.sin_addr = *reinterpret_cast<struct in_addr*>(hal[i]);
	ai.ai_addr = reinterpret_cast<struct sockaddr*>(&sal[i]);
	ai.ai_next = i + 1 < n ? &ail[i + 1] : NULL;
	ail[i] = ai, sal[i] = sa;
      }
    } else {
      *res = reinterpret_cast<struct addrinfo*>(new char[sizeof(ai) + sizeof(sa)]);
      ai.ai_addr = reinterpret_cast<struct sockaddr*>(*res + 1);
      **res = ai;
      *reinterpret_cast<struct sockaddr_in*>(ai.ai_addr) = sa;
    }
    return 0;
  }

  void WSAAPI
  _freeaddrinfo(struct addrinfo* info)
  {
    LOG("Call _freeaddrinfo()" << endl);
    delete [] reinterpret_cast<char*>(info);
  }
}

winsock::_get_t winsock::_get = NULL;
winsock::_free_t winsock::_free = NULL;

winsock::winsock()
{
  static win32::dll ws2("ws2_32.dll");
  _get = _get_t(ws2("getaddrinfo", FARPROC(_getaddrinfo)));
  _free = _free_t(ws2("freeaddrinfo", FARPROC(_freeaddrinfo)));

  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) throw error();
  LOG("Winsock version: " <<
      int(HIBYTE(wsa.wVersion)) << '.' << int(LOBYTE(wsa.wVersion)) << " <= " <<
      int(HIBYTE(wsa.wHighVersion)) << '.' << int(LOBYTE(wsa.wHighVersion)) << endl);
}

struct addrinfo*
winsock::getaddrinfo(const string& host, const string& port, int domain)
{
  struct addrinfo hints = { 0, domain, SOCK_STREAM };
  struct addrinfo* res;
  int err = _get(idn(host).c_str(), port.c_str(), &hints, &res);
  if (err == 0) return res;
  WSASetLastError(err);
  throw error();
}

string
winsock::error::emsg()
{
  char s[35];
  return string("winsock error #") + _ltoa(WSAGetLastError(), s, 10);
}

/*
 * Functions of the class winsock::tcpclient
 */
winsock::tcpclient&
winsock::tcpclient::connect(const string& host, const string& port, int domain)
{
  shutdown();
  LOG("Connect: " << host << "(" << idn(host) << "):" << port << endl);
  struct addrinfo* ai = getaddrinfo(host, port, domain);
  for (struct addrinfo* p = ai; p; p = p->ai_next) {
    SOCKET s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (s == INVALID_SOCKET) continue;
    if (::connect(s, p->ai_addr, int(p->ai_addrlen)) == 0) {
      _socket = s;
      break;
    }
    closesocket(s);
  }
  winsock::freeaddrinfo(ai);
  if (_socket == INVALID_SOCKET) throw error();
  return *this;
}

winsock::tcpclient&
winsock::tcpclient::shutdown()
{
  if (_socket != INVALID_SOCKET) {
    ::shutdown(_socket, SD_BOTH);
    char buf[32];
    while (::recv(_socket, buf, sizeof(buf), 0) > 0) continue;
    closesocket(_socket);
    _socket = INVALID_SOCKET;
  }
  return *this;
}

size_t
winsock::tcpclient::recv(char* buf, size_t size)
{
  int n = ::recv(_socket, buf, int(min<size_t>(size, INT_MAX)), 0);
  if (n >= 0) return n;
  if (WSAGetLastError() != WSAEWOULDBLOCK) throw error();
  return 0;
}

size_t
winsock::tcpclient::send(const char* data, size_t size)
{
  int n = ::send(_socket, data, int(min<size_t>(size, INT_MAX)), 0);
  if (n >= 0) return n;
  if (WSAGetLastError() != WSAEWOULDBLOCK) throw error();
  return 0;
}

winsock::tcpclient&
winsock::tcpclient::timeout(int sec)
{
  assert(_socket != INVALID_SOCKET);
  u_long nb = u_long(sec < 0);
  if (ioctlsocket(_socket, FIONBIO, &nb) != 0) throw error();
  if (sec >= 0) {
    int ms = sec * 1000;
    if (setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&ms, sizeof(ms)) != 0 ||
	setsockopt(_socket, SOL_SOCKET, SO_SNDTIMEO, (char*)&ms, sizeof(ms)) != 0) {
      throw error();
    }
  }
  return *this;
}

bool
winsock::tcpclient::wait(int op, int sec)
{
  assert(_socket != INVALID_SOCKET);
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(_socket, &fds);
  fd_set* fdsp[2] = { NULL, NULL };
  fdsp[op != 0] = &fds;
  timeval tv = { sec, 0 };
  int n = select(0, fdsp[0], fdsp[1], NULL, sec < 0 ? NULL : &tv);
  if (n == 0 && sec) {
    WSASetLastError(WSAETIMEDOUT);
    n = -1;
  }
  if (n < 0) throw error();
  return n > 0;
}

/*
 * Functions of the class winsock::tlsclient
 */
winsock::tlsclient::tlsclient(DWORD proto)
  : _avail(false)
{
  SCHANNEL_CRED auth = { SCHANNEL_CRED_VERSION };
  auth.grbitEnabledProtocols = proto;
  auth.dwFlags = (SCH_CRED_USE_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION);
  _ok(AcquireCredentialsHandle(NULL, const_cast<SEC_CHAR*>(UNISP_NAME_A),
			       SECPKG_CRED_OUTBOUND, NULL, &auth, NULL, NULL, &_cred, NULL));
}

winsock::tlsclient::~tlsclient()
{
  assert(!_avail);
  FreeCredentialsHandle(&_cred);
}

string
winsock::tlsclient::_emsg(SECURITY_STATUS ss)
{
  char s[9];
  return string("SSPI error #0x") + _ultoa(ss, s, 16);
}

SECURITY_STATUS
winsock::tlsclient::_ok(SECURITY_STATUS ss) const
{
  if (FAILED(ss)) throw error(_emsg(ss));
  return ss;
}

SECURITY_STATUS
winsock::tlsclient::_token(SecBufferDesc* inb)
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
			      NULL, req, 0, 0, inb, 0, &_ctx, &outb, &attr, NULL);
  _avail = true;
  switch (ss) {
  case SEC_I_COMPLETE_AND_CONTINUE: ss = SEC_I_CONTINUE_NEEDED;
  case SEC_I_COMPLETE_NEEDED: _ok(CompleteAuthToken(&_ctx, &outb));
  }
  _sendtoken((const char*)out.pvBuffer, out.cbBuffer);
  return ss;
}

void
winsock::tlsclient::_sendtoken(const char* data, size_t size)
{
  while (size) {
    size_t n = _send(data, size);
    data += n, size -= n;
  }
}

size_t
winsock::tlsclient::_copyextra(size_t i, size_t size)
{
  size_t n = min<size_t>(_extra.size(), size - i);
  CopyMemory(_buf.data + i, _extra.data(), n);
  _extra.erase(0, n);
  return n;
}

winsock::tlsclient&
winsock::tlsclient::connect()
{
  try {
    SECURITY_STATUS ss = _extra.empty() ? _token() : SEC_I_CONTINUE_NEEDED;
    const size_t buflen = 16 * 1024;
    _buf(buflen);
    size_t n = 0;
    while (ss == SEC_I_CONTINUE_NEEDED) {
      size_t t = !_extra.empty() ?
	_copyextra(n, buflen) :
	_recv(_buf.data + n, buflen - n);
      if (t == 0) throw error(_emsg(SEC_E_INCOMPLETE_MESSAGE));
      n += t;
      SecBuffer in[2] = { { DWORD(n), SECBUFFER_TOKEN, _buf.data } };
      SecBufferDesc inb = { SECBUFFER_VERSION, 2, in };
      ss = _token(&inb);
      if (ss == SEC_E_INCOMPLETE_MESSAGE) {
	if (n < buflen) ss = SEC_I_CONTINUE_NEEDED;
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
    shutdown();
    throw;
  }
  return *this;
}

winsock::tlsclient&
winsock::tlsclient::shutdown()
{
  if (_avail) {
    _recvq.clear(), _extra.clear();
    try {
#if __MINGW32__ && defined(ApplyControlToken) // for MinGWs bug.
      win32::dll secur32("secur32.dll");
      typedef SECURITY_STATUS (WINAPI* act)(PCtxtHandle, PSecBufferDesc);
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
  return *this;
}

bool
winsock::tlsclient::verify(const string& cn, DWORD ignore)
{
  LOG("Auth: " << cn << "(" << idn(cn) << ")... ");
  win32::wstr name(idn(cn));
  PCCERT_CHAIN_CONTEXT chain;
  {
    PCCERT_CONTEXT context;
    _ok(QueryContextAttributes(&_ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &context));
    CERT_CHAIN_PARA ccp = { sizeof(CERT_CHAIN_PARA) };
    BOOL ok = CertGetCertificateChain(NULL, context, NULL,
				      context->hCertStore, &ccp, 0, NULL, &chain);
    CertFreeCertificateContext(context);
    win32::valid(ok);
  }
  CERT_CHAIN_POLICY_STATUS status = { sizeof(CERT_CHAIN_POLICY_STATUS) };
  {
    CERT_CHAIN_POLICY_PARA policy = { sizeof(CERT_CHAIN_POLICY_PARA) };
    SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl;
    ZeroMemory(&ssl, sizeof(ssl));
    ssl.cbStruct = sizeof(ssl);
    ssl.dwAuthType = AUTHTYPE_SERVER;
    ssl.fdwChecks = ignore;
    ssl.pwszServerName = const_cast<LPWSTR>(LPCWSTR(name));
    policy.pvExtraPolicyPara = &ssl;
    BOOL ok = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL,
					       chain, &policy, &status);
    CertFreeCertificateChain(chain);
    win32::valid(ok);
  }
  LOG(status.dwError << endl);
  return !status.dwError;
}

size_t
winsock::tlsclient::recv(char* buf, size_t size)
{
  assert(_avail);
  if (!_recvq.empty()) {
    size_t n = min<size_t>(size, _recvq.size() - _rest);
    CopyMemory(buf, _recvq.data() + _rest, n);
    _rest += n;
    if (_rest == _recvq.size()) _recvq.clear();
    return n;
  }
  _rest = 0;
  size_t done = 0;
  size_t n = 0;
  while (size && !done) {
    size_t t = !_extra.empty() ?
      _copyextra(n, _sizes.cbMaximumMessage) :
      _recv(_buf.data + n, _sizes.cbMaximumMessage - n);
    if (t == 0) {
      if (n == 0) break;
      throw error(_emsg(SEC_E_INCOMPLETE_MESSAGE));
    }
    n += t;
    SecBuffer dec[4] = { { DWORD(n), SECBUFFER_DATA, _buf.data } };
    SecBufferDesc decb = { SECBUFFER_VERSION, 4, dec };
    SECURITY_STATUS ss = DecryptMessage(&_ctx, &decb, 0, NULL);
    if (ss == SEC_E_INCOMPLETE_MESSAGE && n < _sizes.cbMaximumMessage) continue;
    _ok(ss), n = 0;
    string extra;
    for (int i = 0; i < 4; ++i) {
      switch(dec[i].BufferType) {
      case SECBUFFER_DATA:
	if (dec[i].cbBuffer) {
	  size_t m = 0;
	  if (done < size) {
	    m = min<size_t>(size - done, dec[i].cbBuffer);
	    CopyMemory(buf + done, dec[i].pvBuffer, m);
	    done += m;
	  }
	  _recvq.append((char*)dec[i].pvBuffer + m, dec[i].cbBuffer - m);
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
      throw error(_emsg(SEC_E_CONTEXT_EXPIRED));
    }
    if (ss == SEC_I_RENEGOTIATE) connect();
  }
  return done;
}

size_t
winsock::tlsclient::send(const char* data, size_t size)
{
  assert(_avail);
  if (size) {
    size = min<size_t>(size, _sizes.cbMaximumMessage);
    SecBuffer enc[4] = {
      { _sizes.cbHeader, SECBUFFER_STREAM_HEADER, _buf.data },
      { DWORD(size), SECBUFFER_DATA, _buf.data + _sizes.cbHeader },
      { _sizes.cbTrailer, SECBUFFER_STREAM_TRAILER,
	_buf.data + _sizes.cbHeader + size }
    };
    SecBufferDesc encb = { SECBUFFER_VERSION, 4, enc };
    CopyMemory(_buf.data + _sizes.cbHeader, data, size);
    _ok(EncryptMessage(&_ctx, 0, &encb, 0));
    _sendtoken(_buf.data, enc[0].cbBuffer + enc[1].cbBuffer + enc[2].cbBuffer);
  }
  return size;
}

/*
 * Functions of IDN
 */
string
winsock::idn(const string& host)
{
  string::size_type i = 0;
  while (i < host.size() && BYTE(host[i]) < 0x80) ++i;
  return i == host.size() ? host : idn(win32::wstr(host));
}

string
winsock::idn(LPCWSTR host)
{
  struct lambda {
    static string encode(LPCWSTR input, size_t length, size_t n) {
      static const error overflow("punycode overflow");
      string output;
      size_t delta = 0, bias = 72, damp = 700;
      for (WCHAR code = 0x80; n < length; ++code) {
	WCHAR next = WCHAR(-1);
	for (size_t i = 0; i < length; ++i) {
	  if (input[i] >= code && input[i] < next) next = input[i];
	}
	size_t t = delta + (next - code) * (n + 1);
	if (t < delta) throw overflow;
	delta = t, code = next;
	for (size_t i = 0; i < length; ++i) {
	  if (input[i] != code) {
	    if (input[i] < code && ++delta == 0) throw overflow;
	  } else {
	    enum { base = 36, tmin = 1, tmax = 26, skew = 38 };
	    static const char b36[] = "abcdefghijklmnopqrstuvwxyz0123456789";
	    size_t q = delta;
	    for (size_t k = base;; k += base) {
	      size_t qd = k > bias ? min<size_t>(k - bias, tmax) : tmin;
	      if (q < qd) break;
	      output += b36[qd + (q - qd) % (base - qd)];
	      q = (q - qd) / (base - qd);
	    }
	    output += b36[q];
	    size_t k = 0;
	    q = delta / damp, q += q / ++n;
	    for (; q > (base - tmin) * tmax / 2; q /= base - tmin) k += base;
	    bias = k + (base - tmin + 1) * q / (q + skew);
	    delta = 0, damp = 2;
	  }
	}
	if (++delta == 0) throw overflow;
      }
      return output;
    }
  };

  string result;
  for (LPCWSTR p = host; *p; ++p) {
    string asc;
    LPCWSTR t = p;
    for (; *p && *p != '.'; ++p) {
      if (*p < 0x80) asc += static_cast<string::value_type>(*p);
    }
    if (asc.size() < static_cast<string::size_type>(p - t)) {
      result += "xn--";
      if (!asc.empty()) result += asc, result += '-';
      result += lambda::encode(t, p - t, asc.size());
    } else result += asc;
    if (!*p) break;
    result += static_cast<string::value_type>(*p);
  }
  return result;
}
