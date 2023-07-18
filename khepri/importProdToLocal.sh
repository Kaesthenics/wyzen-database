echo 'Récupération de la base de prod';
./getFromProd.sh;

echo 'Import de la base de prod en local';

./import.docker.total.dodo.sh dump/dump.total.khepri.complete.$(date +'%u').custom
