#!/bin/sh
# simple configurable build script for kernel modules

xtblk='\e[0;30m' # Black - Regular
txtred='\e[0;31m' # Red
txtgrn='\e[0;32m' # Green
txtblu='\e[0;34m' # Blue
txtwht='\e[0;37m' # White
bldblk='\e[1;30m' # Black - Bold
bldred='\e[1;31m' # Red
bldgrn='\e[1;32m' # Green
bldblu='\e[1;34m' # Blue

LOC="./drivers/misc/"
FILETYPE=".ko"
MODULE=$1
BOARD=root@$2
KMOD_PATH="${LOC}${MODULE}${FILETYPE}"

if [ $# -ne 2 ]; 
then
echo "${bldred} invalid arguments, usage: ./buildScript <ModuleName> <boardIP> ${txtwht}"
return 1
fi

make ${KMOD_PATH}
if [ $? -ne 0 ];
then 
echo "${bldred}make failed -> exiting ${txtwht}"
return 1
fi

echo installing ${MODULE}.ko on ${BOARD}
scp ${LOC}${MODULE}${FILETYPE} ${BOARD}:/lib/modules/5.4.69/kernel/drivers/misc
ssh ${BOARD} rmmod ${MODULE}
ssh ${BOARD} modprobe ${MODULE}
if [ $? -eq 0 ];
then
echo "${bldgrn}Kernel module installed ${txtwht}"
return 0
else
echo "${bldred}Error: Kernel module not installed ${txtwht}"
return 1
fi

