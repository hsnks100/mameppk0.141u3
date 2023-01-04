. env.sh

if [[ -n $1 && $1 == "VC" ]]; then
    export BIN_SUFFIX="vc"
    export PLUS_BIN_FILES=" \
    $PLUSBIN_ROOT/vmameppkui.exe"
else
    export BIN_SUFFIX="gcc"
    export PLUS_BIN_FILES=" \
    $PLUSBIN_ROOT/mameppkui.exe"
fi

################
# test directory
################
if [ ! -d $TEMPDIR ]; then
    echo "WARNING: $TEMPDIR does not exist."
    exit
fi
if [ ! -d $PLUSSVN_ROOT ]; then
    echo "WARNING: $PLUSSVN_ROOT does not exist."
    exit
fi

CURR_PATH=`pwd`
cd $PLUSSVN_ROOT/dirs

##############
# prepare vars
##############
DIR_FILES='docs/readme_pk.txt'
CURR_DATE=`date +%Y%m%d`
MAME_VER=`cat ../src/version.c | sed -n 's/^.*"\([0-9]\+\.[^ ]\+\).*$/\1/p'`
ARCHIVE_NAME=mameppk_bin_$BIN_SUFFIX-$MAME_VER-$CURR_DATE.7z

######
# pack
######
rm -f $TEMPDIR/$ARCHIVE_NAME
#7z a -mx=9 $TEMPDIR/$ARCHIVE_NAME $PLUS_BIN_FILES . ../lang -xr!*.svn
7z a -mx=9 $TEMPDIR/$ARCHIVE_NAME $DIR_FILES $PLUS_BIN_FILES
mv -f $TEMPDIR/$ARCHIVE_NAME $CURR_PATH

cd $CURR_PATH

###############
# upload source
###############
echo "About to upload $ARCHIVE_NAME..."
echo "<'Enter' to continue, any other key then 'Enter' to abort>"
read isUpload
if [ -z $isUpload ]; then
    curl -T $ARCHIVE_NAME -u $FTP_LOGIN $FTP_URL
fi
