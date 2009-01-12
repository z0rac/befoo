#ifndef H_MAILBOX /* -*- mode: c++ -*- */
/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
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
  static bool digit(const string& s, int& value);
  static string uppercase(string s);
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

class mailbox {
  mailbox* _next;
  string _name;
  struct { string proto, user, host, port, path; } _uri;
  string _passwd;
  list<mail> _mails;
  int _recent;
  list<string> _ignore;
public:
  mailbox(const string& name = string())
    : _next(NULL), _name(name), _recent(0) {}
  virtual ~mailbox() {}
  const mailbox* next() const { return _next; }
  mailbox* next() { return _next; }
  mailbox* next(mailbox* next) { return _next = next; }
  bool uri(const string& uri, const string& passwd = string());
  const string& name() const { return _name; }
  const list<mail>& mails() const { return _mails; }
  const list<mail>& mails(const list<mail>& mails)
  { return _mails = mails; }
  int recent() const { return _recent; }
  const mail* find(const string& uid) const;
  const list<string>& ignore() const { return _ignore; }
  const list<string>& ignore(const list<string>& ignore)
  { return _ignore = ignore; }
  void fetchmail();
public:
  class backend {
    class _stream {
    public:
      virtual ~_stream() {}
      virtual int open(const string& host, const string& port) = 0;
      virtual void close() = 0;
      virtual void read(char* buf, size_t size) = 0;
      virtual void write(const char* data, size_t size) = 0;
    };
    auto_ptr<_stream> _st;
  protected:
    int tls() const;
    void starttls();
    string read(size_t size);
    string readline();
    void write(const char* data, size_t size) { _st->write(data, size); }
    void write(const string& data) { _st->write(data.data(), data.size()); }
  public:
    typedef _stream stream;
    virtual ~backend() {}
    void tcp(const string& host, const string& port);
    void ssl(const string& host, const string& port);
    virtual void login(const string& user, const string& passwd) = 0;
    virtual void logout() = 0;
    virtual int fetch(mailbox& mbox, const string& path) = 0;
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
