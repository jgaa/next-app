#!/bin/sh

echo "Starting a disposable mariadb container for testing..."

if [ -z "${NA_DBPASSWD}" ] ; then
  NA_DBPASSWD=`dd if=/dev/random bs=48 count=1 | base64`
  echo "NA_DBPASSWD is: ${NA_DBPASSWD}"
fi

docker run --rm --detach --name na-mariadb -p 127.0.0.1:3306:3306 --env MARIADB_ROOT_PASSWORD=${NA_DBPASSWD}  mariadb:latest
