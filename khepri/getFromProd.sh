#!/bin/bash

CUR_NUMDAY=$(date +'%u')
NUMDAY=${1:-$CUR_NUMDAY}

scp ddse.bkp:/var/dump/db/postgre/dump.total.khepri.complete.${NUMDAY}.custom dump
#gzip -f -d *.${NUMDAY}.sql.gz#!/bin/bash

