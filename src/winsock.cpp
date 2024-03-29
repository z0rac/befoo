/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "stdafx.h"

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
  return "winsock error #" + win32::digit(WSAGetLastError());
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
winsock::tcpclient::shutdown() noexcept
{
  if (auto socket = _socket; socket != INVALID_SOCKET) {
    _socket = INVALID_SOCKET;
    ::shutdown(socket, SD_BOTH);
    for (char t[32]; ::recv(socket, t, sizeof(t), 0) > 0;) continue;
    closesocket(socket);
  }
  return *this;
}

size_t
winsock::tcpclient::recv(char* buf, size_t size)
{
  auto n = ::recv(_socket, buf, static_cast<int>(min(size, INT_MAX)), 0);
  if (n >= 0) return n;
  switch (WSAGetLastError()) {
  case WSAEWOULDBLOCK: return 0;
  case WSAETIMEDOUT: throw timedout();
  }
  throw error();
}

size_t
winsock::tcpclient::send(char const* data, size_t size)
{
  auto n = ::send(_socket, data, static_cast<int>(min(size, INT_MAX)), 0);
  if (n >= 0) return n;
  switch (WSAGetLastError()) {
  case WSAEWOULDBLOCK: return 0;
  case WSAETIMEDOUT: throw timedout();
  }
  throw error();
}

winsock::tcpclient&
winsock::tcpclient::timeout(int sec)
{
  assert(_socket != INVALID_SOCKET);
  auto nb = u_long(sec < 0);
  if (ioctlsocket(_socket, FIONBIO, &nb) != 0) throw error();
  if (sec >= 0) {
    DWORD ms = sec * 1000;
    if (setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, LPCSTR(&ms), sizeof(ms)) != 0 ||
	setsockopt(_socket, SOL_SOCKET, SO_SNDTIMEO, LPCSTR(&ms), sizeof(ms)) != 0) {
      throw error();
    }
  }
  return *this;
}

/*
 * Functions of the class winsock::tlsclient
 */
winsock::tlsclient::tlsclient()
{
  SEC_CHAR pkg[] = UNISP_NAME;
  SCHANNEL_CRED auth { SCHANNEL_CRED_VERSION };
  auth.dwFlags = (SCH_CRED_USE_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION);
  _ok(AcquireCredentialsHandle({}, pkg, SECPKG_CRED_OUTBOUND, {}, &auth, {}, {}, &_cred, {}));
}

winsock::tlsclient::~tlsclient()
{
  assert(!_ctx);
  FreeCredentialsHandle(&_cred);
}

class winsock::tlsclient::error : public winsock::error {
public:
  error(SECURITY_STATUS ss) : winsock::error("SSPI error #0x" + win32::hexdigit(ss)) {}
};

SECURITY_STATUS
winsock::tlsclient::_ok(SECURITY_STATUS ss) const
{
  if (FAILED(ss)) throw error(ss);
  return ss;
}

SECURITY_STATUS
winsock::tlsclient::_init(SecBufferDesc* inb)
{
  constexpr ULONG req = (ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT |
			 ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR |
			 ISC_REQ_STREAM | ISC_REQ_ALLOCATE_MEMORY |
			 ISC_REQ_USE_SUPPLIED_CREDS); // for Win2kPro
  ULONG attr;
  struct obuf : public SecBuffer {
    obuf() : SecBuffer{ 0, SECBUFFER_TOKEN } {}
    ~obuf() { if (pvBuffer) FreeContextBuffer(pvBuffer); }
  } out;
  SecBufferDesc outb { SECBUFFER_VERSION, 1, &out };
  auto ss = InitializeSecurityContext(&_cred, _ctx, {}, req, 0, 0,
				      inb, 0, &_ctxb, &outb, &attr, {});
  _ctx = &_ctxb;
  switch (ss) {
  case SEC_I_COMPLETE_AND_CONTINUE: ss = SEC_I_CONTINUE_NEEDED;
  case SEC_I_COMPLETE_NEEDED: _ok(CompleteAuthToken(_ctx, &outb));
  }
  _send(LPCSTR(out.pvBuffer), out.cbBuffer);
  return ss;
}

void
winsock::tlsclient::_send(char const* data, size_t size)
{
  while (size) {
    auto n = sendlo(data, size);
    data += n, size -= n;
  }
}

winsock::tlsclient&
winsock::tlsclient::connect()
{
  try {
    size_t n = 0;
    if (_rbuf.empty()) _rbuf.resize(16 * 1024);
    for (auto ss = _init(); ss == SEC_I_CONTINUE_NEEDED;) {
      if (_remain) n = _remain, _remain = 0;
      else if (auto t = recvlo(_rbuf.data() + n, _rbuf.size() - n); t) n += t;
      else throw error(SEC_E_INCOMPLETE_MESSAGE);
      SecBuffer in[2] { { DWORD(n), SECBUFFER_TOKEN, _rbuf.data() } };
      SecBufferDesc inb { SECBUFFER_VERSION, 2, in };
      if (ss = _init(&inb); ss == SEC_E_INCOMPLETE_MESSAGE) {
	if (n < _rbuf.size()) ss = SEC_I_CONTINUE_NEEDED;
      } else if (in[1].BufferType == SECBUFFER_EXTRA) {
	_remain = in[1].cbBuffer;
	MoveMemory(_rbuf.data(), _rbuf.data() + in[0].cbBuffer - _remain, _remain);
      } else n = 0;
      _ok(ss);
    }
    _ok(QueryContextAttributes(_ctx, SECPKG_ATTR_STREAM_SIZES, &_sizes));
    _rbuf.resize(_sizes.cbHeader + _sizes.cbMaximumMessage + _sizes.cbTrailer);
  } catch (...) {
    shutdown();
    throw;
  }
  return *this;
}

