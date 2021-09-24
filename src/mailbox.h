/* -*- mode: c++ -*-
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#pragma once

#include <ctime>
#include <exception>
#include <list>
#include <memory>
#include <string>
#include <functional>
#include <mutex>

class tokenizer {
protected:
  std::string _s;
  using size_type = decltype(_s)::size_type;
  size_type _next = 0;
  auto uppercase(size_type to) const
  { return uppercase(std::string_view(_s).substr(_next, to - _next)); }
  auto findf(char const* s) const { return _s.find_first_of(s, _next); }
  auto findf(char const* s, size_type pos) const
  { return _s.find_first_of(s, pos); }
  auto findq(char const* s) const { return findq(s, _next); }
  size_type findq(char const* s, size_type pos) const;
public:
  tokenizer() {}
  tokenizer(std::string const& s) : _s(s) {}
  explicit operator bool() const noexcept { return _next < _s.size(); }
  auto& data() const noexcept { return _s; }
  auto remain() const noexcept
  { return std::string_view(_s).substr(std::min<size_type>(_next, _s.size())); }
  int peek() const { return _next < _s.size() ? _s[_next] & 255 : -1; }

  static bool digit(std::string_view s, int* value = {}) noexcept;
  static std::string uppercase(std::string_view s);
};

class mail {
  std::string _uid;
  std::string _subject;            // The subject of this mail.
  std::pair<std::string, std::string> _from; // name of sender, and a header line.
  time_t _date;               // The received date and time.
public:
  mail() {}
  mail(std::string const& uid) : _uid(uid) {}
  auto& uid() const noexcept { return _uid; }
  auto& subject() const noexcept { return _subject; }
  auto& from() const noexcept { return _from; }
  auto date() const noexcept { return _date; }
  auto& sender() const noexcept { return _from.first; }
  bool header(std::string const& headers);
public:
  // decoder - parse and decode a message.
  class decoder : public tokenizer {
  protected:
    static std::string_view trim(std::string_view text);
    std::string eword(std::string::size_type to);
    static std::string eword(std::string_view text, bool unescape);
    static std::string eword(std::string_view text);
    static std::string decodeB(std::string_view text);
    static std::string decodeQ(std::string_view text);
    std::string_view token(bool comment = false);
  public:
    decoder() {}
    decoder(std::string const& s) : tokenizer(s) {}
    std::pair<int, std::string> field(std::initializer_list<char const*> names);
    std::string unstructured() { return eword(_s.size()); }
    std::pair<std::string, std::string> address();
    time_t date();
  };
};

class uri {
  std::string _part[6];
public:
  uri() {}
  explicit uri(std::string_view uri);
  operator std::string() const;
  enum component { scheme, user, host, port, path, fragment };
  auto& operator[](component i) { return _part[i]; }
  auto& operator[](component i) const { return _part[i]; }
};

class mailbox {
  mailbox* _next = {};
  std::string _name;
  uri _uri;
  std::string _passwd;
  int _domain = 0;
  int _verify = 0;
  std::mutex mutable _mutex;
  std::list<mail> _mails;
  int _recent = 0;
  std::list<std::string> _ignore;
public:
  mailbox(std::string const& name = {}) : _name(name) {}
  virtual ~mailbox() {}
  auto const* next() const noexcept { return _next; }
  auto* next() noexcept { return _next; }
  auto* next(mailbox* next) noexcept { return _next = next; }
  auto& name() const noexcept { return _name; }
  std::string uristr() const { return _uri; }
  mailbox& uripasswd(std::string const& uri, std::string const& passwd);
  mailbox& domain(int domain) noexcept { return _domain = domain, *this; }
  mailbox& verify(int verify) noexcept { return _verify = verify, *this; }
  auto lock() const { return std::unique_lock(_mutex); }
  auto& mails() const noexcept { return _mails; }
  auto const& mails(std::list<mail>& mails) { return _mails.swap(mails), _mails; }
  auto recent() const noexcept { return _recent; }
  mail const* find(std::string const& uid) const;
  auto& ignore() const noexcept { return _ignore; }
  auto const& ignore(std::list<std::string>& ignore)
  { return _ignore.swap(ignore), _ignore; }
  void fetchmail(bool idle = false);
  void exit() noexcept { if (_backend) _backend->disconnect(); }
public:
  class backend {
    class _stream {
    public:
      virtual ~_stream() {}
      virtual void disconnect() noexcept = 0;
      virtual size_t read(char* buf, size_t size) = 0;
      virtual size_t write(char const* data, size_t size) = 0;
      virtual bool tls() const noexcept = 0;
      virtual _stream* starttls(std::string const& host) = 0;
    };
    std::unique_ptr<_stream> _st;
  protected:
    auto tls() const noexcept { return _st->tls(); }
    void starttls(std::string const& host);
    std::string read(size_t size);
    std::string read();
    void write(char const* data, size_t size);
    void write(std::string const& data);
  public:
    using stream = _stream;
    virtual ~backend() {}
    void tcp(std::string const& host, std::string const& port, int domain, int verify);
    void ssl(std::string const& host, std::string const& port, int domain, int verify);
    void disconnect() noexcept { if (_st.get()) _st->disconnect(); }
    virtual bool login(uri const& uri, std::string const& passwd) = 0;
    virtual void logout() = 0;
    virtual size_t fetch(mailbox& mbox, uri const& uri) = 0;
    virtual size_t fetch(mailbox&) { return 0; }
  };
  virtual void fetching(bool idle) = 0;
private:
  backend* _backend = {};
public:
  class error : public std::exception {
    std::string _msg;
  public:
    error(char const* msg) : _msg(msg) {}
    error(std::string const& msg) : _msg(msg) {}
    char const* what() const noexcept override { return _msg.c_str(); }
    std::string const& msg() const { return _msg; }
  };
  class silent : public error {
  public:
    silent() : error("silent") {}
  };
};
