/*
 * Copyright (C) 2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "define.h"
#include "winsock.h"
#include "win32.h"
#include "window.h"
#include "mailbox.h"
#include "setting.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <exception>
#include <memory>
#include <string>
#include <list>
#include <vector>
#include <cassert>

#ifdef _DEBUG
#include <iostream>
#define DBG(s) s
#define LOG(s) (std::cout << win32::time(time({})) << "|" << s)
#else
#define DBG(s)
#define LOG(s)
#endif
