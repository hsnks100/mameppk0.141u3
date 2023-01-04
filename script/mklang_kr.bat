@call setpath.bat
@copy /y %PARENTPLUSSVN_ROOT%\makelang\text\catver.ini ..\makelang\text\
@copy /y %PARENTPLUSSVN_ROOT%\makelang\text\tp_*.txt ..\makelang\text\
@copy /y %PARENTPLUSSVN_ROOT%\makelang\text\kr_*.txt ..\makelang\text\
@copy /y %PARENTPLUSSVN_ROOT%\makelang\text\mame_kr.lst ..\makelang\text\
@mkdir ..\lang\ko_KR\
@copy /y %PARENTPLUSSVN_ROOT%\makelang\data\kr\history.dat ..\lang\ko_KR\history.dat
@sh makelang.sh ko_kr
@pause
