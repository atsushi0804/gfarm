#!/bin/sh

set -eu

. ./regress.conf

BASEDIR=`dirname $0`
. ${BASEDIR}/../gfpconcat/setup_gfpconcat.sh

GFCP="gfcp -c"

setup_test

get_mode_gfarm()
{
    gfls -l ${1} | awk '{print $1}'
}

get_mode_local()
{
    ls -l ${1} | awk '{print $1}'
}

test_write_to_gfarm_j1()
{
    gfrm -f $gfile_out
    # with -t (range_test once)
    $GFCP -t -j 1 file:$lfile1 $gfile_out
}

test_write_to_local()
{
    rm -f $lfile_out
    $GFCP $gfile1 $lfile_out
}

test_write_to_gfarm()
{
    gfrm -f $gfile_out
    $GFCP $lfile2 $gfile_out
}

test_write_to_local_readonly()
{
    rm -f $lfile_out
    gfchmod 400 $gfile1
    $GFCP $gfile1 $lfile_out
    [ $(get_mode_local $lfile_out) = "-r--------" ]
}

test_write_to_gfarm_readonly()
{
    gfrm -f $gfile_out
    chmod 400 $lfile2
    $GFCP $lfile2 $gfile_out
    [ $(get_mode_gfarm $gfile_out) = "-r--------" ]
}

test_write_to_local_0byte()
{
    rm -f $lfile_out
    $GFCP $gfile_zero $lfile_out
}

test_write_to_gfarm_0byte()
{
    gfrm -f $gfile_out
    $GFCP $lfile_zero $gfile_out
}

test_overwrite_to_local()
{
    rm -f $lfile_out
    $GFCP $gfile1 $lfile_out
    if $GFCP $gfile1 $lfile_out 2> /dev/null; then
        exit $exit_fail
    fi
    $GFCP -f $gfile1 $lfile_out
}

test_overwrite_to_gfarm()
{
    gfrm -f $gfile_out
    $GFCP $lfile1 $gfile_out
    if $GFCP $lfile1 $gfile_out 2> /dev/null; then
        exit $exit_fail
    fi
    $GFCP -f $lfile1 $gfile_out
}

test_same_locale_file()
{
    if $GFCP -f $lfile1 file:$lfile1 2> /dev/null; then
        exit $exit_fail
    fi
}

test_same_gfarm_file()
{
    if $GFCP -f $gfile1 $gfile1 2> /dev/null; then
        exit $exit_fail
    fi
}

test_write_to_gfarm_j1
test_write_to_local
test_write_to_gfarm
test_write_to_local_readonly
test_write_to_gfarm_readonly
test_write_to_local_0byte
test_write_to_gfarm_0byte
test_overwrite_to_local
test_overwrite_to_gfarm
test_same_locale_file
test_same_gfarm_file

clean_test
exit $exit_pass
