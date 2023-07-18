#!/bin/bash
SCRIPTDIR=$(dirname $0)

# Load library
source ../postgre.inc

OUTIL="Import total schema khepri"
HOST="127.0.0.1"
PORT=5433
DATABASE="total"
DBUSER="wyzen"
DBPASS="wyzen" #demande de mot de passe
CHARSET="utf8"

FILES=$*

for FILE in $FILES
do
        runImport $FILE
done

exit 0


