# Bootstrapping

## Developer setup

This setup describes how you can bootstrap nextapp on your development machine.
Assume that we are working in the build directory where cmake has built the
project.

** signupd **

First, create a X509 certificate on the *nextappd* server. We will use this to
authenticate *signupd*.

We assume that mariadb's root password is in `${NA_ROOT_DBPASSWD}` and that the 
db-password for `nextapp` user is in  `${NA_NEXTAPP_DB_PASSWD}` and the passwod
for the `signupd` user is in  `${NA_SIGNUP_DB_PASSWD}`.

The argument `--create-client-cert tls/signupd` will write the cert, key and a ca cert-cert
in the `./tls/` directory, and prefic the names with `signupd-`.

```sh
./bin/nextappd --db-passwd ${NA_NEXTAPP_DB_PASSWD} --admin-cert --create-client-cert tls/signupd

```

The command below will bootstrap signupd. It will create the signup database, 
install the certs, and store the public URL for the nextappd server.

```sh
./bin/signupd bootstrap --root-db-passwd ${NA_ROOT_DBPASSWD} --db-passwd ${NA_SIGNUP_DB_PASSWD} --nextapp-address https://nextapp.ws.dev.next-app.org:10321 --grpc-client-ca-cert tls/signupd-ca.pem --grpc-client-cert tls/signupd-cert.pem --grpc-client-key tls/signupd-key.pem 
```

If you need to update an existing sighupd instance, for example after you create a new database for nextapp,
you can add the `--drop-database` argument to force it to drop the current signup database.


