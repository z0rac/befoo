befoo - IMAP4���[���`�F�b�J�[ <http://befoo.sourceforge.jp/>

�T�v:
-----
befoo��Windows2000�ȍ~�p�̃V���v����IMAP4/POP3�N���C�A���g�ŁA������
���[���{�b�N�X�̃��[�����`�F�b�N���Č����A���M�ҁA���t�̈ꗗ��\������
���B


�C���X�g�[��:
-------------
"befoo.exe"(�ƕK�v�Ȃ�"extend.dll")���A�v���P�[�V�����t�H���_�ɃR�s�[
���Ă��������B�����N���������ꍇ�ɂ́A�X�^�[�g�A�b�v�t�H���_�ɃV���[�g
�J�b�g���쐬���Ă��������B
"extend.dll"�͐ݒ�_�C�A���O���g����悤�ɂ��܂��B���ꂪ�����ꍇ�͐ݒ�
�t�@�C�����m�[�g�p�b�h�Ŏ菑�����邱�ƂɂȂ�܂��B


�ݒ�:
-----
befoo��"befoo.ini"�t�@�C������ݒ��ǂݍ��݂܂��B���̃t�@�C���̓��[�U
���̃��[�J���uApplication Data�v�t�H���_�A�܂��́A�A�v���P�[�V�����Ɠ�
���t�H���_�ɒu���Ă��������B�Ȃ��A�����N�����ɂ́A�uApplication Data�v
�t�H���_�Ɏ����쐬����܂��B�ȉ���"befoo.ini"�̐ݒ��ł��B

[���[���{�b�N�X��]
uri=imap://username@mail.example.com/
passwd=�p�X���[�h	; ��ňÍ�������܂��B
sound=MailBeep		; �T�E���h����WAVE�t�@�C���̃p�X�B
			; (�f�t�H���g:���M���Ȃ�)
period=10		; ���P�ʂ̃��[���m�F�Ԋu�B(�f�t�H���g:15��)
ip=6			; IP�o�[�W����6�B(�f�t�H���g:0=�w��Ȃ�)
verify=1		; TLS/SSL�ؖ����̌��؃��x��(�T�[�o�[���̂�)�B
			; (�f�t�H���g:3=���S����)

[SSL���g�p]
uri=imap+ssl://username@mail.example.com/
passwd=...
sound=%windir%\\Media\\chimes.wav
verify=2		; ���ȏؖ���

[Google Apps email]
uri=imap+ssl://username%40your_domain.com@imap.gmail.com/
passwd=...

[POP3]
uri=pop://username@pop.example.com/
passwd=...

[POP3�V��]		; �V���݈̂ꗗ�\���B
uri=pop://username@pop.example.com/#recent
passwd=...

[POP3��SSL�g�p]
uri=pop+ssl://username@pop.example.com/
passwd=...

[(preferences)]		; ����ݒ�B
icon=32,50,2		; �A�C�R���̃T�C�Y�A�������A���\�[�X�ԍ��B
			; (�f�t�H���g: 64,0,1)
balloon=5,3		; �o���[���\���b���A�����B(�f�t�H���g: 10,0)
summary=5,1,20		; �ꗗ�\���b���A���[���m�F��̕\��/��\���A��
			; ����(��A�N�e�B�u��)�B(�f�t�H���g: 3,0,0)
delay=30		; 1��ڂ̃��[���m�F�܂ł̕b���B(�f�t�H���g: 0)


���C�Z���X:
-----------
GNU GPL�Ɋ�Â��ĔЕz����܂��B�ڍׂ�LICENSE.txt�����ǂ݂��������B
���{��� <http://sourceforge.jp/projects/opensource/wiki/licenses/
GNU_General_Public_License_version_3.0>


�o�O���|�[�g:
-------------
�o�O���|�[�g��v�]��SourceForge.JP�̃`�P�b�g�V�X�e���ł��A�����������B
<http://sourceforge.jp/ticket/newticket.php?group_id=3995>
