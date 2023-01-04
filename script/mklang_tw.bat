@call setpath.bat
@copy /y %PARENTPLUSSVN_ROOT%\makelang\text\catver.ini ..\makelang\text\
@copy /y %PARENTPLUSSVN_ROOT%\makelang\text\tp_*.txt ..\makelang\text\
@copy /y %PARENTPLUSSVN_ROOT%\makelang\text\tw_*.txt ..\makelang\text\
@copy /y %PARENTPLUSSVN_ROOT%\makelang\text\mame_tw.lst ..\makelang\text\
@mkdir ..\lang\zh_TW\
@copy /y %PARENTPLUSSVN_ROOT%\makelang\data\jp\command.dat ..\lang\zh_TW\command.dat
@sh makelang.sh zh_tw
@pause
