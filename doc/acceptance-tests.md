# Acceptance Tests

This document describes how to build and run the acceptance tests for `src/NextAppUi/`.

The acceptance harness uses:
- real `nextappd`
- real `signupd`
- real MariaDB
- isolated per-device client workspaces
- Qt Test as the test runner

## Build

Configure and build the acceptance targets:

```bash
cmake -S /home/jgaa/src/next-app -B /tmp/nextapp-ui-runtime-tests -DNEXTAPP_WITH_UI=ON -DWITH_TESTS=ON
cmake --build /tmp/nextapp-ui-runtime-tests -j4 --target nextappui_acceptance_device tst_nextappui_acceptance
```

## Build Local Backend Images

If you want to run the acceptance tests against locally built backend images:

```bash
./building/build-nextapp-image.sh --scripted --tag acceptance-local
./building/build-signup-image.sh --scripted --tag acceptance-local
```

This produces:
- `jgaafromnorth/nextappd:acceptance-local`
- `jgaafromnorth/signupd:acceptance-local`

## Run

### Full Matrix

Run the current Phase 4 target matrix:

```bash
env NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 \
  NEXTAPP_ACCEPTANCE_REPOSITORY=jgaafromnorth \
  NEXTAPP_ACCEPTANCE_TAG=acceptance-local \
  NEXTAPP_ACCEPTANCE_TENANTS=3 \
  NEXTAPP_ACCEPTANCE_DEVICES_PER_TENANT=5 \
  /tmp/nextapp-ui-runtime-tests/bin/tst_nextappui_acceptance \
  backendFixtureReplicatesAcrossTenantMatrixWhenEnabled
```

### Smaller Smoke Run

For a faster local validation:

```bash
env NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 \
  NEXTAPP_ACCEPTANCE_REPOSITORY=jgaafromnorth \
  NEXTAPP_ACCEPTANCE_TAG=acceptance-local \
  NEXTAPP_ACCEPTANCE_TENANTS=2 \
  NEXTAPP_ACCEPTANCE_DEVICES_PER_TENANT=3 \
  /tmp/nextapp-ui-runtime-tests/bin/tst_nextappui_acceptance \
  backendFixtureReplicatesAcrossTenantMatrixWhenEnabled
```

## Useful Environment Variables

- `NEXTAPP_ACCEPTANCE_RUN_BACKEND=1`
  enables the real container-backed acceptance tests
- `NEXTAPP_ACCEPTANCE_REPOSITORY`
  docker repository for `nextappd` and `signupd`
- `NEXTAPP_ACCEPTANCE_TAG`
  docker tag to use for both backend images
- `NEXTAPP_ACCEPTANCE_TENANTS`
  number of tenants for the matrix test
- `NEXTAPP_ACCEPTANCE_DEVICES_PER_TENANT`
  number of devices per tenant for the matrix test
- `NEXTAPP_ACCEPTANCE_PULL=1`
  pulls backend images before startup

## Artifacts

Each run writes artifacts under:

```text
/tmp/nextapp-acceptance/run-<id>/
```

Important locations:
- backend logs:
  `/tmp/nextapp-acceptance/run-<id>/backend/logs/`
- device logs:
  `/tmp/nextapp-acceptance/run-<id>/devices/<tenant>/<device>/logs/`
- docker command stdout/stderr:
  `/tmp/nextapp-acceptance/run-<id>/artifacts/`

## Notes

- The acceptance executable sets a larger Qt Test function timeout automatically.
- The current matrix covers:
  - first-device signup
  - OTP add-device onboarding
  - scripted writes
  - disconnect/reconnect catch-up
  - forced full sync
  - checksum and row-count convergence
