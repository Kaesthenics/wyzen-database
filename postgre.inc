#!/bin/bash
PROG=$0
SEP="----------------------------------------------------------------------"

SCRIPTNAME=$(basename -s ".sh" "$PROG")
SCRIPTFILE=$(basename "$PROG")


DAY_OF_WEEK=`date +"%u"`
SUFFIXE=${SUFFIXE:=$DAY_OF_WEEK}
LOGFILE=postgre.log

DUMPOPTIONS_COMPLETE="--format=custom --clean --if-exists --no-owner --no-privileges --exclude-schema 'information_schema' --exclude-schema '^pg_*'"
DUMPOPTIONS_STRUCT="--format=tar --no-owner --no-acl --schema-only"
DUMPOPTIONS_DATA="--format=tar --no-owner --no-acl --data-only"
DUMPOPTIONS_USERS=""

IMPORTOPTIONS="--clean --if-exists --no-owner --no-privileges"

DUMPOPTIONS=""
DEST_DIR=${DEST_DIR:=$(pwd)}

test -d "$DEST_DIR" || mkdir -p "$DEST_DIR"

function error {
	echo "ERROR: " $*
	exit 1
}

function checkVariables {
	[ $(command -v pg_restore) ] || error "pg_restore command not exists" 
	if [ -z "$HOST" ]
	then
		echo "Host not found. Set HOST variable."
		exit 3
	fi

	if [ -z "$PORT" ]
	then
		PORT=5432
	fi

	echo "postgre on $HOST:$PORT"
}

function checkVariablesDump {
	[ $(command -v pg_dump) ] || error "pg_dump command not exists" 
        if [ -z "$DUMPOPTIONS" ]
        then
			DUMPOPTIONS=${DUMPOPTIONS_DEFAULT}
        fi
}

function DbVersion {
	[ $(command -v mysql) ] || error "mysql command not exists"

	VERSION=$(psql --version |cut -d ' ' -f 3)

	echo $VERSION
}

function Titre {
	echo $SEP
	echo "Import des donnees du fichier $1"
	echo $SEP
}

function existsDumpfile {
	checkVariables
	if [ "x$1" == "x" ]
	then
		echo 
		echo "Usage:"
		echo "Import du dump de $OUTIL: $PROG <dump_file>"
		echo 
		echo $SEP
		exit 10
	fi

	if [ ! -f $1 ]
	then
		echo "Fichier dump introuvable: $1"
		echo $SEP
		exit 11
	fi
	
	if [ "${file: -3}" == ".gz" ]
	then
		gzip -f -d "$file"
		if [ $? != 0 ]
		then
			echo "Decompression impossible de $file"
			exit 12
		fi
		file=`echo $file | rev | cut -c 4- | rev`
	fi

	echo "$file"
}

function runImport {
	checkVariables

	DUMPFILE=$1
	Titre $DUMPFILE
	SQLFILE=$(existsDumpfile $DUMPFILE)
	
        LOGFILE="${DUMPFILE}.log"

	if [ "x$DBPASS" == "x" ]
	then
		read -p "DB password: " -r -s DBPASS
		echo
	fi  

	echo "Import des donnees en cours ..."

	DATABASE_URL=postgresql://${DBUSER}:${DBPASS}@${HOST}:${PORT}/${DATABASE}

	CMD="pg_restore --dbname ${DATABASE_URL} ${IMPORTOPTIONS} ${IGNORED_TABLES_STRING} $DUMPFILE"
	echo $CMD
	$CMD 2>> ${LOGFILE}

	echo "Import des donnees $SQLFILE termine."
	echo $SEP
	return $?
}

###
# build options for excluded tables
# ENV: EXCLUDED_TABLES="table1 table2 table3 ..."
#
function getExcludedTables {
	IGNORED_TABLES_STRING=""
	
	if [ ! -z "${EXCLUDED_TABLES}" ]; then
		for TABLE in ${EXCLUDED_TABLES}; do
		   IGNORED_TABLES_STRING=${IGNORED_TABLES_STRING}" ----exclude-table-data=table=${TABLE}"
		done
	fi
	echo ${IGNORED_TABLES_STRING}
}

# Remove some codes
function removeCodes {
	FILE=$1

	#echo "Suppression des codes 50013"
	#sed -i '/^\/\*\!50013/d' $FILE

	#echo "Suppression des codes 50017"
	#sed -i -E 's/\/\*!50017 DEFINER(.)*\*\///ig' $FILE

	return 0
}

