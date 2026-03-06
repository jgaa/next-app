#!/bin/sh

## Access after bootstrapping:
##  mysql -h 127.0.0.1 -u nextapp -p nextapp

echo "Starting a disposable mariadb container for testing..."

if [ -z "${NA_ROOT_DBPASSWD}" ] ; then
  NA_ROOT_DBPASSWD=`dd if=/dev/random bs=48 count=1 | base64`
  echo "NA_ROOT_DBPASSWD is: ${NA_ROOT_DBPASSWD}"
fi

echo "NA_ROOT_DBPASSWD is preset to: ${NA_ROOT_DBPASSWD}"

docker run --rm --detach --name na-mariadb -v /var/tmp/nextapp/mariadb:/var/lib/mysql:Z -p 127.0.0.1:3306:3306 --env MARIADB_ROOT_PASSWORD=${NA_ROOT_DBPASSWD}  mariadb:latest

echo "Waiting for MariaDB to become ready on 127.0.0.1:3306 ..."
i=0
while [ ${i} -lt 60 ]; do
  if docker exec na-mariadb mariadb-admin --host=127.0.0.1 --port=3306 --user=root --password="${NA_ROOT_DBPASSWD}" ping >/dev/null 2>&1 ; then
    echo "MariaDB is ready."
    exit 0
  fi
  sleep 1
  i=$((i + 1))
done

echo "MariaDB did not become ready within 60 seconds."
echo "Recent logs from na-mariadb:"
docker logs --tail 100 na-mariadb || true
exit 1
