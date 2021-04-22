/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include <string>
#include <cstdio>
#include <cstdlib>

/** crc32 - CRC32 calculator
 */
namespace {
  class crc32 {
    unsigned _tab[256];
  public:
    crc32() noexcept {
      for (unsigned i = 0; i < 256; ++i) {
	auto v = i;
	for (int c = 8; c--;) {
	  v = ((int(!(v & 1)) - 1) & 0xedb88320) ^ (v >> 1);
	}
	_tab[i] = v;
      }
    }
    unsigned operator()(std::string_view s) const noexcept {
      auto v = ~0U;
      for (auto c : s) v = _tab[(v ^ c) & 255] ^ (v >> 8);
      return ~v;
    }
  };
  crc32 const crc;
}

#if MAIN

#include <iostream>
#include <iomanip>
#include <list>

namespace {
  struct elem {
    unsigned hash;
    unsigned cp;
    std::string cs;
    elem() {}
    elem(unsigned hash, unsigned cp, std::string const& cs) : hash(hash), cp(cp), cs(cs) {}
  };
}

int
main()
{
  constexpr char ws[] = "\t ";
  std::list<elem> ls;
  while (std::cin) {
    std::string s;
    getline(std::cin, s);
    auto i = s.find(';');
    if (i != s.npos) s.erase(i);
    i = s.find_first_not_of(ws);
    if (i == s.npos) continue;
    auto n = s.find_first_not_of("0123456789", i);
    if (n == s.npos || n == i) continue;
    std::string cp(s, i, n - i);
    i = s.find_first_not_of(ws, n);
    if (i == s.npos || i == n) continue;
    n = s.find_first_of(ws, i);
    std::string cs(s, i, n - i);
    for (auto& c : cs) c = static_cast<char>(std::toupper(c));
    auto hash = crc(cs);
    auto const end = ls.end();
    auto p = ls.begin();
    while (p != end && hash > p->hash) ++p;
    if (p == end || p->hash != hash) {
      ls.insert(p, elem(hash, strtoul(cp.c_str(), {}, 10), cs));
    } else {
      std::cerr << "conflict: " << cs << "(" << cp << ") and "
		<< p->cs << "(" << p->cp << ")" << std::endl;
    }
  }
  for (int cp = 1; cp < 65536; ++cp) {
    char no[10];
    sprintf(no, "%d", cp);
    constexpr char const* prefix[] { "WINDOWS-", "X-CP", "CP", "CP0", "CP00" };
    int i = sizeof(prefix) / sizeof(prefix[0]);
    i -= (cp >= 1000) + (cp >= 10000);
    while (i--) {
      auto cs = std::string(prefix[i]) + no;
      auto hash = crc(cs);
      for (auto const& t : ls) {
	if (hash > t.hash) continue;
	if (hash == t.hash && cs != t.cs) {
	  std::cerr << "conflict: " << cs << "(" << cp << ") and "
		    << t.cs << "(" << t.cp << ")" << std::endl;
	}
	break;
      }
    }
  }
  std::cout << "// This file was created by codepage.exe." << std::endl
	    << std::endl
	    << "constexpr unsigned hash[] = {" << std::endl
	    << std::hex << std::setfill('0');
  for (auto& t : ls) {
    std::cout << "  0x" << std::setw(8) << t.hash << ",\t// " << t.cs << std::endl;
  }
  std::cout << "};" << std::endl
	    << std::endl
	    << "constexpr unsigned short codepage[] = {" << std::endl
	    << std::dec << std::setfill(' ');
  for (auto& t : ls) {
    std::cout << "    " << std::setw(8) << t.cp << ",\t// " << t.cs << std::endl;
  }
  std::cout << "};" << std::endl;
  return 0;
}

#else

unsigned
codepage(std::string_view charset)
{
#include "codepage.h"
  if (charset.empty()) return 0;
  auto cs = std::string(charset);
  for (auto& c : cs) c = static_cast<char>(std::toupper(c));
  auto const k = crc(cs);
  for (size_t lo = 0, hi = sizeof(hash) / sizeof(hash[0]); lo < hi;) {
    auto i = (lo + hi) >> 1;
    if (auto diff = int(k - hash[i]); !diff) return codepage[i];
    else if (diff < 0) hi = i;
    else lo = i + 1;
  }
  if (auto i = cs.find_last_not_of("0123456789"); i < cs.size() - 1) {
    auto prefix = std::string_view(cs).substr(0, i + 1);
    for (auto t : { "WINDOWS-", "CP", "X-CP" }) {
      if (t == prefix) return strtoul(cs.c_str() + i + 1, {}, 10);
    }
  }
  return 0;
}

#endif
