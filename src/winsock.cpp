/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
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
#define LOG(s) (std::cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

/*
 * Functions of the class winsock
 */
winsock::winsock()
{
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) throw error();
  LOG("Winsock version: " <<
      int(HIBYTE(wsa.wVersion)) << '.' << int(LOBYTE(wsa.wVersion)) << " <= " <<
      int(HIBYTE(wsa.wHighVersion)) << '.' << int(LOBYTE(wsa.wHighVersion)) << std::endl);
}

std::string
winsock::error::emsg()
{
  char s[35];
  return std::string("winsock error #") + _ltoa(WSAGetLastError(), s, 10);
}

/*
 * Functions of the class winsock::tcpclient
 */
winsock::tcpclient&
winsock::tcpclient::connect(std::string const& host, std::string const& port, int domain)
{
  shutdown();
  LOG("Connect: " << host << "(" << idn(host) << "):" << port << std::endl);
  struct addrinfo* ai;
  {
    struct addrinfo hints { 0, domain, SOCK_STREAM };
    auto err = getaddrinfo(idn(host).c_str(), port.c_str(), &hints, &ai);
    if (err) {
      WSASetLastError(err);
      throw error();
    }
  }
  for (auto p = ai; p; p = p->ai_next) {
    auto s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (s == INVALID_SOCKET) continue;
    if (::connect(s, p->ai_addr, int(p->ai_addrlen)) == 0) {
      _socket = s;
      break;
    }
    closesocket(s);
  }
  freeaddrinfo(ai);
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
  int n = ::recv(_socket, buf, int(std::min<size_t>(size, INT_MAX)), 0);
  if (n >= 0) return n;
  if (WSAGetLastError() != WSAEWOULDBLOCK) throw error();
  return 0;
}

size_t
winsock::tcpclient::send(char const* data, size_t size)
{
  int n = ::send(_socket, data, int(std::min<size_t>(size, INT_MAX)), 0);
  if (n >= 0) return n;
  if (WSAGetLastError() != WSAEWOULDBLOCK) throw error();
  return 0;
}

