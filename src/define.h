/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#define _WIN32_WINNT 0x0601
#include <sdkddkver.h>

#include "version.h"

#define ID_MENU_FETCH          1
#define ID_MENU_SUMMARY        2
#define ID_MENU_TRAYICON       10
#define ID_MENU_SETTINGS       11
#define ID_MENU_ALWAYSONTOP    12
#define ID_MENU_ABOUT          90
#define ID_MENU_EXIT           99

#define ID_EVENT_LOGOFF        (-1)

#define ID_TEXT_FETCHING       1
#define ID_TEXT_FETCHED_MAIL   2
#define ID_TEXT_FETCH_ERROR    3
#define ID_TEXT_BALLOON_TITLE  4
#define ID_TEXT_BALLOON_ERROR  5
#define ID_TEXT_SUMMARY_TITLE  6
#define ID_TEXT_SUMMARY_COLUMN 7
#define ID_TEXT_ABOUT          8
#define ID_TEXT_VERSION        9
