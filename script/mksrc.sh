. env.sh

################
# test directory
################
if [ ! -d $TEMPDIR ]; then
    echo "WARNING: $TEMPDIR does not exist."
    exit
fi
if [ ! -d $MAMESRC_ROOT ]; then
    echo "WARNING: $MAMESRC_ROOT does not exist."
    exit
fi
if [ ! -d $PLUSSVN_ROOT ]; then
    echo "WARNING: $PLUSSVN_ROOT does not exist."
    exit
fi

CURR_PATH=`pwd`
cd $PLUSSVN_ROOT

##############
# prepare vars
##############
# awk: get the 4th column, sed: make the path relative
SRC_FILES=`find $PLUSSVN_ROOT/src -path *.svn/text-base/*.svn-base | sed -n 's/^.*\/src\/\(.*\)\.svn\/text-base\/\(.*\).svn-base$/src\/\1\2/p'`
DIFF_FILES=`for file in $SRC_FILES; do diff -qN $MAMESRC_ROOT/$file $PLUSSVN_ROOT/$file > nul || echo $file; done`
CURR_DATE=`date +%Y%m%d`
MAME_VER=`cat src/version.c | sed -n 's/^.*"\([0-9]\+\.[^ ]\+\).*$/\1/p'`
ARCHIVE_NAME=mameppk_src-$MAME_VER-$CURR_DATE.7z

######
# pack
######
rm -f $TEMPDIR/$ARCHIVE_NAME
7z a -mx=9 $TEMPDIR/$ARCHIVE_NAME $PLUSSRC_ROOT_FILES $DIFF_FILES -xr!*.png
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
