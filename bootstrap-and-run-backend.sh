#!/bin/bash

export NA_ROOT_DBPASSWD=`dd if=/dev/random bs=48 count=1 | base64`
export NEXTAPP_DBPASSWD=`dd if=/dev/random bs=48 count=1 | base64`

docker run --rm --detach --name na-mariadb -p 127.0.0.1:3306:3306 --env MARIADB_ROOT_PASSWORD=${NA_ROOT_DBPASSWD}  mariadb:latest

docker run --rm --name nextappd-init -p 127.0.0.1:10321:10321 --link na-mariadb jgaafromnorth/nextappd --root-db-passwd ${NA_ROOT_DBPASSWD} --db-passwd ${NA_DBPASSWD} -C debug --db-host na-mariadb --bootstrap

docker run --rm --detach --name nextappd -p 127.0.0.1:10321:10321 --link na-mariadb jgaafromnorth/nextappd --db-passwd ${NA_DBPASSWD} -C debug --db-host na-mariadb -g 0.0.0.0:10321

echo "Passwords: database/root NA_ROOT_DBPASSWD=${NA_ROOT_DBPASSWD}  nextapp database user NEXTAPP_DBPASSWD=${NEXTAPP_DBPASSWD}"

