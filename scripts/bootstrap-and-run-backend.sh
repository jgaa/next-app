#!/bin/bash

nextapp_port=10100
signup_port=10101
mariadb_port=10102
certsdir=/tmp/nextapp_backend/certs
log_level=debug
stop_only=false
nopull=false
REPOSITORY="ghcr.io/jgaa"

# Function to display help message
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --stop-only         Stop only running containers."
    echo "  --nopull            Skip pulling the latest image."
    echo "  --repository NAME   Specify the repository name."
    echo "  -h, --help          Show this help message and exit."
    echo ""
    echo "This script bootstraps and runs the NextApp backend temporarily."
    echo "It is ment for development, testing, and just playing around with the software."
    echo "It ***NOT*** ment for production."
    exit 0
}

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --stop-only)
            stop_only=true
            shift
            ;;
        --nopull)
            nopull=true
            shift
            ;;
        --repository)
            if [[ -n "$2" && "$2" != --* ]]; then
                REPOSITORY="$2"
                shift 2
            else
                echo "Error: --repository requires a repository name"
                exit 1
            fi
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done



# List of containers to check and stop if running
containers=("nextappd-devel" "signupd-devel" "na-mariadb-devel")

for container in "${containers[@]}"; do
    # Check if the container is running
    if [[ $(docker ps --filter "name=$container" --filter "status=running" -q) ]]; then
        echo "Stopping running container: $container"
        docker stop "$container"
    else
        echo "Container $container is not running. Skipping..."
    fi
done

if [[ "$stop_only" == true ]]; then
    exit 0
fi

if [[ "$nopull" == true ]]; then
    echo "Skipping Docker pull..."
else
    docker pull mariadb:latest
    docker pull ghcr.io/jgaa/nextappd:latest
    docker pull ghcr.io/jgaa/signupd:latest
fi

if [ -z ${NEXTAPP_BACKEND_HOSTNAME+x} ]; then
    host=127.0.0.1
else
    host=${NEXTAPP_BACKEND_HOSTNAME}
fi

export NA_ROOT_DBPASSWD=`dd if=/dev/random bs=48 count=1 2> /dev/null | base64`
export NEXTAPP_DBPASSWD=`dd if=/dev/random bs=48 count=1 2> /dev/null | base64`

echo "Starting mariadb"
docker run --rm --detach --name na-mariadb-devel -p ${host}:${mariadb_port}:3306 --env MARIADB_ROOT_PASSWORD=${NA_ROOT_DBPASSWD} mariadb:latest

echo "Bootstrapping nextappd"
docker run --rm --name nextappd-devel-init --link na-mariadb-devel ${REPOSITORY}/nextappd --root-db-passwd ${NA_ROOT_DBPASSWD} --db-passwd ${NEXTAPP_DBPASSWD} -C ${log_level} --db-host na-mariadb-devel --bootstrap --server-fqdn ${host} --server-fqdn nextappd-devel -c ''

mkdir -p /tmp/nextapp_backend/certs
chmod 0777 /tmp/nextapp_backend/certs

echo "Creating signed client TLS certs for signupd"
docker run -v ${certsdir}:/certs --rm --name nextappd-devel-clicert --link na-mariadb-devel ${REPOSITORY}/nextappd --root-db-passwd ${NA_ROOT_DBPASSWD} --db-passwd ${NEXTAPP_DBPASSWD} -C ${log_level} --db-host na-mariadb-devel --create-client-cert /certs/signup --admin-cert -c ''

echo "Starting nextappd"
docker run -v ${certsdir}:/certs --rm --detach --name nextappd-devel -p ${host}:${nextapp_port}:10321 --link na-mariadb-devel ${REPOSITORY}/nextappd --db-passwd ${NEXTAPP_DBPASSWD} -C ${log_level} --db-host na-mariadb-devel -g 0.0.0.0:10321 --server-fqdn ${host} --server-fqdn nextappd-devel -c ''

echo "Bootstrapping signupd"
docker run -v ${certsdir}:/certs --rm  --name signupd-devel \
    --link nextappd-devel --link na-mariadb-devel ${REPOSITORY}/signupd \
    bootstrap -C ${log_level} \
    --root-db-passwd ${NA_ROOT_DBPASSWD} --db-passwd ${NEXTAPP_DBPASSWD} --db-host na-mariadb-devel \
    --nextapp-address https://nextappd-devel:10321 --nextapp-public-url https://${host}:${nextapp_port} \
    --grpc-client-ca-cert /certs/signup-ca.pem --grpc-client-cert /certs/signup-cert.pem --grpc-client-key /certs/signup-key.pem

echo "Starting sigupd"
docker run -v $(pwd)/doc:/doc --rm --detach --name signupd-devel -p ${host}:${signup_port}:10322 \
    --link nextappd-devel --link na-mariadb-devel ${REPOSITORY}/signupd \
    -C ${log_level} \
    --db-passwd ${NEXTAPP_DBPASSWD} --db-host na-mariadb-devel \
    --grpc-address 0.0.0.0:10322 --grpc-tls-mode none  --welcome /doc/sample-welcome.html --eula /doc/sample-eula.html -c ''

echo "Passwords:"
echo "  database/root NA_ROOT_DBPASSWD=${NA_ROOT_DBPASSWD}"
echo "  nextapp database user NEXTAPP_DBPASSWD=${NEXTAPP_DBPASSWD}"
echo "Signup address is: http://${host}:${signup_port}"
