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
cd $PLUSSVN_ROOT/dirs

##############
# prepare vars
##############
# sed: filter svn files; make the path relative
DIR_FILES=`find . -type "f" | sed -e '/\.svn/d' -ne 's/^\.\///p'`
CURR_DATE=`date +%Y%m%d`
#MAME_VER=`cat ../src/version.c | sed -n 's/^.*"\([0-9]\+\.[^ ]\+\).*$/\1/p'`
ARCHIVE_NAME=mameppk_folders-$CURR_DATE.7z

######
# pack
######
rm -f $TEMPDIR/$ARCHIVE_NAME
7z a -mx=9 $TEMPDIR/$ARCHIVE_NAME $DIR_FILES
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
