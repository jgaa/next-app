#!/usr/bin/env python3
"""Orchestration regression tests; no daemon or project build is required.

Run: python3 -m unittest discover -s self-hosted/tests -v
The Docker CLI is simulated; OpenSSL certificate verification is real.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

DOCKER = r'''#!/usr/bin/env python3
import json, os, pathlib, subprocess, sys
root = pathlib.Path(os.environ['TEST_ROOT'])
p = root / 'state.json'
s = json.loads(p.read_text())
a = sys.argv[1:]
s.setdefault('calls', []).append(a)
def save(): p.write_text(json.dumps(s))
def cert(host):
    san = ('IP:' if host.replace('.', '').isdigit() and not s.get('legacy_ip_san') else 'DNS:') + host
    subprocess.run(['openssl', 'req', '-newkey', 'rsa:2048', '-nodes',
        '-keyout', str(root/'server-key.pem'), '-out', str(root/'server.csr'),
        '-subj', '/CN='+host, '-addext',
        'subjectAltName='+san+',DNS:nextappd'], check=True, capture_output=True)
    subprocess.run(['openssl', 'x509', '-req', '-in', str(root/'server.csr'),
        '-CA', str(root/'client-ca.pem'), '-CAkey', str(root/'ca-key.pem'),
        '-CAcreateserial', '-copy_extensions', 'copy', '-days', '1',
        '-out', str(root/'server-cert.pem')], check=True, capture_output=True)
    s['host'] = host
if a[:1] == ['info']: pass
elif a[:2] == ['volume', 'ls']:
    if s.get('volumes'): print('existing-volume')
elif a[:1] == ['inspect']:
    print('healthy' if '.State.Health' in a[2] else 'running 0')
elif a[:2] == ['compose', 'version']: print('Docker Compose version v2.test')
elif a[:1] == ['compose']:
    a = a[5:] # strip compose --env-file .env -f compose.yaml
    action = a[0]
    if action in ('config', 'stop'): pass
    elif action == 'ps': print(a[-1]+'-container')
    elif action == 'up': s['volumes'] = True
    elif action == 'exec':
        if '--execute=SELECT 1' in a:
            user = next(v.split('=',1)[1] for v in a if v.startswith('--user='))
            if os.environ['MYSQL_PWD'] != s[user+'_password']: save(); sys.exit(1)
        else:
            q = sys.stdin.read()
            if 'information_schema.SCHEMATA' in q:
                print(int(s.get('signup' if "'signup'" in q else 'nextapp', False)))
            elif q.startswith('SELECT version > 0'):
                print(int(s.get('ready', True)))
            elif 'SHA2(CONCAT(cert,pkey)' in q: print('unchanged-ca-fingerprint')
            elif "SELECT cert FROM nextapp.cert WHERE id='grpc-server'" in q:
                print((root/'server-cert.pem').read_text(), end='')
            elif "SELECT cert FROM nextapp.cert WHERE id='ca'" in q:
                print((root/'client-ca.pem').read_text(), end='')
            elif q.startswith('UPDATE signup.instance'):
                s['public_host'] = s['host']
            elif 'COUNT(*) FROM signup.instance WHERE' in q and 'server_id IS NOT NULL' not in q:
                print(int(s.get('public_host') == os.environ['NEXTAPP_HOST']))
            else: print(1)
    elif action == 'run':
        if '/bin/cat' in a:
            kind = a[-1].split('signup-')[-1].split('.')[0]
            print((root/('client-'+kind+'.pem')).read_text(), end='')
        elif 'echo "$n"' in a[-1]: print(3 if s.get('client') else 0)
        elif 'create-client-cert' in a:
            s['client'] = True; s['client_creations'] = s.get('client_creations',0)+1
        elif 'create-grpc-cert' in a:
            cert(a[a.index('--server-fqdn')+1]); s['server_updates'] = s.get('server_updates',0)+1
        elif 'bootstrap' in a:
            host = a[a.index('--server-fqdn')+1] if '--server-fqdn' in a else None
            if host:
                s['nextapp'] = True
                s['nextapp_password'] = os.environ['MARIADB_NEXTAPP_PASSWORD']
                cert(host)
            else:
                if s.pop('interrupt_signup', False): save(); sys.exit(17)
                s['signup'] = True
                s['signup_password'] = os.environ['MARIADB_SIGNUP_PASSWORD']
                s['public_host'] = os.environ['NEXTAPP_HOST']
        else: raise RuntimeError(a)
    else: raise RuntimeError(a)
else: raise RuntimeError(a)
save()
'''


class BootstrapTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='nextapp-bootstrap-test-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        for file in ('bootstrap.sh', 'compose.yaml'):
            shutil.copy2(ROOT/file, self.root/file)
        self.bin = self.root/'bin'
        self.bin.mkdir()
        (self.bin/'docker').write_text(DOCKER)
        (self.bin/'sleep').write_text('#!/bin/sh\nexit 0\n')
        for file in self.bin.iterdir(): file.chmod(0o755)
        self.env = dict(os.environ, TEST_ROOT=str(self.root),
                        PATH=str(self.bin)+':'+os.environ['PATH'])
        (self.root/'state.json').write_text('{}')
        # A genuine CA-signed client identity tests parsing, pairing and trust.
        self.openssl('req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                     '-keyout', 'ca-key.pem', '-out', 'client-ca.pem', '-days', '1', '-subj', '/CN=TestCA')
        self.openssl('req', '-newkey', 'rsa:2048', '-nodes',
                     '-keyout', 'client-key.pem', '-out', 'client.csr', '-subj', '/CN=Signup')
        self.openssl('x509', '-req', '-in', 'client.csr', '-CA', 'client-ca.pem',
                     '-CAkey', 'ca-key.pem', '-CAcreateserial', '-out', 'client-cert.pem', '-days', '1')

    def openssl(self, *args):
        subprocess.run(['openssl', *args], cwd=self.root, check=True, capture_output=True)

    def state(self): return json.loads((self.root/'state.json').read_text())

    def change_state(self, **values):
        s = self.state(); s.update(values)
        (self.root/'state.json').write_text(json.dumps(s))

    def run_setup(self, *args, ok=True):
        r = subprocess.run(['bash', 'bootstrap.sh', '--non-interactive', *args],
                           cwd=self.root, env=self.env, capture_output=True, text=True)
        if ok: self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
        else: self.assertNotEqual(r.returncode, 0, r.stdout+r.stderr)
        return r

    def test_fresh_and_second_run_preserve_credentials_and_identity(self):
        r = self.run_setup()
        original = (self.root/'.env').read_bytes()
        self.assertEqual((self.root/'.env').stat().st_mode & 0o777, 0o600)
        values = dict(line.split('=',1) for line in original.decode().splitlines())
        secrets = [v for k,v in values.items() if 'PASSWORD' in k]
        self.assertEqual(len(set(secrets)), 4)
        for secret in secrets:
            self.assertRegex(secret, '^[0-9a-f]{64}$')
            self.assertNotIn(secret, r.stdout+r.stderr)
        self.run_setup()
        self.assertEqual(original, (self.root/'.env').read_bytes())
        self.assertEqual(self.state()['client_creations'], 1)
        self.assertNotIn('server_updates', self.state())

    def test_resume_between_steps(self):
        self.change_state(interrupt_signup=True)
        self.run_setup(ok=False)
        original = (self.root/'.env').read_bytes()
        self.run_setup()
        self.assertEqual(original, (self.root/'.env').read_bytes())
        self.assertEqual(self.state()['client_creations'], 1)

    def test_hostname_update_preserves_client_and_passwords(self):
        self.run_setup()
        before = dict(line.split('=',1) for line in (self.root/'.env').read_text().splitlines())
        self.run_setup('--update-host', 'new.example.org')
        after = dict(line.split('=',1) for line in (self.root/'.env').read_text().splitlines())
        self.assertEqual(after.pop('NEXTAPP_HOST'), 'new.example.org')
        before.pop('NEXTAPP_HOST'); self.assertEqual(before, after)
        self.assertEqual(self.state()['client_creations'], 1)
        self.assertEqual(self.state()['server_updates'], 1)
        self.assertFalse((self.root/'.host-update').exists())

    def test_pending_hostname_update_resumes(self):
        self.run_setup()
        (self.root/'.host-update').write_text('pending.example.org\n')
        self.run_setup()
        self.assertEqual(self.state()['public_host'], 'pending.example.org')

    def test_ipv4_certificate(self): self.run_setup('--host', '192.168.1.10')

    def test_legacy_dns_san_for_ip_is_rejected(self):
        self.change_state(legacy_ip_san=True)
        self.run_setup('--host', '192.168.1.10', ok=False)
        self.assertFalse(self.state().get('signup', False))
        self.assertNotIn('client_creations', self.state())

    def test_foreign_server_certificate_rejected(self):
        self.run_setup()
        self.openssl('req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                     '-keyout', 'foreign-key.pem', '-out', 'server-cert.pem',
                     '-days', '1', '-subj', '/CN=localhost', '-addext',
                     'subjectAltName=DNS:localhost,DNS:nextappd')
        self.run_setup(ok=False)
        self.assertEqual(self.state()['client_creations'], 1)

    def test_invalid_values_never_write_env(self):
        for args in [('--host', 'bad;host'), ('--host','999.1.1.1'),
                     ('--nextapp-port','65536'), ('--nextapp-port','010'),
                     ('--signup-bind','::'), ('--signup-public-url','https://bad/path'),
                     ('--project-name','Invalid'), ('--host','-invalid')]:
            with self.subTest(args=args):
                self.run_setup(*args, ok=False)
                self.assertFalse((self.root/'.env').exists())

    def test_plaintext_exposure_requires_acknowledgment(self):
        self.run_setup('--signup-bind','0.0.0.0', ok=False)
        self.assertFalse((self.root/'.env').exists())
        self.run_setup('--signup-bind','0.0.0.0','--allow-insecure-signup')
        self.run_setup()

    def test_missing_env_with_existing_volume_refuses_new_passwords(self):
        self.change_state(volumes=True)
        self.run_setup(ok=False)
        self.assertFalse((self.root/'.env').exists())

    def test_partial_database_stops_without_rebootstrap(self):
        self.run_setup()
        self.change_state(ready=False)
        before = len(self.state()['calls'])
        self.run_setup(ok=False)
        calls = self.state()['calls'][before:]
        self.assertFalse(any('bootstrap' in a for a in calls))

    def test_existing_env_setup_override_rejected(self):
        self.run_setup()
        original = (self.root/'.env').read_bytes()
        self.run_setup('--host','other.example.org', ok=False)
        self.assertEqual(original, (self.root/'.env').read_bytes())

    def test_env_is_not_executed(self):
        self.run_setup()
        p = self.root/'.env'
        p.write_text(p.read_text().replace('NEXTAPP_HOST=localhost', 'NEXTAPP_HOST=$(touch PWNED)'))
        self.run_setup(ok=False)
        self.assertFalse((self.root/'PWNED').exists())

    def test_changed_database_password_is_detected(self):
        self.run_setup()
        p = self.root/'.env'
        lines = p.read_text().splitlines()
        p.write_text('\n'.join('MARIADB_NEXTAPP_PASSWORD='+'a'*64 if line.startswith('MARIADB_NEXTAPP_PASSWORD=') else line for line in lines)+'\n')
        self.assertIn('does not match', self.run_setup(ok=False).stderr)


if __name__ == '__main__': unittest.main()
