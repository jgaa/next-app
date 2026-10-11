# NextApp self-hosted backend

NextApp is a personal organizer based on Getting Things Done. This deployment
runs a private backend on your Linux workstation or server. Backend databases,
certificates, and application state stay on your machine; clients also keep
local copies. It uses exactly three services: MariaDB, `nextappd`, and `signupd`.
It does not use NextApp's hosted subscription or payment infrastructure.

## Requirements

- Linux with Docker Engine and permission to use its daemon.
- Docker Compose **v2** plugin (`docker compose version`).
- Bash, OpenSSL CLI, and `flock` (normally supplied by util-linux).
- Network access to Docker Hub and `ghcr.io` to download images.

Hardware minimums have not been measured. For an initial personal trial, budget
roughly 2 GB of available RAM and several GB of free disk for images and data;
these are planning estimates, not tested minimum requirements. Monitor actual
usage with `docker stats` and `docker system df`. Leave additional space for
backups and image updates.

All three images intentionally use `latest`. Pulls can change both daemon and
MariaDB versions. Read release notes and back up before upgrading.

## Quick start

```bash
git clone https://github.com/jgaa/next-app.git
cd next-app/self-hosted
./bootstrap.sh
```

The script asks for the NextApp hostname/IP that **clients can reach**, the two
published ports, and each host bind address. Defaults (`localhost`, ports 10321
and 10322, binds 127.0.0.1) work for a desktop client on the same machine.
Bind addresses control which host interfaces accept connections; they are
independent of addresses announced to clients.

The signup protocol and public URL describe the endpoint clients use. Selecting
HTTPS requires you to provide an external TLS proxy; signupd continues to listen
on plaintext HTTP/2 internally. No reverse proxy is installed by this script.
The public signup URL may use a different hostname or port from NextApp.

Bootstrap creates a private `.env` (mode 0600), starts MariaDB, invokes the
daemons' existing bootstrap commands, validates certificates, and starts the
services. It checks that signupd has recorded an authenticated initial connection
to NextApp. Existing credentials, CA, databases, and complete client certificates
are preserved on subsequent runs.

Example completion:

```text
Installation completed.
NextApp endpoint: https://localhost:10321
Client signup URL: http://localhost:10322
Logs: docker compose logs -f
Stop: docker compose down
Passwords are stored privately in .env. Back up .env and the volumes.
```

For automation:

```bash
./bootstrap.sh --non-interactive \
  --host nextapp.example.org --nextapp-bind 0.0.0.0 \
  --signup-protocol https --signup-public-url https://signup.example.org
```

Run `./bootstrap.sh --help` for all options. IPv4 addresses and DNS names are
supported; the script currently rejects IPv6 deployment addresses. IP-based
NextApp endpoints require an image containing the IP SAN fix in this repository.
An older image may incorrectly issue DNS SANs for IPs; bootstrap detects this
and stops. Use a DNS hostname resolving to the host until the fix is published.

`.env.example` describes the format, but its password placeholders are not usable
credentials. Let bootstrap generate `.env`, or prepare all values manually using
independent `openssl rand -hex 32` passwords. Use unquoted `KEY=value` lines.
The script does not execute `.env` as shell code. On rerun, installation options
are rejected instead of silently overriding the existing configuration.
Edit non-secret settings in `.env` explicitly; hostname changes use the option
below. Port changes also require updating the address stored by signupd: edit
`NEXTAPP_PORT`, then run `./bootstrap.sh --update-host` with the current hostname.

Each installation needs a separate directory, a unique `COMPOSE_PROJECT_NAME`
(set with `--project-name` on first setup), and non-conflicting published ports.
Run all Compose commands from that installation's directory. Do not override
its project name or installation settings through exported shell variables.

### Connect a client

On desktop or Android, open the initial signup wizard. On **Choose a cloud server
to get started**, put the printed **Client signup URL** in the **Server** field,
choose **New subscription**, and press **Connect**. This label is also used for
self-hosted accounts; this deployment has no payment service. Complete the
account wizard. Signup returns the NextApp endpoint, private CA certificate,
and a client certificate/key; the client stores these for its mTLS connection.
Do not give end users the signup administrator password from `.env`.

