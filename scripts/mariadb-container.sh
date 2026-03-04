#!/bin/sh

## Access after bootstrapping:
##  mysql -h 127.0.0.1 -u nextapp -p nextapp

echo "Starting a disposable mariadb container for testing..."

if [ -z "${NA_ROOT_DBPASSWD}" ] ; then
  NA_ROOT_DBPASSWD=`dd if=/dev/random bs=48 count=1 | base64`
  echo "NA_ROOT_DBPASSWD is: ${NA_ROOT_DBPASSWD}"
fi

if [ -z "${NA_MARIADB_PORT}" ] ; then
  NA_MARIADB_PORT=3306
  echo "NA_MARIADB_PORT is: ${NA_MARIADB_PORT}"
fi

if [ -z "${NA_MARIADB_NAME}" ] ; then
  NA_MARIADB_NAME="na-mariadb-tmp"
  echo "NA_MARIADB_NAME is: ${NA_MARIADB_NAME}"
fi

echo "NA_ROOT_DBPASSWD is preset to: ${NA_ROOT_DBPASSWD}"

docker run --rm --detach --name ${NA_MARIADB_NAME} -p 127.0.0.1:${NA_MARIADB_PORT}:3306 --env MARIADB_ROOT_PASSWORD=${NA_ROOT_DBPASSWD}  mariadb:latest

