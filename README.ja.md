| [English](README.md) | Japanese |
|----------------------|----------|

概要
====
[befoo](https://github.com/z0rac/befoo)はWindows用のシンプルなIMAP4/POP3クライアントです。

複数のメールボックスのメールをチェックして件名、送信者、日付の一覧を表示します。

インストール
------------
"befoo.exe"(と必要なら"extend.dll")をアプリケーションフォルダにコピー
してください。
自動起動したい場合には、スタートアップフォルダにショートカットを作成してください。

"extend.dll"は設定ダイアログを使えるようにします。
これが無い場合は設定ファイルをノートパッドで手書きすることになります。

設定
----
befooは"befoo.ini"ファイルから設定を読み込みます。
このファイルはユーザ毎のローカル「Application Data」フォルダ、または、アプリケーションと同
じフォルダに置いてください。

なお、初期起動時には、「Application Data」フォルダに自動作成されます。

以下は"befoo.ini"の設定例です。
```
[メールボックス名]
uri=imap://username@mail.example.com/
passwd=パスワード	; 後で暗号化されます。
sound=MailBeep		; サウンド名かWAVEファイルのパス。(デフォルト:着信音なし)
period=10		; 分単位のメール確認間隔。(デフォルト:15分)
ip=6			; IPバージョン6。(デフォルト:0=指定なし)
verify=1		; TLS/SSL証明書の検証レベル(サーバー名のみ)。(デフォルト:3=完全検証)

[SSLを使用]
uri=imap+ssl://username@mail.example.com/
passwd=...
sound=%windir%\\Media\\chimes.wav
verify=2		; 自己証明書

[Google Apps email]
uri=imap+ssl://username%40your_domain.com@imap.gmail.com/
passwd=...

[POP3]
uri=pop://username@pop.example.com/
passwd=...

[POP3新着]		; 新着のみ一覧表示。
uri=pop://username@pop.example.com/#recent
passwd=...

[POP3でSSL使用]
uri=pop+ssl://username@pop.example.com/
passwd=...

[(preferences)]		; 動作設定。
icon=32,50,2		; アイコンのサイズ、透明率、リソース番号。(デフォルト: 64,0,1)
balloon=5,3		; バルーン表示秒数、件数。(デフォルト: 10,0)
summary=5,1,20		; 一覧表示秒数、メール確認後の表示/非表示、透明率(非アクティブ時)。(デフォルト: 3,0,0)
delay=30		; 1回目のメール確認までの秒数。(デフォルト: 0)
```

ライセンス
----------
GNU GPL version3 \([日本語訳](https://licenses.opensource.jp/GPL-3.0/GPL-3.0.html)\)に基づいて頒布されます。
詳細は[LICENSE.txt](LICENSE.txt)をお読みください。
