| English | [Japanese](README.ja.md) |
|---------|--------------------------|

Introduction
============
[Befoo](https://github.com/z0rac/befoo) is a simple IMAP4/POP3 client for Windows.

It fetchesmail from plural mailbox, and shows summary with subject, sender and date.

Installation
------------
You will copy the file "befoo.exe" to your application folder.
If you want it to start automatically, right-click to select the settings menu, and check "Register in Startup" in the dialog that opens.

The previous version of "extend.dll" has been merged into "befoo.exe" and is no longer needed.

Settings
--------
Befoo will read your settings from the file "befoo.ini".
This file will be located in the user's local application data folder, or the same folder
with the application.

At initial startup, it will be created in the local application data folder.

Each setting item can also be configured in the "Settings" dialog.

See below an example for "befoo.ini":

```
[mailboxname]
uri=imap://username@imap.example.com/
passwd=your password	; It will be coded later.
sound=MailBeep		; A sound alias or path to a wave file. (default: No sound)
period=10,1		; Fetching emails every 10 minutes with fetching immediately if possible. (default: 15,1 meaning "immediately")
ip=6			; IP version 6. (default: 0 meaning "unspecified")
verify=1		; Ability to verify certificate, only server name. (default: 3 meaning "full")

[another over SSL]
uri=imap+ssl://username@imap.example.com/
passwd=...
sound=%windir%\\Media\\chimes.wav
verify=2		; Allow self certification.

[Google Apps email]
uri=imap+ssl://username%40your_domain.com@imap.gmail.com/
passwd=...
period=5,0		; Fetching emails every 5 minutes without fetching immediately.

[POP3]
uri=pop://username@pop.example.com/
passwd=...

[POP3 recents]		; Summary of recents only.
uri=pop://username@pop.example.com/#recent
passwd=...

[POP3 over SSL]
uri=pop+ssl://username@pop.example.com/
passwd=...

[(preferences)]
icon=32,50,2		; The mascot icon size, transparency and resource number. (default: 64,0,1)
balloon=5,3		; Period and subjects to show the balloon. (default: 10,0)
summary=5,1,20		; Period to show the summary, switch to show the summary when mail is fetched, and the inactive summary transparency. (default: 3,0,0)
delay=30		; Delay seconds to the first fetching. (default: 0)
```

Licensing
---------
This product is distributed under the GNU GPL version 3.
Please read through the file [LICENSE.txt](LICENSE.txt) for more information.
