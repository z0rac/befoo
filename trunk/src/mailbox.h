#ifndef H_MAILBOX /* -*- mode: c++ -*- */
/*
 * Copyright (C) 2009-2012 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#define H_MAILBOX

#include <ctime>
#include <exception>
#include <list>
#include <memory>
#include <string>

using namespace std;

class tokenizer {
protected:
  string _s;
  string::size_type _next;
  string uppercase(string::size_type to) const
  { return uppercase(_s.substr(_next, to - _next)); }
  string::size_type findf(const char* s) const
  { return _s.find_first_of(s, _next); }
  string::size_type findf(const char* s, string::size_type pos) const
  { return _s.find_first_of(s, pos); }
  string::size_type findq(const char* s) const { return findq(s, _next); }
  string::size_type findq(const char* s, string::size_type pos) const;
public:
  tokenizer() : _next(0) {}
  tokenizer(const string& s) : _s(s), _next(0) {}
  operator bool() const { return _next < _s.size(); }
  const string& data() const { return _s; }
  string remain() const { return *this ? _s.substr(_next) : string(); }
  int peek() const { return _next < _s.size() ? _s[_next] & 255 : -1; }

  static bool digit(const string& s, int& value);
  static bool digit(const string& s) { int v; return digit(s, v); }
  static string uppercase(string s);
};

class mail {
  string _uid;
  string _subject;            // The subject of this mail.
  pair<string, string> _from; // The 1st is name of sender, and the 2nd is a header line.
  time_t _date;               // The received date and time.
public:
  mail() {}
  mail(const string& uid) : _uid(uid) {}
  const string& uid() const { return _uid; }
  const string& subject() const { return _subject; }
  const pair<string, string>& from() const { return _from; }
  time_t date() const { return _date; }
  const string& sender() const { return _from.first; }
  bool header(const string& headers);
public:
  // decoder - parse and decode a message.
  class decoder : public tokenizer {
    string _field;
  protected:
    static string trim(const string& text);
    string eword(string::size_type to);
    static string eword(const string& text, bool unescape);
    static string eword(const string& text,
			string::size_type pos = 0,
			string::size_type end = string::npos);
    static string decodeB(const string& text);
    static string decodeQ(const string& text);
    string token(bool comment = false);
  public:
    decoder() {}
    decoder(const string& s) : tokenizer(s) {}
    int field(const char* names);
    const string& field() const { return _field; }
    string unstructured() { return eword(_s.size()); }
    pair<string, string> address();
    time_t date();
  };
};

class uri {
  string _part[6];
public:
  uri() {}
  explicit uri(const string& uri);
  operator string() const;
  string& operator[](int i) { return _part[i]; }
  const string& operator[](int i) const { return _part[i]; }
  enum { scheme, user, host, port, path, fragment };
public:
  string hostname() const;
  string mailbox() const;
};

class mailbox {
  mailbox* _next;
  string _name;
  uri _uri;
  string _passwd;
  int _domain;
  int _verify;
  list<mail> _mails;
  int _recent;
  list<string> _ignore;
public:
  mailbox(const string& name = string())
    : _next(NULL), _name(name), _domain(0), _verify(0), _recent(0) {}
  virtual ~mailbox() {}
  const mailbox* next() const { return _next; }
  mailbox* next() { return _next; }
  mailbox* next(mailbox* next) { return _next = next; }
  const string& name() const { return _name; }
  string uristr() const { return _uri; }
  mailbox& uripasswd(const string& uri, const string& passwd);
  mailbox& domain(int domain) { _domain = domain; return *this; }
  mailbox& verify(int verify) { _verify = verify; return *this; }
  const list<mail>& mails() const { return _mails; }
  const list<mail>& mails(list<mail>& mails)
  { return _mails.swap(mails), _mails; }
  int recent() const { return _recent; }
  const mail* find(const string& uid) const;
  const list<string>& ignore() const { return _ignore; }
  const list<string>& ignore(list<string>& ignore)
  { return _ignore.swap(ignore), _ignore; }
  void fetchmail();
public:
  class backend {
    class _stream {
    public:
      virtual ~_stream() {}
      virtual size_t read(char* buf, size_t size) = 0;
      virtual size_t write(const char* data, size_t size) = 0;
      virtual int tls() const = 0;
      virtual _stream* starttls(const string& host) = 0;
    };
    auto_ptr<_stream> _st;
  protected:
    int tls() const { return _st->tls(); }
    void starttls(const string& host);
    string read(size_t size);
    string read();
    void write(const char* data, size_t size);
    void write(const string& data);
  public:
    typedef _stream stream;
    virtual ~backend() {}
    void tcp(const string& host, const string& port, int domain, int verify);
    void ssl(const string& host, const string& port, int domain, int verify);
    virtual void login(const uri& uri, const string& passwd) = 0;
    virtual void logout() = 0;
    virtual size_t fetch(mailbox& mbox, const uri& uri) = 0;
  };
public:
  class error : public exception {
    string _msg;
  public:
    error(const char* msg) : _msg(msg) {}
    error(const string& msg) : _msg(msg) {}
    ~error() throw() {}
    const char* what() const throw() { return _msg.c_str(); }
    const string& msg() const { return _msg; }
  };
};

#endif
