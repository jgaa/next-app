#!/bin/sh

## Access after bootstrapping:
##  mysql -h 127.0.0.1 -u nextapp -p nextapp

echo "Starting a disposable mariadb container for testing..."

if [ -z "${NA_ROOT_DBPASSWD}" ] ; then
  NA_ROOT_DBPASSWD=`dd if=/dev/random bs=48 count=1 | base64`
  echo "NA_ROOT_DBPASSWD is: ${NA_ROOT_DBPASSWD}"
fi

docker run --rm --detach --name na-mariadb -v /var/tmp/nextapp/mariadb:/var/lib/mysql:Z -p 127.0.0.1:3306:3306 --env MARIADB_ROOT_PASSWORD=${NA_ROOT_DBPASSWD}  mariadb:latest

