#!/bin/sh
set -xeu
status=1
trap '[ $status = 1 ] && echo NG; exit $status' 0 1 2 15

: ${PKG:=gfarm}

CONF_OPT=
case $PKG in
gfarm)
	CONF_OPT="--enable-xmlattr --with-globus" #--with-infiniband
	;;
gfarm2fs)
	CONF_OPT=--with-gfarm=/usr/local
	;;
esac

./configure $CONF_OPT
make clean > /dev/null
make -j $(nproc) > /dev/null
sudo make install > /dev/null

gfarm-prun -p "(cd $PWD; sudo make install > /dev/null)"

status=0
echo Done
