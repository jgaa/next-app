#!/usr/bin/env bash
set -euo pipefail
umask 077
export COMPOSE_PROGRESS=quiet COMPOSE_ANSI=never
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

fail() { printf 'Error: %s\n' "$*" >&2; exit 1; }
trap 'printf "Setup stopped at line %s. Existing data was preserved. See README.md for recovery.\n" "$LINENO" >&2' ERR
help() {
    cat <<'HELP'
Usage: ./bootstrap.sh [options]
  --non-interactive             Use defaults without prompts
  --host HOST                  Public NextApp DNS name or IPv4 address
  --nextapp-port PORT           Default: 10321
  --nextapp-bind ADDRESS        Host IPv4 bind address (default: 127.0.0.1)
  --signup-port PORT            Default: 10322
  --signup-bind ADDRESS         Host IPv4 bind address (default: 127.0.0.1)
  --signup-protocol http|https  Advertised protocol; HTTPS requires external proxy
  --signup-public-url URL       Client-facing signup URL, independently of bind
  --allow-insecure-signup       Acknowledge plaintext signup on non-loopback binds
  --project-name NAME           Unique Compose project (default: nextapp-self-hosted)
  --update-host HOST            Replace server certificate, retaining CA and clients
  --help                       Show this help
Existing .env is preserved; setup options cannot override it. --update-host is
an explicit exception. IPv6 deployment is not supported by this script yet.
HELP
}
noninteractive=no
update_host=
configured=no
declare -A options=()
while (($#)); do
    case "$1" in
        --help|-h) help; exit 0 ;;
        --non-interactive) noninteractive=yes; shift ;;
        --allow-insecure-signup) options[ALLOW_INSECURE_SIGNUP]=yes; configured=yes; shift ;;
        --host|--nextapp-port|--nextapp-bind|--signup-port|--signup-bind|--signup-protocol|--signup-public-url|--project-name|--update-host)
            (($# >= 2)) && [[ -n "$2" ]] || fail "Missing value for $1"
            case "$1" in
                --host) key=NEXTAPP_HOST ;;
                --nextapp-port) key=NEXTAPP_PORT ;;
                --nextapp-bind) key=NEXTAPP_BIND_ADDRESS ;;
                --signup-port) key=SIGNUP_PORT ;;
                --signup-bind) key=SIGNUP_BIND_ADDRESS ;;
                --signup-protocol) key=SIGNUP_PROTOCOL ;;
                --signup-public-url) key=SIGNUP_PUBLIC_URL ;;
                --project-name) key=COMPOSE_PROJECT_NAME ;;
                --update-host) update_host=$2; shift 2; continue ;;
            esac
            options[$key]=$2; configured=yes; shift 2 ;;
        *) fail "Unknown option: $1 (see --help)" ;;
    esac
done
keys=(COMPOSE_PROJECT_NAME NEXTAPP_HOST NEXTAPP_PORT NEXTAPP_BIND_ADDRESS
      SIGNUP_PORT SIGNUP_BIND_ADDRESS SIGNUP_PROTOCOL SIGNUP_PUBLIC_URL
      ALLOW_INSECURE_SIGNUP MARIADB_ROOT_PASSWORD MARIADB_NEXTAPP_PASSWORD
      MARIADB_SIGNUP_PASSWORD SIGNUP_ADMIN_PASSWORD)
