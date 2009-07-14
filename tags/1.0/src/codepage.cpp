/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include <string>

using namespace std;

/** crc32 - CRC32 calculator
 */
namespace {
  class crc32 {
    unsigned _tab[256];
  public:
    crc32()
    {
      for (unsigned i = 0; i < 256; ++i) {
	unsigned v = i;
	for (int c = 8; c--;) {
	  v = ((int(!(v & 1)) - 1) & 0xedb88320) ^ (v >> 1);
	}
	_tab[i] = v;
      }
    }

    unsigned operator()(const string& s) const
    {
      unsigned v = ~0U;
      for (const char* p = s.c_str(); *p; ++p) {
	v = _tab[(v ^ *p) & 255] ^ (v >> 8);
      }
      return ~v;
    }
  };
  const crc32 crc;
}

#if MAIN

#include <iostream>
#include <iomanip>
#include <list>

namespace {
  struct elem {
    unsigned hash;
    unsigned cp;
    string cs;
    elem() {}
    elem(unsigned hash, unsigned cp, const string& cs)
      : hash(hash), cp(cp), cs(cs) {}
  };
}

int
main()
{
  static const char ws[] = " \t";
  list<elem> ls;
  while (cin) {
    string s;
    getline(cin, s);
    string::size_type i = s.find_first_of(';');
    if (i != string::npos) s.erase(i);
    i = s.find_first_not_of(ws);
    if (i == string::npos) continue;
    string::size_type n = s.find_first_not_of("0123456789", i);
    if (n == string::npos || n == i) continue;
    string cp(s, i, n - i);
    i = s.find_first_not_of(ws, n);
    if (i == string::npos || i == n) continue;
    n = s.find_first_of(ws, i);
    string cs(s, i, n - i);
    for (string::iterator p = cs.begin(); p != cs.end(); ++p) {
      *p = static_cast<char>(toupper(*p));
    }
    unsigned hash = crc(cs);
    list<elem>::iterator p = ls.begin();
    while (p != ls.end() && hash > p->hash) ++p;
    if (p == ls.end() || p->hash != hash) {
      char* e;
      ls.insert(p, elem(hash, strtoul(cp.c_str(), &e, 10), cs));
    } else {
      cerr << "conflict: " << cs << "(" << cp << ") and "
	   << p->cs << "(" << p->cp << ")" << endl;
    }
  }
  for (int cp = 1; cp < 65536; ++cp) {
    char no[10];
    sprintf(no, "%ld", cp);
    static const char* prefix[] = {
      "WINDOWS-", "X-CP", "CP", "CP0", "CP00"
    };
    int i = sizeof(prefix) / sizeof(prefix[0]);
    i -= (cp >= 1000) + (cp >= 10000);
    while (i--) {
      string cs = string(prefix[i]) + no;
      unsigned hash = crc(cs);
      list<elem>::const_iterator p = ls.begin();
      while (p != ls.end() && hash > p->hash) ++p;
      if (p != ls.end() && p->hash == hash && p->cs != cs) {
	cerr << "conflict: " << cs << "(" << cp << ") and "
	     << p->cs << "(" << p->cp << ")" << endl;
      }
    }
  }
  cout << "// This file was created by codepage.exe." << endl
       << endl
       << "static const unsigned hash[] = {" << endl
       << hex << setfill('0');
  for (list<elem>::iterator p = ls.begin(); p != ls.end(); ++p) {
    cout << "  0x" << setw(8) << p->hash << ",\t// " << p->cs << endl;
  }
  cout << "};" << endl
       << endl
       << "static const unsigned short codepage[] = {" << endl
       << dec << setfill(' ');
  for (list<elem>::iterator p = ls.begin(); p != ls.end(); ++p) {
    cout << "    " << setw(8) << p->cp << ",\t// " << p->cs << endl;
  }
  cout << "};" << endl;
  return 0;
}

#else

unsigned
codepage(const string& charset)
{
#include "codepage.h"
  const unsigned k = crc(charset);
  int lo = 0, hi = sizeof(hash) / sizeof(hash[0]);
  while (lo < hi) {
    int i = (lo + hi) >> 1;
    int diff = int(k - hash[i]);
    if (!diff) return codepage[i];
    if (diff < 0) hi = i;
    else lo = i + 1;
  }
  string::size_type i = charset.find_last_not_of("0123456789");
  if (i < charset.size() - 1) {
    string prefix(charset, 0, i + 1);
    if (prefix == "WINDOWS-" || prefix == "CP" ||  prefix == "X-CP") {
      char* e;
      return strtoul(charset.c_str() + i + 1, &e, 10);
    }
  }
  return 0;
}

#endif
