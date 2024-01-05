#!/bin/bash

docker run --rm --detach --name nextappd -p 127.0.0.1:10321:10321 --link na-mariadb jgaafromnorth/nextappd --db-user nextapp --db-passwd ${NA_DBPASSWD}  -C info --db-host na-mariadb -g 0.0.0.0:10321