winsock::tcpclient&
winsock::tcpclient::timeout(int sec)
{
  assert(_socket != INVALID_SOCKET);
  auto nb = u_long(sec < 0);
  if (ioctlsocket(_socket, FIONBIO, &nb) != 0) throw error();
  if (sec >= 0) {
    DWORD ms = sec * 1000;
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
  fd_set* fdsp[2] {};
  fdsp[op != 0] = &fds;
  timeval tv { sec, 0 };
  int n = select(0, fdsp[0], fdsp[1], {}, sec < 0 ? nullptr : &tv);
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
winsock::tlsclient::tlsclient()
{
  SCHANNEL_CRED auth { SCHANNEL_CRED_VERSION };
  auth.dwFlags = (SCH_CRED_USE_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION);
  _ok(AcquireCredentialsHandle({}, const_cast<SEC_CHAR*>(UNISP_NAME_A),
			       SECPKG_CRED_OUTBOUND, {}, &auth, {}, {}, &_cred, {}));
}

winsock::tlsclient::~tlsclient()
{
  assert(!_avail);
  FreeCredentialsHandle(&_cred);
}

std::string
winsock::tlsclient::_emsg(SECURITY_STATUS ss)
{
  char s[9];
  return std::string("SSPI error #0x") + _ultoa(ss, s, 16);
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
  constexpr ULONG req = (ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT |
			 ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR |
			 ISC_REQ_STREAM | ISC_REQ_ALLOCATE_MEMORY |
			 ISC_REQ_USE_SUPPLIED_CREDS); // for Win2kPro
  ULONG attr;
  struct obuf : public SecBuffer {
    obuf() { cbBuffer = 0, BufferType = SECBUFFER_TOKEN, pvBuffer = 0; }
    ~obuf() { if (pvBuffer) FreeContextBuffer(pvBuffer); }
  } out;
  SecBufferDesc outb { SECBUFFER_VERSION, 1, &out };
  auto ss = InitializeSecurityContext(&_cred, _avail ? &_ctx : nullptr,
				      {}, req, 0, 0, inb, 0, &_ctx, &outb, &attr, {});
  _avail = true;
  switch (ss) {
  case SEC_I_COMPLETE_AND_CONTINUE: ss = SEC_I_CONTINUE_NEEDED;
  case SEC_I_COMPLETE_NEEDED: _ok(CompleteAuthToken(&_ctx, &outb));
  }
  _sendtoken((char const*)out.pvBuffer, out.cbBuffer);
  return ss;
}

void
winsock::tlsclient::_sendtoken(char const* data, size_t size)
{
  while (size) {
    auto n = _send(data, size);
    data += n, size -= n;
  }
}

size_t
winsock::tlsclient::_copyextra(size_t i, size_t size)
{
  auto n = std::min<size_t>(_extra.size(), size - i);
  CopyMemory(_buf.get() + i, _extra.data(), n);
  _extra.erase(0, n);
  return n;
}

winsock::tlsclient&
winsock::tlsclient::connect()
{
  try {
    auto ss = _extra.empty() ? _token() : SEC_I_CONTINUE_NEEDED;
    constexpr size_t buflen = 16 * 1024;
    _buf = std::unique_ptr<char[]>(new char[buflen]);
    size_t n = 0;
    while (ss == SEC_I_CONTINUE_NEEDED) {
      auto t = !_extra.empty() ? _copyextra(n, buflen) : _recv(_buf.get() + n, buflen - n);
      if (t == 0) throw error(_emsg(SEC_E_INCOMPLETE_MESSAGE));
      n += t;
      SecBuffer in[2] { { DWORD(n), SECBUFFER_TOKEN, _buf.get() } };
      SecBufferDesc inb { SECBUFFER_VERSION, 2, in };
      ss = _token(&inb);
      if (ss == SEC_E_INCOMPLETE_MESSAGE) {
	if (n < buflen) ss = SEC_I_CONTINUE_NEEDED;
      } else {
	n = 0;
	if (in[1].BufferType == SECBUFFER_EXTRA) {
	  n = in[1].cbBuffer;
	  memmove(_buf.get(), _buf.get() + in[0].cbBuffer - n, n);
	}
      }
      _ok(ss);
    }
    _extra.assign(_buf.get(), n);
    _ok(QueryContextAttributes(&_ctx, SECPKG_ATTR_STREAM_SIZES, &_sizes));
    _buf = std::unique_ptr<char[]>
      (new char[_sizes.cbHeader + _sizes.cbMaximumMessage + _sizes.cbTrailer]);
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
      DWORD value = SCHANNEL_SHUTDOWN;
      SecBuffer in { sizeof(value), SECBUFFER_TOKEN, &value };
      SecBufferDesc inb { SECBUFFER_VERSION, 1, &in };
      _ok(ApplyControlToken(&_ctx, &inb));
      while (_token() == SEC_I_CONTINUE_NEEDED) continue;
    } catch (...) {
      LOG("SSPI: shutdown error." << std::endl);
    }
    DeleteSecurityContext(&_ctx);
    _avail = false;
  }
  return *this;
}

bool
winsock::tlsclient::verify(std::string const& cn, DWORD ignore)
{
  LOG("Auth: " << cn << "(" << idn(cn) << ")... ");
  auto name = win32::wstring(idn(cn));
  PCCERT_CHAIN_CONTEXT chain;
  {
    PCCERT_CONTEXT context;
    _ok(QueryContextAttributes(&_ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &context));
    CERT_CHAIN_PARA ccp { sizeof(ccp) };
    auto ok = CertGetCertificateChain({}, context, {}, context->hCertStore, &ccp, 0, {}, &chain);
    CertFreeCertificateContext(context);
    win32::valid(ok);
  }
  CERT_CHAIN_POLICY_STATUS status { sizeof(status) };
  {
    CERT_CHAIN_POLICY_PARA policy { sizeof(policy) };
    SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl { sizeof(ssl) };
    ssl.dwAuthType = AUTHTYPE_SERVER;
    ssl.fdwChecks = ignore;
    ssl.pwszServerName = const_cast<LPWSTR>(name.c_str());
    policy.pvExtraPolicyPara = &ssl;
    auto ok = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &status);
    CertFreeCertificateChain(chain);
    win32::valid(ok);
  }
  LOG(status.dwError << std::endl);
  return !status.dwError;
}

