#!/bin/sh
# set up access to board without password

SSHDIR="/home/ldd/.ssh/"
SSHFILE="/home/ldd/.ssh/id_rsa.pub"

xtblk='\e[0;30m' # Black - Regular
txtred='\e[0;31m' # Red
txtgrn='\e[0;32m' # Green
txtblu='\e[0;34m' # Blue
txtwht='\e[0;37m' # White
bldblk='\e[1;30m' # Black - Bold
bldred='\e[1;31m' # Red
bldgrn='\e[1;32m' # Green
bldblu='\e[1;34m' # Blue

if [ $# -ne 1 ]; 
then
echo "${bldred}Invalid arguments, usage: ./SetUpBoardAccess <boardIP> ${txtwht}"
return 1
fi

echo "${txtred}Set key file below to:$txtgrn /home/ldd/.ssh/id_rsa ${txtwht}"

sudo ssh-keygen -t rsa
sudo ssh-copy-id -i $SSHFILE root@$1
