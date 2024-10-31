#!/bin/bash

if [ -z "${NA_DBPASSWD}" ] ; then
  NA_DBPASSWD=`dd if=/dev/random bs=48 count=1 | base64`
  echo "NA_DBPASSWD is: ${NA_DBPASSWD}"
fi


docker run --rm -it --name nextappd-bootstrap --link na-mariadb jgaafromnorth/nextappd --db-user nextapp --db-passwd ${NA_DBPASSWD}  -C info --db-host na-mariadb --bootstrap --root-db-passwd ${NA_ROOT_DBPASSWD}

