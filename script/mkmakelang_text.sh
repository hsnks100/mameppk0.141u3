. env.sh

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
cd $PLUSSVN_ROOT

##############
# prepare vars
##############
# awk: get the 4th column, sed: make the path relative
MAKELANG_TEXT_FILES=`find $PLUSSVN_ROOT/makelang/text -path *.svn/text-base/*.svn-base | sed -n 's/^.*\/makelang\/\(.*\)\.svn\/text-base\/\(.*\).svn-base$/makelang\/\1\2/p'`
CURR_DATE=`date +%Y%m%d`
ARCHIVE_TEXT_NAME=makelang_text-$CURR_DATE.7z

######
# pack
######
rm -f $TEMPDIR/$ARCHIVE_TEXT_NAME
7z a -mx=9 $TEMPDIR/$ARCHIVE_TEXT_NAME $MAKELANG_TEXT_FILES
mv -f $TEMPDIR/$ARCHIVE_TEXT_NAME $CURR_PATH

cd $CURR_PATH

###############
# upload files
###############
echo "About to upload $ARCHIVE_NAME..."
echo "<'Enter' to continue, any other key then 'Enter' to abort>"
read isUpload
if [ -z $isUpload ]; then
    curl -T $ARCHIVE_TEXT_NAME -u $FTP_LOGIN $FTP_URL/download/
fi