# Do not execute .env as shell code or allow inherited installation settings
# to override the file that ordinary Compose operations use.
for key in "${keys[@]}"; do unset "$key"; done
ipv4() {
    local part
    local -a parts
    [[ $1 =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || return 1
    IFS=. read -r -a parts <<< "$1"
    for part in "${parts[@]}"; do
        [[ ${#part} -le 3 && ( $part == 0 || $part != 0* ) ]] || return 1
        ((10#$part <= 255)) || return 1
    done
}
hostname() {
    local label
    local -a labels
    if [[ $1 =~ ^[0-9.]+$ ]]; then ipv4 "$1"; return; fi
    [[ -n $1 && ${#1} -le 253 && $1 != *..* && $1 != .* && $1 != *. ]] || return 1
    IFS=. read -r -a labels <<< "$1"
    for label in "${labels[@]}"; do
        [[ ${#label} -le 63 && $label =~ ^[a-zA-Z0-9]([a-zA-Z0-9-]*[a-zA-Z0-9])?$ ]] || return 1
    done
}
port() { [[ $1 =~ ^[1-9][0-9]{0,4}$ ]] && ((10#$1 <= 65535)); }
validate() {
    [[ $COMPOSE_PROJECT_NAME =~ ^[a-z0-9][a-z0-9_-]*$ ]] || fail 'Invalid Compose project name'
    hostname "$NEXTAPP_HOST" || fail 'Invalid NextApp hostname/IPv4 address'
    if ! port "$NEXTAPP_PORT" || ! port "$SIGNUP_PORT"; then fail 'Ports must be between 1 and 65535'; fi
    if ! ipv4 "$NEXTAPP_BIND_ADDRESS" || ! ipv4 "$SIGNUP_BIND_ADDRESS"; then fail 'Bind addresses must be IPv4 addresses'; fi
    [[ $SIGNUP_PROTOCOL == http || $SIGNUP_PROTOCOL == https ]] || fail 'Signup protocol must be http or https'
    [[ $ALLOW_INSECURE_SIGNUP == yes || $ALLOW_INSECURE_SIGNUP == no ]] || fail 'Invalid insecure signup acknowledgment'
    [[ $SIGNUP_PUBLIC_URL =~ ^(https?)://([^/:]+)(:([0-9]+))?$ ]] || fail 'Signup URL must be http(s)://hostname[:port] without a path'
    [[ ${BASH_REMATCH[1]} == "$SIGNUP_PROTOCOL" ]] || fail 'Signup URL protocol differs from SIGNUP_PROTOCOL'
    local url_host=${BASH_REMATCH[2]} url_port=${BASH_REMATCH[4]}
    hostname "$url_host" || fail 'Invalid signup URL hostname'
    [[ -z $url_port ]] || port "$url_port" || fail 'Invalid signup URL port'
    if [[ $NEXTAPP_PORT == "$SIGNUP_PORT" && ( $NEXTAPP_BIND_ADDRESS == "$SIGNUP_BIND_ADDRESS" || $NEXTAPP_BIND_ADDRESS == 0.0.0.0 || $SIGNUP_BIND_ADDRESS == 0.0.0.0 ) ]]; then
        fail 'NextApp and signup ports overlap on the same host address'
    fi
    local secret
    for secret in MARIADB_ROOT_PASSWORD MARIADB_NEXTAPP_PASSWORD MARIADB_SIGNUP_PASSWORD SIGNUP_ADMIN_PASSWORD; do
        [[ ${!secret} =~ ^[a-fA-F0-9]{64}$ ]] || fail "$secret must contain 64 hexadecimal characters (openssl rand -hex 32)"
    done
    # Check the actual backend bind even when HTTPS is advertised by a proxy.
    if [[ $SIGNUP_BIND_ADDRESS != 127.* && $ALLOW_INSECURE_SIGNUP != yes ]]; then
        printf 'WARNING: signupd will expose plaintext gRPC on %s. Use HTTPS on untrusted networks; restrict direct backend access.\n' "$SIGNUP_BIND_ADDRESS" >&2
        if [[ $noninteractive == yes || ! -t 0 ]]; then
            fail 'Use --allow-insecure-signup for a trusted LAN/VPN, or keep signup bound to 127.0.0.1'
        fi
        local answer
        read -r -p 'Type YES to acknowledge plaintext signup exposure: ' answer
        [[ $answer == YES ]] || fail 'Plaintext signup exposure was not acknowledged'
        ALLOW_INSECURE_SIGNUP=yes
    fi
}
compose() { docker compose --env-file .env -f compose.yaml "$@"; }
for program in docker openssl flock; do command -v "$program" >/dev/null || fail "Required program missing: $program"; done
docker compose version >/dev/null 2>&1 || fail 'Install the Docker Compose v2 plugin'
docker info >/dev/null 2>&1 || fail 'Docker daemon unavailable or access denied; check docker info'
# flock is released automatically even after an interruption; the directory
# prevents a writable lock file from appearing in a public checkout.
mkdir -p .bootstrap.lock
exec 9>.bootstrap.lock/lock
flock -n 9 || fail 'Another bootstrap process is running'
if [[ -e .env ]]; then
    [[ -f .env && ! -L .env ]] || fail '.env must be a regular file, not a symlink'
    [[ $configured == no ]] || fail 'Existing .env preserved. Edit its non-secret settings instead of passing setup options'
    declare -A seen=()
    while IFS= read -r line || [[ -n $line ]]; do
        [[ -z $line || $line == \#* ]] && continue
        [[ $line == *=* ]] || fail 'Invalid .env line; use unquoted KEY=value syntax'
        key=${line%%=*}; value=${line#*=}
        [[ " ${keys[*]} " == *" $key "* ]] || fail "Unknown .env key: $key"
        [[ ! -v seen[$key] ]] || fail "Duplicate .env key: $key"
        seen[$key]=yes
        printf -v "$key" '%s' "$value"
    done < .env
    for key in "${keys[@]}"; do [[ -v $key ]] || fail "Missing .env key: $key"; done
    chmod 600 .env
    validate
else
    [[ -z $update_host ]] || fail '--update-host requires an initialized installation'
    COMPOSE_PROJECT_NAME=${options[COMPOSE_PROJECT_NAME]:-nextapp-self-hosted}
    NEXTAPP_HOST=${options[NEXTAPP_HOST]:-localhost}
    NEXTAPP_PORT=${options[NEXTAPP_PORT]:-10321}
    NEXTAPP_BIND_ADDRESS=${options[NEXTAPP_BIND_ADDRESS]:-127.0.0.1}
    SIGNUP_PORT=${options[SIGNUP_PORT]:-10322}
    SIGNUP_BIND_ADDRESS=${options[SIGNUP_BIND_ADDRESS]:-127.0.0.1}
    SIGNUP_PROTOCOL=${options[SIGNUP_PROTOCOL]:-http}
    ALLOW_INSECURE_SIGNUP=${options[ALLOW_INSECURE_SIGNUP]:-no}
    printf 'NextApp Self-Hosted Setup\n=========================\n\n'
    if [[ $noninteractive == no ]]; then
        [[ -t 0 ]] || fail 'Use --non-interactive when stdin is not a terminal'
        for key in NEXTAPP_HOST NEXTAPP_PORT NEXTAPP_BIND_ADDRESS SIGNUP_PORT SIGNUP_BIND_ADDRESS SIGNUP_PROTOCOL; do
            case "$key" in
                NEXTAPP_HOST) prompt='NextApp hostname or IP' ;;
                NEXTAPP_PORT) prompt='NextApp listening port' ;;
                NEXTAPP_BIND_ADDRESS) prompt='NextApp bind address' ;;
                SIGNUP_PORT) prompt='Signup listening port' ;;
                SIGNUP_BIND_ADDRESS) prompt='Signup bind address' ;;
                SIGNUP_PROTOCOL) prompt='Signup public protocol (HTTPS requires external proxy)' ;;
            esac
            read -r -p "$prompt [${!key}]: " value
            [[ -z $value ]] || printf -v "$key" '%s' "$value"
        done
    fi
    if [[ $SIGNUP_PROTOCOL == https ]]; then default_url="https://$NEXTAPP_HOST"; else default_url="http://$NEXTAPP_HOST:$SIGNUP_PORT"; fi
    SIGNUP_PUBLIC_URL=${options[SIGNUP_PUBLIC_URL]:-$default_url}
    if [[ $noninteractive == no ]]; then
        read -r -p "Public signup URL [$SIGNUP_PUBLIC_URL]: " value
        [[ -z $value ]] || SIGNUP_PUBLIC_URL=$value
    fi
    printf 'Generating credentials...\n'
    for key in MARIADB_ROOT_PASSWORD MARIADB_NEXTAPP_PASSWORD MARIADB_SIGNUP_PASSWORD SIGNUP_ADMIN_PASSWORD; do
        printf -v "$key" '%s' "$(openssl rand -hex 32)"
    done
    validate
    # Never generate new credentials against an existing project volume.
    existing_volumes=$(docker volume ls -q --filter "label=com.docker.compose.project=$COMPOSE_PROJECT_NAME")
    [[ -z $existing_volumes ]] || fail 'Project volumes already exist but .env is missing. Restore the original .env or choose a new --project-name'
    (set -o noclobber; for key in "${keys[@]}"; do printf '%s=%s\n' "$key" "${!key}"; done > .env)
fi
for key in "${keys[@]}"; do export "${key?}"; done
compose config --quiet
printf 'Starting MariaDB...\n'
compose up -d mariadb
healthy=no
for ((attempt=0; attempt<120; attempt++)); do
    container=$(compose ps -q mariadb)
    if [[ -n $container ]]; then
        health=$(docker inspect --format '{{.State.Health.Status}}' "$container")
        if [[ $health == healthy ]]; then healthy=yes; break; fi
        [[ $health != unhealthy ]] || fail 'MariaDB is unhealthy; run docker compose logs mariadb'
    fi
    sleep 2
done
[[ $healthy == yes ]] || fail 'MariaDB health timeout; run docker compose logs mariadb'
sql() {
    # shellcheck disable=SC2016 # Expanded by the container shell.
    compose exec -T mariadb sh -c 'export MYSQL_PWD="$MARIADB_ROOT_PASSWORD"; exec mariadb --user=root --batch --raw --skip-column-names' <<< "$1"
}
# Confirm application credentials independently, without exposing them in argv.
check_password() {
    local user=$1
    export MYSQL_PWD=$2
    compose exec -T -e MYSQL_PWD mariadb mariadb --protocol=tcp --host=127.0.0.1 --user="$user" --execute='SELECT 1' >/dev/null || fail "$user database password does not match .env. Restore the original credentials"
    unset MYSQL_PWD
}
nextapp() {
    compose run --rm --no-deps -T --entrypoint /bin/sh -e NEXTAPP_ROOT_DBPASSW nextappd \
        -c 'umask 077; exec /usr/local/bin/nextappd "$@"' sh "$@" --config '' --db-host mariadb --db-port 3306
}
export NEXTAPP_ROOT_DBPASSW=$MARIADB_ROOT_PASSWORD
if [[ $(sql "SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME='nextapp'") == 0 ]]; then
    printf 'Bootstrapping nextappd...\n'
    nextapp bootstrap --server-fqdn "$NEXTAPP_HOST" --server-fqdn nextappd
else
    [[ $(sql "SELECT version > 0 FROM nextapp.nextapp WHERE id=1") == 1 ]] || fail 'Incomplete nextapp database. See interrupted bootstrap recovery in README.md'
    [[ $(sql "SELECT COUNT(*) FROM nextapp.cert WHERE id='ca' AND cert <> '' AND pkey <> ''") == 1 ]] || fail 'NextApp CA missing/incomplete. Stop and restore a backup; do not create a replacement CA'
    [[ $(sql "SELECT COUNT(*) FROM nextapp.user WHERE kind='super' AND system_user=1 AND active=1") == 1 ]] || fail 'Incomplete NextApp admin initialization; restore a backup'
fi
check_password nextapp "$MARIADB_NEXTAPP_PASSWORD"
[[ $(sql "SELECT COUNT(*) FROM nextapp.cert WHERE id='ca' AND cert <> '' AND pkey <> ''") == 1 ]] || fail 'NextApp CA was not initialized'
verify_server() {
    local cert ca
    cert=$(sql "SELECT cert FROM nextapp.cert WHERE id='grpc-server'")
    ca=$(sql "SELECT cert FROM nextapp.cert WHERE id='ca'")
    openssl verify -CAfile <(printf '%s\n' "$ca") <(printf '%s\n' "$cert") >/dev/null || return 1
    if ipv4 "$1"; then
        openssl x509 -noout -checkip "$1" <<< "$cert" >/dev/null || return 1
    else
        openssl x509 -noout -checkhost "$1" <<< "$cert" >/dev/null || return 1
    fi
    openssl x509 -noout -checkhost nextappd <<< "$cert" >/dev/null
}
if [[ -e .host-update ]]; then
    pending=$(cat .host-update)
    [[ -z $update_host || $update_host == "$pending" ]] || fail 'A different hostname update is pending; resume without arguments first'
    update_host=$pending
fi
if [[ -n $update_host ]]; then
    hostname "$update_host" || fail 'Invalid updated hostname/IPv4 address'
    [[ $(sql "SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME='signup'") == 1 ]] || fail 'Finish initial bootstrap before changing the hostname'
    [[ $(sql 'SELECT COUNT(*) FROM signup.instance') == 1 ]] || fail 'Expected one signup instance; inspect signup.instance before changing hostname'
    printf '%s\n' "$update_host" > .host-update
    compose stop signupd nextappd
    ca_before=$(sql "SELECT SHA2(CONCAT(cert,pkey),256) FROM nextapp.cert WHERE id='ca'")
    nextapp create-grpc-cert --server-fqdn "$update_host" --server-fqdn nextappd
    verify_server "$update_host" || fail 'Server certificate verification failed; the latest image may lack IP SAN support. Use a DNS hostname or publish the fixed image'
    [[ $(sql "SELECT SHA2(CONCAT(cert,pkey),256) FROM nextapp.cert WHERE id='ca'") == "$ca_before" ]] || fail 'CA changed unexpectedly; services remain stopped'
    sql "UPDATE signup.instance SET grpc_public_url='https://$update_host:$NEXTAPP_PORT' WHERE grpc_url='https://nextappd:10321';" >/dev/null
    temporary=$(mktemp .env.XXXXXX)
    awk -v host="$update_host" '/^NEXTAPP_HOST=/ {$0="NEXTAPP_HOST=" host} {print}' .env > "$temporary"
    mv -- "$temporary" .env
    NEXTAPP_HOST=$update_host
    export NEXTAPP_HOST
    rm -- .host-update
fi
verify_server "$NEXTAPP_HOST" || fail 'Server certificate does not match configured host and internal nextappd name. See hostname recovery in README.md'
printf 'Creating signupd client certificate...\n'
# File count detects interrupted writes; never overwrite existing certificates.
# shellcheck disable=SC2016 # Expanded by the container shell.
cert_count=$(compose run --rm --no-deps -T --entrypoint /bin/sh nextappd -c 'n=0; for f in /var/lib/nextappd/signup-ca.pem /var/lib/nextappd/signup-cert.pem /var/lib/nextappd/signup-key.pem; do if [ -e "$f" ]; then n=$((n+1)); fi; done; echo "$n"')
if [[ $cert_count == 0 ]]; then
    nextapp create-client-cert --cert-name /var/lib/nextappd/signup --admin-cert
elif [[ $cert_count != 3 ]]; then
    fail 'Partial signup client certificate files. Preserve them and see README.md recovery'
fi
# Validate the certificate/key/CA set on the host without saving private keys.
client_cert=$(compose run --rm --no-deps -T --entrypoint /bin/cat nextappd /var/lib/nextappd/signup-cert.pem)
client_key_pub=$(compose run --rm --no-deps -T --entrypoint /bin/cat nextappd /var/lib/nextappd/signup-key.pem | openssl pkey -pubout)
[[ $(openssl x509 -pubkey -noout <<< "$client_cert") == "$client_key_pub" ]] || fail 'Signup client certificate/key mismatch'
client_ca=$(compose run --rm --no-deps -T --entrypoint /bin/cat nextappd /var/lib/nextappd/signup-ca.pem)
openssl verify -CAfile <(printf '%s\n' "$client_ca") <(printf '%s\n' "$client_cert") >/dev/null
db_ca=$(sql "SELECT cert FROM nextapp.cert WHERE id='ca'")
[[ $(openssl x509 -noout -fingerprint -sha256 <<< "$client_ca") == "$(openssl x509 -noout -fingerprint -sha256 <<< "$db_ca")" ]] || fail 'Signup certificate belongs to a different NextApp CA'
if [[ $(sql "SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME='signup'") == 0 ]]; then
    printf 'Bootstrapping signupd...\n'
    export SIGNUP_ROOT_DB_PASSWD=$MARIADB_ROOT_PASSWORD
    compose run --rm --no-deps -T -e SIGNUP_ROOT_DB_PASSWD -e SIGNUP_ADMIN_PASSWORD signupd bootstrap \
        --config '' --db-host mariadb --db-port 3306 \
        --nextapp-address https://nextappd:10321 \
        --nextapp-public-url "https://$NEXTAPP_HOST:$NEXTAPP_PORT" \
        --grpc-client-ca-cert /certs/signup-ca.pem \
        --grpc-client-cert /certs/signup-cert.pem --grpc-client-key /certs/signup-key.pem
else
    [[ $(sql 'SELECT version > 0 FROM signup.signup WHERE id=1') == 1 ]] || fail 'Incomplete signup schema; see README.md recovery'
    [[ $(sql "SELECT COUNT(*) FROM signup.localuser WHERE kind='admin'") == 1 ]] || fail 'Incomplete signup admin initialization; see README.md recovery'
fi
check_password signup "$MARIADB_SIGNUP_PASSWORD"
[[ $(sql "SELECT COUNT(*) FROM signup.instance WHERE grpc_url='https://nextappd:10321' AND grpc_public_url='https://$NEXTAPP_HOST:$NEXTAPP_PORT' AND ca_cert <> '' AND grpc_key <> '' AND grpc_cert <> ''") == 1 ]] || fail 'Signup instance configuration is missing or differs from .env. See README.md recovery'
printf 'Starting services...\n'
compose up -d
printf 'Waiting for signupd to authenticate with nextappd...\n'
synced=no
for ((attempt=0; attempt<60; attempt++)); do
    if [[ $(sql "SELECT COUNT(*) FROM signup.instance WHERE grpc_url='https://nextappd:10321' AND server_id IS NOT NULL AND server_id <> ''") == 1 ]]; then
        synced=yes; break
    fi
    sleep 2
done
[[ $synced == yes ]] || fail 'Signupd has not synchronized with NextApp; inspect docker compose logs signupd nextappd'
declare -A service_states=()
for service in nextappd signupd; do
    container=$(compose ps -q "$service")
    [[ -n $container ]] || fail "$service is missing; inspect docker compose logs $service"
    service_states[$service]=$(docker inspect --format '{{.State.Status}} {{.RestartCount}}' "$container")
    [[ ${service_states[$service]} == running\ * ]] || fail "$service is not running; inspect docker compose logs $service"
done
sleep 3
for service in nextappd signupd; do
    container=$(compose ps -q "$service")
    [[ -n $container && $(docker inspect --format '{{.State.Status}} {{.RestartCount}}' "$container") == "${service_states[$service]}" ]] || fail "$service failed to remain running; inspect docker compose logs $service"
done
printf '\nInstallation completed.\nNextApp endpoint: https://%s:%s\nClient signup URL: %s\n' "$NEXTAPP_HOST" "$NEXTAPP_PORT" "$SIGNUP_PUBLIC_URL"
[[ $SIGNUP_PROTOCOL != https ]] || printf 'Configure your external HTTPS proxy before connecting clients.\n'
printf 'Logs: docker compose logs -f\nStop: docker compose down\nPasswords are stored privately in .env. Back up .env and the volumes.\n'
