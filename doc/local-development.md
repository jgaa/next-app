# How to Work with NextApp Locally

NextApp is developed under Linux. The server components only run under Linux.

This documentation assumes that you use Linux.

## Compile from Sources

TBD

## Run the Back End Locally from the Command Line

This section describes how to run the backend and client locally using Docker to run MariaDB.

First, start a *throw-away* MariaDB instance. We will not persist data, so all data will exist only until the container stops.

```sh
./scripts/mariadb-container.sh
```

Or, if you want to use a non-standard port for MariaDB, give the container a specific name, or specify a password:

```sh
NA_ROOT_DBPASSWD=secret \
  NA_MARIADB_PORT=33006 \
  NA_MARIADB_NAME=nadb-tmp \
  ./scripts/mariadb-container.sh

$ docker ps
CONTAINER ID   IMAGE            COMMAND                  CREATED         STATUS         PORTS                       NAMES
344518659cd4   mariadb:latest   "docker-entrypoint.s…"   3 seconds ago   Up 2 seconds   127.0.0.1:33006->3306/tcp   nadb-tmp
```

Now, let’s bootstrap the NextApp backend. We start with the main server, `nextappd`.

Change to the build directory and run:

```sh
./bin/nextappd bootstrap \
  --root-db-passwd secret \
  --db-passwd also-secret \
  --db-port 33006 \
  -l trace \
  -L /tmp/nextappd.log \
  -T \
  --server-fqdn localhost \
  -c ''
```

Next, we need to create a client certificate for the `signupd` service.

```sh
./bin/nextappd create-client-cert \
  --db-passwd also-secret \
  --db-port 33006 \
  -l trace \
  -L /tmp/nextappd.log \
  -T \
  --cert-name tls/signup \
  --admin-cert \
  -c ''
```

You should now have an admin certificate for `signupd` in the `tls` folder in your build directory.

Next, start the server from the command line.

If you plan to step through the server with a debugger, you should add the CLI argument `--disable-grpc-keepalive` to prevent user sessions from being closed while execution is paused and client timeout events are delayed.

If you want to see full gRPC messages in the logs, add the CLI argument `--log-messages 1`.

```sh
./bin/nextappd \
  --db-passwd also-secret \
  --db-port 33006 \
  -l trace \
  -L /tmp/nextappd.log \
  -T \
  -c ''
```

Now, open another shell and change to your build directory. We must also start the signup server. This is the service that the client initially connects to in order to create an account on the `nextappd` server.

```sh
./bin/signupd bootstrap \
  --root-db-passwd secret \
  --db-passwd also-secret \
  --db-port 33006 \
  -l trace \
  -L /tmp/nextappd.log \
  -T \
  --grpc-client-ca-cert tls/signup-ca.pem \
  --grpc-client-cert tls/signup-cert.pem \
  --grpc-client-key tls/signup-key.pem
```

Finally, start the signup server so the desktop app can connect to the backend.

```sh
./bin/signupd \
  --db-passwd also-secret \
  --db-port 33006 \
  -l trace \
  -L /tmp/signupd.log \
  -T \
  -c '' \
  --grpc-tls-mode none
```

The `--grpc-tls-mode none` argument tells the server **not** to use a TLS certificate for the gRPC interface used by the client. **Never do this in staging or production.**

With this setting, the NextApp desktop app must connect to:

```
http://localhost:10322
```
