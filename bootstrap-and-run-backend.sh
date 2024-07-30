#!/bin/bash

nextapp_port=10100
signup_port=10101
mariadb_port=10102
certsdir=/tmp/nextapp_backend/certs
log_level=info

if [ -z ${NEXTAPP_BACKEND_HOSTNAME+x} ]; then
    host=127.0.0.1
else
    host=${NEXTAPP_BACKEND_HOSTNAME}
fi

export NA_ROOT_DBPASSWD=`dd if=/dev/random bs=48 count=1 2> /dev/null | base64`
export NEXTAPP_DBPASSWD=`dd if=/dev/random bs=48 count=1 2> /dev/null | base64`

echo "Starting mariadb"
docker run --rm --detach --name na-mariadb-devel -p ${host}:${mariadb_port}:3306 --env MARIADB_ROOT_PASSWORD=${NA_ROOT_DBPASSWD}  mariadb:latest

echo "Bootstrapping nextappd"
docker run --rm --name nextappd-devel-init --link na-mariadb-devel jgaafromnorth/nextappd --root-db-passwd ${NA_ROOT_DBPASSWD} --db-passwd ${NEXTAPP_DBPASSWD} -C ${log_level} --db-host na-mariadb-devel --bootstrap --server-fqdn ${host} --server-fqdn nextappd-devel -c ''

mkdir -p /tmp/nextapp_backend/certs
chmod 0777 /tmp/nextapp_backend/certs

echo "Creating signed client TLS certs for signupd"
docker run -v ${certsdir}:/certs --rm --name nextappd-devel-clicert --link na-mariadb-devel jgaafromnorth/nextappd --root-db-passwd ${NA_ROOT_DBPASSWD} --db-passwd ${NEXTAPP_DBPASSWD} -C ${log_level} --db-host na-mariadb-devel --create-client-cert /certs/signup --admin-cert -c ''

echo "Starting nextappd"
docker run -v ${certsdir}:/certs --rm --detach --name nextappd-devel -p ${host}:${nextapp_port}:10321 --link na-mariadb-devel jgaafromnorth/nextappd --db-passwd ${NEXTAPP_DBPASSWD} -C ${log_level} --db-host na-mariadb-devel -g 0.0.0.0:10321 --server-fqdn ${host} --server-fqdn nextappd-devel -c ''

echo "Starting sigupd"
docker run -v ${certsdir}:/certs -v $(pwd)/doc:/doc --rm --detach --name signupd-devel -p ${host}:${signup_port}:10322 --link nextappd-devel jgaafromnorth/signupd -C ${log_level} -g 0.0.0.0:10322 --grpc-tls-mode none --nextapp-address https://nextappd-devel:10321 --grpc-client-cert /certs/signup-cert.pem --grpc-client-key /certs/signup-key.pem --grpc-client-ca-cert /certs/signup-ca.pem --nextapp-public-url https://${host}:${nextapp_port} --welcome /doc/sample-welcome.html --eula /doc/sample-eula.html -c ''

echo "Passwords:"
echo "  database/root NA_ROOT_DBPASSWD=${NA_ROOT_DBPASSWD}"
echo "  nextapp database user NEXTAPP_DBPASSWD=${NEXTAPP_DBPASSWD}"
echo "Signup address is: http://${host}:${signup_port}"
