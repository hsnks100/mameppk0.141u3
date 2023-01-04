###############
# CLOSE ALL BACKGROUND APPS BEFORE BENCHMARKING
###############

. benchenv.sh

#sleep 10

CURR_PATH=`pwd`

rm -f $CURR_PATH/benchmark.htm

echo -e "<html>\n<head>\n<style>\n.benchmark {border-collapse:collapse; font:12px arial}\n.benchmark td {padding:2px 4px}\n</style>\n</head>\n<body>\n\n" >> $CURR_PATH/benchmark.htm
echo -e "<table border=\"1\" cellpadding=\"0\" cellspacing=\"0\" class=\"benchmark\">" >> $CURR_PATH/benchmark.htm
echo -e "<p>$PARAMS</p>" >> $CURR_PATH/benchmark.htm
#header
echo -e "  <tr>\n    <td>Game</td>\n    <td>Driver</td>" >> $CURR_PATH/benchmark.htm
for TESTBIN in $TESTBINLIST; do
    MAMEEXE=`basename $TESTBIN`
    MAMEBUILD=`echo $TESTBIN | sed -n 's/\/'"$MAMEEXE"'//p'`
    echo -e "    <td>$MAMEBUILD</td>" >> $CURR_PATH/benchmark.htm
done
echo -e "  </tr>" >> $CURR_PATH/benchmark.htm

for GAME in $GAMELIST; do
    #fill in Game and Driver fields
    TESTBIN=`echo $TESTBINLIST | awk '{ print $1}'`
    MAMEEXE=`basename $TESTBIN`
    MAMEBUILD=`echo $TESTBIN | sed -n 's/\/'"$MAMEEXE"'//p'`
    cd $TESTBINROOT/$MAMEBUILD
    DRV=`./$MAMEEXE $GAME -norc -ls | sed -n 's/.* \(.*.c\)/\1/p'`
    echo -e "  <tr>\n    <td>$GAME</td>\n    <td>$DRV</td>" >> $CURR_PATH/benchmark.htm
    #fill in FPS fields
    for TESTBIN in $TESTBINLIST; do
        MAMEEXE=`basename $TESTBIN`
        MAMEBUILD=`echo $TESTBIN | sed -n 's/\/'"$MAMEEXE"'//p'`

        echo $GAME [$MAMEBUILD]
        cd $TESTBINROOT/$MAMEBUILD

        rm -f nvram/$GAME.nv
        rm -f cfg/$GAME.cfg
        rm -f diff/$GAME.dif

        DRV=`./$MAMEEXE $GAME -norc -ls | sed -n 's/.* \(.*.c\)/\1/p'`
        if [ $DRV == "neogeo.c" ]; then
            PARAMS="-bios 1 $PARAMS"
        fi
        ./$MAMEEXE $GAME $PARAMS | sed -n 's/.*: \([0-9\.%]\+\) .*/\1/p' | awk '{ printf("    <td><b>%.1f</b></td>\n",$1) }' >> $CURR_PATH/benchmark.htm
        if [ $? -ne 0 ]; then
            echo "FAIL!"
        fi
    done
    echo -e "  </tr>" >> $CURR_PATH/benchmark.htm
done
echo -e "</table>\n</body>\n</html>" >> $CURR_PATH/benchmark.htm