For another device on an existing account, choose **Add device to existing user**
and use a one-time password obtained from your existing device. Android's
`localhost` means the phone itself: use a reachable LAN/VPN hostname/IP and bind
addresses, or a public deployment. A client already configured for another
backend must use the client's existing reset/account migration procedure before
onboarding to this one; export important data first.

## Local, LAN, VPN, and public access

- **Local trial:** Keep both bind addresses at 127.0.0.1. Use the desktop client
  on the same host.
- **Trusted LAN:** Use a reachable NextApp hostname/IP and bind the services to
  the LAN interface. Plaintext signup exposes account setup and issued private
  keys to the network. Use it only when you trust that network; HTTPS is preferable.
- **VPN:** Use a reachable VPN hostname/IP and restrict binds/firewall rules to
  VPN interfaces. Plaintext signup is acceptable only inside a trusted encrypted
  VPN connection.
- **Public internet:** NextApp uses its private CA and end-to-end mTLS. Signup
  requires publicly trusted HTTPS via your separately managed proxy. Keep the
  plaintext signup backend on 127.0.0.1 and publish only the proxy's HTTPS port.

Any non-loopback signup bind triggers a plaintext exposure warning, even if
HTTPS is advertised. Interactive setup requires typing `YES`; automation must
supply `--allow-insecure-signup`. This acknowledges direct HTTP exposure; it
does not make HTTP safe on an untrusted network. The acknowledgment is stored
in `.env`. Only bind plaintext signup to an interface with trusted access.

