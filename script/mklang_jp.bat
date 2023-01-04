@call setpath.bat
@copy /y %PARENTPLUSSVN_ROOT%\makelang\text\catver.ini ..\makelang\text\
@copy /y %PARENTPLUSSVN_ROOT%\makelang\text\tp_*.txt ..\makelang\text\
@copy /y %PARENTPLUSSVN_ROOT%\makelang\text\jp_*.txt ..\makelang\text\
@copy /y %PARENTPLUSSVN_ROOT%\makelang\text\mame_jp.lst ..\makelang\text\
@mkdir ..\lang\ja_JP\
@copy /y %PARENTPLUSSVN_ROOT%\makelang\data\jp\*.dat ..\lang\ja_JP\
@sh makelang.sh ja_jp
@pause