size_t
winsock::tlsclient::recv(char* buf, size_t size)
{
  assert(_avail);
  if (!_recvq.empty()) {
    auto n = std::min<size_t>(size, _recvq.size() - _rest);
    CopyMemory(buf, _recvq.data() + _rest, n);
    _rest += n;
    if (_rest == _recvq.size()) _recvq.clear();
    return n;
  }
  _rest = 0;
  size_t done = 0;
  size_t n = 0;
  while (size && !done) {
    auto t = !_extra.empty() ?
      _copyextra(n, _sizes.cbMaximumMessage) :
      _recv(_buf.get() + n, _sizes.cbMaximumMessage - n);
    if (t == 0) {
      if (n == 0) break;
      throw error(_emsg(SEC_E_INCOMPLETE_MESSAGE));
    }
    n += t;
    SecBuffer dec[4] { { DWORD(n), SECBUFFER_DATA, _buf.get() } };
    SecBufferDesc decb { SECBUFFER_VERSION, 4, dec };
    auto ss = DecryptMessage(&_ctx, &decb, 0, {});
    if (ss == SEC_E_INCOMPLETE_MESSAGE && n < _sizes.cbMaximumMessage) continue;
    _ok(ss), n = 0;
    std::string extra;
    for (int i = 0; i < 4; ++i) {
      switch(dec[i].BufferType) {
      case SECBUFFER_DATA:
	if (dec[i].cbBuffer) {
	  size_t m = 0;
	  if (done < size) {
	    m = std::min<size_t>(size - done, dec[i].cbBuffer);
	    CopyMemory(buf + done, dec[i].pvBuffer, m);
	    done += m;
	  }
	  _recvq.append((char*)dec[i].pvBuffer + m, dec[i].cbBuffer - m);
	} else if (ss == SEC_E_OK && *_buf.get() == 0x15) {
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
winsock::tlsclient::send(char const* data, size_t size)
{
  assert(_avail);
  if (size) {
    size = std::min<size_t>(size, _sizes.cbMaximumMessage);
    SecBuffer enc[4] = {
      { _sizes.cbHeader, SECBUFFER_STREAM_HEADER, _buf.get() },
      { DWORD(size), SECBUFFER_DATA, _buf.get() + _sizes.cbHeader },
      { _sizes.cbTrailer, SECBUFFER_STREAM_TRAILER, _buf.get() + _sizes.cbHeader + size }
    };
    SecBufferDesc encb { SECBUFFER_VERSION, 4, enc };
    CopyMemory(_buf.get() + _sizes.cbHeader, data, size);
    _ok(EncryptMessage(&_ctx, 0, &encb, 0));
    _sendtoken(_buf.get(), enc[0].cbBuffer + enc[1].cbBuffer + enc[2].cbBuffer);
  }
  return size;
}

/*
 * Functions of IDN
 */
std::string
winsock::idn(std::string_view domain)
{
  for (auto c : domain) {
    if (unsigned(c) >= 0x80) return idn(win32::wstring(domain));
  }
  return std::string(domain);
}

std::string
winsock::idn(std::wstring_view domain)
{
  constexpr auto encode = [](auto name) {
    std::string ascii;
    for (auto c : name) {
      if (c < 0x80) ascii += static_cast<char>(c);
    }
    if (ascii.size() < name.size()) {
      constexpr char overflow[] = "punycode overflow";
      auto n = ascii.size();
      if (n) ascii += '-';
      size_t delta = 0, bias = 72, damp = 700;
      for (wchar_t code = 0x80; n < name.size(); ++code) {
	wchar_t next = wchar_t(-1);
	for (auto c : name) {
	  if (c >= code && c < next) next = c;
	}
	auto t = delta + (next - code) * (n + 1);
	if (t < delta) throw error(overflow);
	delta = t, code = next;
	for (size_t i = 0; i < name.size(); ++i) {
	  if (name[i] != code) {
	    if (name[i] < code && ++delta == 0) throw error(overflow);
	  } else {
	    enum { base = 36, tmin = 1, tmax = 26, skew = 38 };
	    constexpr char b36[] = "abcdefghijklmnopqrstuvwxyz0123456789";
	    auto q = delta;
	    for (size_t k = base;; k += base) {
	      size_t qd = k > bias ? std::min<size_t>(k - bias, tmax) : tmin;
	      if (q < qd) break;
	      ascii += b36[qd + (q - qd) % (base - qd)];
	      q = (q - qd) / (base - qd);
	    }
	    ascii += b36[q];
	    size_t k = 0;
	    q = delta / damp, q += q / ++n;
	    for (; q > (base - tmin) * tmax / 2; q /= base - tmin) k += base;
	    bias = k + (base - tmin + 1) * q / (q + skew);
	    delta = 0, damp = 2;
	  }
	}
	if (++delta == 0) throw error(overflow);
      }
      ascii = "xn--" + ascii;
    }
    return ascii;
  };
  std::string result;
  for (;;) {
    auto i = domain.find('.');
    if (i == domain.npos) break;
    result += encode(domain.substr(0, i)) += '.';
    domain = domain.substr(i + 1);
  }
  return result + encode(domain);
}