# Dump Users
function runDumpUsers {
	LISTE_DATABASE=$1
        checkVariables
        checkVariablesDump
	DT=$(date +"%Y-%m-%d %k:%M:%S")
	V=$(DbVersion)

        # Lecture du mot de passe pour la connexion
        if [ "x$DBPASS" == "x" ]
        then
                read -p "DB root password: " -r -s DBPASS
        fi

        EXCLUDE=""
        PARAMS="${DUMPOPTIONS} $EXCLUDE"

	for databaseName in $LISTE_DATABASE; do
                BASENAME="${DEST_DIR}/dump.$databaseName"
                OUTFILE="${BASENAME}.users.${SUFFIXE}.sql"
                LOGFILE="${BASENAME}.${SUFFIXE}.log"
                rm -f $OUTFILE $LOGFILE 2>/dev/null

                IGNORED_TABLES_STRING=$(getExcludedTables)

		if [ $V = "5.5" ]; then
                CREATE_USERS="\
select distinct concat('CREATE USER IF NOT EXISTS ''',User,'''@''',Host,''' IDENTIFIED BY ''',if(password!='',password,authentication_string),''';') create_queries \
from mysql.user \
where (user, host) in (SELECT DISTINCT user, host FROM mysql.db where db='$databaseName');"
                else
                CREATE_USERS="\
select distinct concat('CREATE USER IF NOT EXISTS ''',User,'''@''',Host,''' IDENTIFIED BY ''',authentication_string,''';') create_queries \
from mysql.user \
where (user, host) in (SELECT DISTINCT user, host FROM mysql.db where db='$databaseName');"
                fi

                GRANTS="SELECT group_concat(CONCAT('SHOW GRANTS FOR ''', user, ''' ;') SEPARATOR '') FROM mysql.db where db='$databaseName';"

                echo "-- DUMP USER FOR $databaseName" > ${OUTFILE}

                # CREATE USERS
                mysql -N -B -h ${HOST} -u${DBUSER} -p${DBPASS} -e "${CREATE_USERS}" >> ${OUTFILE} 2>${LOGFILE}

                # BUILD SHOW GRANTS
                SHOW_GRANTS=$(mysql -N -B -h ${HOST} -u${DBUSER} -p${DBPASS} -e "${GRANTS}");

                # SHOW GRANTS
                mysql -N -B -h ${HOST} -u${DBUSER} -p${DBPASS} -e "${SHOW_GRANTS}" | sed 's/^GRANT.*/&;/' >> ${OUTFILE} 2>${LOGFILE}

		RET=$?
		echo "-- Dump completed on ${DT}" >> ${OUTFILE}
                if [ $RET -eq 0 ]
                then
                        echo "Dump Users for $databaseName: OK"
                else
                        echo "Dump Users for $databaseName: ERR"
                fi
                gzip -f ${OUTFILE}

		echo "Destination dir: ${OUTFILE}.gz"
        done
}

# Dump Structure
function runDumpComplete {
        DBNAMEANDSCHEMA=$1
        DBNAME=$(echo $1|cut -d'.' -f1)
        SCHEMA=$(echo $1|cut -d'.' -f2)
        checkVariables
        checkVariablesDump

        # Lecture du mot de passe pour la connexion
        if [ "x$DBPASS" == "x" ]
        then
                read -p "DB root password: " -r -s DBPASS
        fi

        EXCLUDE=""
        PARAMS="${DUMPOPTIONS} $EXCLUDE"

                BASENAME="${DEST_DIR}/dump.$DBNAMEANDSCHEMA"
                OUTFILE="${BASENAME}.complete.${SUFFIXE}.custom"
                LOGFILE="${BASENAME}.${SUFFIXE}.log"
                rm -f $OUTFILE $LOGFILE 2>/dev/null

                IGNORED_TABLES_STRING=$(getExcludedTables)

		DATABASE_URL=postgresql://${DBUSER}:${DBPASS}@${HOST}:${PORT}/${DBNAME}

                echo -n "Dump definitions: $databaseName"
                CMD="pg_dump  --dbname ${DATABASE_URL} ${DUMPOPTIONS_COMPLETE} --schema=${SCHEMA} ${IGNORED_TABLES_STRING}"
                $CMD |sed -E 's/(DROP|CREATE|COMMENT ON) EXTENSION/-- \1 EXTENSION/g' > ${OUTFILE} 2>> ${LOGFILE}
                if [ $? -eq 0 ]
                then
                        echo ": OK"
                else
                        echo ": ERR"
                	echo "Destination dir: ${OUTFILE}"
                fi

}

function runScript {
        checkVariables

        FILE=$1
        Titre $FILE
        SQLFILE=$(existsDumpfile $FILE)

        LOGFILE="${FILE}.log"

        if [ "x$DBPASS" == "x" ]
        then
                read -p "DB password: " -r -s DBPASS
                echo
        fi

        echo "Import du script en cours ..."

        DATABASE_URL=postgresql://${DBUSER}:${DBPASS}@${HOST}:${PORT}/${DATABASE}

        CMD="psql --dbname ${DATABASE_URL} -f $FILE"
        echo $CMD
        $CMD 2>> ${LOGFILE}

        echo "Import du script $SQLFILE termine."
        echo $SEP
        return $?
}


function getIpFromContainerName {
	IP=$(docker inspect -f '{{range.NetworkSettings.Networks}}{{.IPAddress}}{{end}}' $1)
	echo "$IP"
}

# Dump All Data
function runDump {

	# $1 = <dbname>.<schema>

	echo "Dump dans $DEST_DIR"

	#runDumpUsers $*
	runDumpComplete $*
}

