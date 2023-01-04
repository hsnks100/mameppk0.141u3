. env.sh

CURR_PATH=`pwd`
SHORT_NAME=`echo $1 | sed -n 's/[a-z]\{2\}_//p'`

rm -f lang/$1/*.mmo

cd ../makelang
./$SHORT_NAME.bat
CURR_DATE=`date +%Y%m%d`
ARCHIVE_NAME=lang_ppk_$SHORT_NAME-$CURR_DATE.7z

cd $CURR_PATH/..
DIR_FILES=`find lang/$1 -type "f" | sed '/\.svn/d'`

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