NextApp does **not** require Let's Encrypt. A reverse proxy in front of NextApp
must use TCP passthrough to preserve mTLS. Do not terminate NextApp TLS at an
HTTP proxy. Signup is gRPC over HTTP/2, not an ordinary web page: use the
[host Nginx example](examples/nginx-signup.conf) or
[host Caddy example](examples/caddy-signup.Caddyfile). They cover certificate
renewal and forward only to the loopback backend. Caddy uses
[h2c upstream transport](https://caddyserver.com/docs/caddyfile/directives/reverse_proxy)
and [automatic HTTPS](https://caddyserver.com/docs/automatic-https). Nginx uses
[`grpc_pass`](https://nginx.org/en/docs/http/ngx_http_grpc_module.html); Certbot's
[renewal procedure](https://eff-certbot.readthedocs.io/en/stable/using.html#renewing-certificates)
must reload Nginx after successful renewal. Configure public DNS and the relevant
ACME/firewall access before testing clients.

## Running and upgrading

```bash
docker compose up -d          # start
docker compose down           # stop and remove containers, retain volumes
docker compose logs -f        # follow logs
docker compose restart       # restart
docker compose ps            # service status
```

Back up first, review daemon and MariaDB release notes, and check that MariaDB
supports upgrading from your current database version to the newly pulled
version. `latest` may cross a major-version boundary requiring an intermediate
MariaDB upgrade; do not assume every jump is supported.

```bash
docker compose pull
docker compose up -d
docker compose logs --tail 100 mariadb nextappd signupd
./bootstrap.sh --non-interactive   # check credentials, certificates, stored configuration
```

Both daemons check their database schema version at startup and automatically
apply available migrations. MariaDB uses `MARIADB_AUTO_UPGRADE=1` for its system
tables ([official image documentation](https://mariadb.com/docs/server/server-management/automated-mariadb-deployment-and-administration/docker-and-mariadb/mariadb-server-docker-official-image-environment-variables)).
That does not guarantee support for arbitrary major-version jumps. A migrated
database may not work with an older image: rollback requires compatible images
and restoring the pre-upgrade backup. Record image digests with backups so you
can recover the matching software. Bootstrap's startup checks cannot replace an
actual client connection test after an upgrade.

## Changing hostname or IP

```bash
./bootstrap.sh --update-host nextapp.example.org
```

This stops the daemons, uses `nextappd create-grpc-cert` to replace only the server
certificate, verifies the hostname and unchanged CA, updates signupd's announced
NextApp URL and `.env`, and starts the services. The internal `nextappd` DNS SAN
is retained, so signupd connects over the Docker network instead of relying on
host routing. All client certificates and the CA stay intact. The signup public
URL remains independent: edit `SIGNUP_PUBLIC_URL`/`SIGNUP_PROTOCOL` and your proxy
if it also needs to change. Existing clients may need their NextApp endpoint
updated; changing the signup URL alone does not rewrite endpoints already stored
on clients. An interrupted update leaves `.host-update`; rerun bootstrap to
finish the same update before requesting a different hostname.

## Persistent data and backups

Volumes are prefixed by the Compose project name (default `nextapp-self-hosted`):

| Volume | Contents |
| --- | --- |
| `mariadb_data` | All databases, **NextApp CA private key**, server key/cert, users, and signupd's stored admin client credentials |
| `nextapp_certs` | Signupd's exported client certificate, private key, and public CA certificate; contains no CA private key |
| `nextapp_data` | NextApp daemon home/application files |
| `signup_data` | Signup daemon home/application files |

**`docker compose down -v` deletes named volumes and can permanently destroy
this installation's data.** Do not use it for upgrades or interrupted bootstrap
recovery. The CA private key is critical: losing it can prevent existing clients
from authenticating. The database and signup client key are sensitive backups.
Protect and encrypt backups and keep a copy on another machine. Also back up
`.env`, including the original credentials and project name.

A practical consistent backup is a cold archive. These temporary containers use
the same service images and volumes, without starting their normal entrypoints:

```bash
(
set -euo pipefail
umask 077
backup_dir="backups/$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$backup_dir"
cp .env "$backup_dir/env"
docker image inspect $(docker compose images -q) \
  --format '{{json .RepoDigests}}' > "$backup_dir/image-digests.json"
docker compose stop
# Stop here on any failed archive command; do not treat a partial backup as valid.
docker compose run --rm --no-deps -T --entrypoint tar mariadb \
  -C /var/lib/mysql -czf - . > "$backup_dir/mariadb.tar.gz"
docker compose run --rm --no-deps -T --entrypoint tar nextappd \
  -C / -czf - var/home/nextappd var/lib/nextappd > "$backup_dir/nextapp.tar.gz"
docker compose run --rm --no-deps -T --entrypoint tar signupd \
  -C /var/home/signupd -czf - . > "$backup_dir/signup.tar.gz"
for archive in "$backup_dir"/*.tar.gz; do tar -tzf "$archive" >/dev/null; done
docker compose up -d
)
```

For recovery, use matching image digests from the backup (edit the Compose image
references), the saved `.env`, and **empty destination volumes**. Never unpack a
MariaDB archive over a running or partially initialized database. For a separate
restore test, use another directory/project name and unused host ports; keep
the original installation intact.

```bash
(
set -euo pipefail
# In the restore directory, after copying saved env to .env:
chmod 600 .env
# Keep services stopped throughout extraction.
docker compose run --rm --no-deps -T --entrypoint tar mariadb \
  -C /var/lib/mysql -xzf - < /path/to/backup/mariadb.tar.gz
docker compose run --rm --no-deps -T --entrypoint tar nextappd \
  -C / -xzf - < /path/to/backup/nextapp.tar.gz
docker compose run --rm --no-deps -T --entrypoint tar signupd \
  -C /var/home/signupd -xzf - < /path/to/backup/signup.tar.gz
./bootstrap.sh --non-interactive
)
```

Test restoration and a real client connection before depending on your backups.
Use original ownership (the application images currently run as UID 999).
Archive extraction by the original service users preserves usable permissions.

## Troubleshooting

**Docker unavailable:** Run `docker info` and `docker compose version`. Start
your Docker service and resolve socket permissions using your distribution's
Docker setup instructions. Do not make the socket world-writable.

**Port conflict:** Run `ss -ltn` and `docker compose ps`. Set unused published
ports in `.env`. Changing `NEXTAPP_PORT` also changes the client endpoint; apply
`--update-host` with the existing hostname to update signupd's announcement.

**MariaDB unhealthy:** Run `docker compose logs --tail 100 mariadb` and
`docker inspect --format '{{json .State.Health}}' "$(docker compose ps -q mariadb)"`.
Check disk space (`df -h`), permissions, and supported MariaDB upgrade paths.
The health probe waits for connection readiness and InnoDB initialization, using
the [official healthcheck script](https://mariadb.com/docs/server/server-management/automated-mariadb-deployment-and-administration/docker-and-mariadb/using-healthcheck-sh).
MariaDB has no published host ports; inspect with `docker compose port mariadb 3306`
(which should report no mapping) and `docker compose config`.

**Interrupted bootstrap:** Rerun `./bootstrap.sh --non-interactive`. Completed
database steps are detected from actual database contents, not local success
markers. The daemon CLI refuses to bootstrap an existing database; the script
never uses `--drop-database`. Interruptions between complete steps resume safely.
If interruption happened **inside database creation/migration**, missing tables,
CA, admin, or instance configuration stop setup rather than silently resetting
it. Keep `.env` and all volumes, inspect daemon logs, and take a cold backup of
the stopped installation before repair. Restore the last complete backup into
empty recovery volumes. If this was the first setup and there is no complete
backup, preserve the failed installation for diagnosis and request a database
repair, or create a separate fresh installation with a new directory, project
name, and ports. No automatic destructive recovery is provided.

A partial set of exported signup client files also stops setup. Back up those
files and inspect the matching device entry in the NextApp database before an
operator repairs/replaces them. Never rerun `create-client-cert` over existing
files blindly: it registers another device and overwrites the output files.

**Wrong hostname/certificate:** Use `./bootstrap.sh --update-host HOST` on a
completed installation. For an interruption before signupd was initialized,
first confirm the existing CA row is present, back up, then run the existing CLI:

```bash
docker compose run --rm --no-deps nextappd create-grpc-cert \
  --config '' --db-host mariadb --db-port 3306 \
  --server-fqdn YOUR_CONFIGURED_HOST --server-fqdn nextappd
./bootstrap.sh --non-interactive
```

This keeps an existing CA; do not run it against a database with a missing CA.
IP SAN validation failures require an image containing the certificate fix or
a DNS hostname. Correct DNS resolution separately with `getent hosts HOST`.

**Trust failure/client cannot connect:** Check the client's signup URL, public
DNS, firewall, and published bind addresses. Run
`docker compose logs --tail 100 nextappd signupd`. HTTPS signup must present a
valid publicly trusted certificate and preserve HTTP/2 through the proxy.
NextApp must use its original private CA and mTLS: a bare `curl` request without
a client certificate is not a successful NextApp connection test. Do not replace
the CA to fix a trust error. Verify the certificates/keys belong to this
installation and check the client's clock and certificate expiry.

**Database credential mismatch:** Password environment variables only initialize
a fresh database. Editing `.env` does not rotate database users. Restore the
original `.env` from your backup; do not delete the database volume. If `.env`
is missing while project volumes exist, bootstrap refuses to generate replacement
credentials. Use an explicit database credential rotation procedure if needed.

**Permissions:** Run `docker compose run --rm --no-deps --entrypoint id nextappd`
and `docker compose run --rm --no-deps --entrypoint ls nextappd -ld /var/lib/nextappd
/var/home/nextappd` (on one line). Confirm restored volumes match the image user.
Do not solve permission failures by granting everyone access to private keys.

**Signup instance mismatch:** The public NextApp URL is stored in signupd's
MariaDB database at bootstrap. Changing `.env` alone cannot change it. Use
`--update-host` for a complete single-instance installation. Missing initial
instance registration follows the interrupted-bootstrap recovery above.

## Security notes

Passwords are in `.env`; keep it private and out of version control. Docker
administrators can inspect container environment credentials. The internal
backend network carries database traffic and attaches all three services;
MariaDB has no frontend network or published ports. Only the two application
ports are published. Do not publish database, metrics, or admin ports needlessly.
The CA private key remains in MariaDB and is not mounted into signupd; signupd
has only its own administrative client identity, which is also sensitive.
Protect backups as carefully as live data. Use HTTPS signup on untrusted networks,
and restrict the loopback proxy backend so clients cannot bypass TLS.