winsock::tlsclient&
winsock::tlsclient::shutdown() noexcept
{
  if (_ctx) {
    _recvq.clear(), _remain = 0;
    if (availlo()) {
      try {
	DWORD value = SCHANNEL_SHUTDOWN;
	SecBuffer in { sizeof(value), SECBUFFER_TOKEN, &value };
	SecBufferDesc inb { SECBUFFER_VERSION, 1, &in };
	_ok(ApplyControlToken(_ctx, &inb));
	while (_init() == SEC_I_CONTINUE_NEEDED) continue;
      } catch (...) {
	LOG("SSPI: shutdown error." << std::endl);
      }
    }
    _ctx = {};
    DeleteSecurityContext(&_ctxb);
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
    _ok(QueryContextAttributes(_ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &context));
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
  assert(_ctx);
  if (!_recvq.empty()) {
    auto n = min(size, _recvq.size() - _rest);
    CopyMemory(buf, _recvq.data() + _rest, n);
    _rest += n;
    if (_rest == _recvq.size()) _recvq.clear();
    return n;
  }
  _rest = 0;
  size_t done = 0;
  for (size_t n = 0; size && !done;) {
    if (_remain) n = _remain, _remain = 0;
    else if (auto t = recvlo(_rbuf.data() + n, _sizes.cbMaximumMessage - n); t) n += t;
    else if (n == 0) break;
    else throw error(SEC_E_INCOMPLETE_MESSAGE);
    SecBuffer dec[4] { { DWORD(n), SECBUFFER_DATA, _rbuf.data() } };
    SecBufferDesc decb { SECBUFFER_VERSION, 4, dec };
    auto ss = DecryptMessage(_ctx, &decb, 0, {});
    if (ss == SEC_E_INCOMPLETE_MESSAGE && n < _sizes.cbMaximumMessage) continue;
    _ok(ss), n = 0;
    for (auto const& sec : dec) {
      switch(sec.BufferType) {
      case SECBUFFER_DATA:
	if (sec.cbBuffer) {
	  size_t m = 0;
	  if (done < size) {
	    m = min(size - done, sec.cbBuffer);
	    CopyMemory(buf + done, sec.pvBuffer, m);
	    done += m;
	  }
	  _recvq.append(LPCSTR(sec.pvBuffer) + m, sec.cbBuffer - m);
	} else if (ss == SEC_E_OK && _rbuf[0] == 0x15) {
	  ss = SEC_I_CONTEXT_EXPIRED; // for Win2kPro
	}
	break;
      case SECBUFFER_EXTRA:
	MoveMemory(_rbuf.data() + _remain, sec.pvBuffer, sec.cbBuffer);
	_remain += sec.cbBuffer;
	break;
      }
    }
    if (ss == SEC_I_CONTEXT_EXPIRED && !done) {
      throw error(SEC_E_CONTEXT_EXPIRED);
    }
    if (ss == SEC_I_RENEGOTIATE) connect();
  }
  return done;
}

size_t
winsock::tlsclient::send(char const* data, size_t size)
{
  assert(_ctx);
  if (!size) return size;
  size = min(size, _sizes.cbMaximumMessage);
  std::vector<char> buf(_sizes.cbHeader + size + _sizes.cbTrailer);
  SecBuffer enc[4] = {
    { _sizes.cbHeader, SECBUFFER_STREAM_HEADER, buf.data() },
    { DWORD(size), SECBUFFER_DATA, LPSTR(enc[0].pvBuffer) + enc[0].cbBuffer },
    { _sizes.cbTrailer, SECBUFFER_STREAM_TRAILER, LPSTR(enc[1].pvBuffer) + enc[1].cbBuffer }
  };
  SecBufferDesc encb { SECBUFFER_VERSION, 4, enc };
  CopyMemory(buf.data() + _sizes.cbHeader, data, size);
  _ok(EncryptMessage(_ctx, 0, &encb, 0));
  _send(buf.data(), enc[0].cbBuffer + enc[1].cbBuffer + enc[2].cbBuffer);
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
    struct overflow : winsock::error {
      overflow() : winsock::error("punycode overflow") {}
    };
    std::string ascii;
    for (auto c : name) {
      if (c < 0x80) ascii += static_cast<char>(c);
    }
    if (ascii.size() < name.size()) {
      auto n = ascii.size();
      if (n) ascii += '-';
      size_t delta = 0, bias = 72, damp = 700;
      for (wchar_t code = 0x80; n < name.size(); ++code) {
	wchar_t next = wchar_t(-1);
	for (auto c : name) {
	  if (c >= code && c < next) next = c;
	}
	auto t = delta + (next - code) * (n + 1);
	if (t < delta) throw overflow();
	delta = t, code = next;
	for (size_t i = 0; i < name.size(); ++i) {
	  if (name[i] != code) {
	    if (name[i] < code && ++delta == 0) throw overflow();
	  } else {
	    enum { base = 36, tmin = 1, tmax = 26, skew = 38 };
	    constexpr char b36[] = "abcdefghijklmnopqrstuvwxyz0123456789";
	    auto q = delta;
	    for (size_t k = base;; k += base) {
	      size_t qd = k > bias ? min(k - bias, tmax) : tmin;
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
	if (++delta == 0) throw overflow();
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
