@call setpath.bat
@copy /y %PARENTPLUSSVN_ROOT%\makelang\text\catver.ini ..\makelang\text\
@copy /y %PARENTPLUSSVN_ROOT%\makelang\text\tp_*.txt ..\makelang\text\
@copy /y %PARENTPLUSSVN_ROOT%\makelang\text\cn_*.txt ..\makelang\text\
@copy /y %PARENTPLUSSVN_ROOT%\makelang\text\mame_cn.lst ..\makelang\text\
@mkdir ..\lang\zh_CN\
@copy /y %PARENTPLUSSVN_ROOT%\makelang\data\jp\command.dat ..\lang\zh_CN\command.dat
@sh makelang.sh zh_cn
@pause
