@echo off
@call setpath.bat
rem @cd ..\makelang
rem @perl catver.pl
rem @call all.bat
rem @copy ..\lang\ja_JP\command.dat ..\lang\zh_CN\command.dat /y
rem @copy ..\lang\ja_JP\command.dat ..\lang\zh_TW\command.dat /y
rem @cd ..\script
@sh mkbin.sh
rem @call update_download.bat
@pause
