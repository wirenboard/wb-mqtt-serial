# Running wb-mqtt-serial tests locally

## Prerequisites

1. **Docker** with the `contactless/devenv` image:

   ```bash
   docker pull contactless/devenv
   ```

2. **qemu-user-binfmt** on the host (needed to run cross-compiled ARM binaries on x86_64):

   ```bash
   sudo apt-get install qemu-user-binfmt
   ```

   Verify registration:

   ```bash
   cat /proc/sys/fs/binfmt_misc/qemu-arm
   ```

   Should show `enabled` with the `F` (fixed) flag.

3. **wbdev** script (wraps Docker invocations):

   ```bash
   curl -sL "https://raw.githubusercontent.com/wirenboard/wirenboard/master/wbdev" -o /usr/local/bin/wbdev
   chmod +x /usr/local/bin/wbdev
   ```

4. **Git submodules** initialized (gurux DLMS library):

   ```bash
   git submodule update --init --recursive
   ```

   If SSH access to GitHub is not configured, clone manually:

   ```bash
   git clone https://github.com/wirenboard/Gurux.DLMS.cpp.git thirdparty/gurux
   cd thirdparty/gurux
   git checkout $(git -C ../.. ls-tree HEAD thirdparty/gurux | awk '{print $3}')
   ```

## Running tests

From the wb-mqtt-serial directory:

```bash
WBDEV_TARGET=bullseye-armhf WBDEV_USE_UNSTABLE_DEPS=y wbdev cdeb -j$(nproc)
```

This performs a full Debian package build inside an sbuild chroot, which includes:

1. Installing build dependencies from the Wiren Board APT repository (`wb6/bullseye`)
2. Generating JSON templates from Jinja2 sources
3. Cross-compiling all sources for armhf
4. Running the test suite (`test/wb-homa-test`) via qemu-user
5. Building `.deb` packages

### Key environment variables

| Variable | Description |
|---|---|
| `WBDEV_TARGET` | Build target. Use `bullseye-armhf` or `bullseye-arm64`. The `bullseye-amd64` target uses `dev-tools` repo which lacks `libwbmqtt1-5`. |
| `WBDEV_USE_UNSTABLE_DEPS=y` | Pull dependencies from the `unstable` suite in addition to `stable`. Required if the current code depends on unreleased library versions. |
| `WBDEV_IMAGE` | Override the Docker image (default: `contactless/devenv:latest`). |

### Why not build natively on amd64?

The Wiren Board APT repository (`deb.wirenboard.com`) does not publish `libwbmqtt1-5-dev` for `amd64`. The packages are only available for `armhf` and `arm64` in the `wb6/bullseye` and `wb8/bullseye` repository paths. Cross-compilation with qemu-user-binfmt is the supported way to build and test on x86_64 hosts.

## Interpreting results

A successful build ends with:

```
[  PASSED  ] 234 tests.
Status: successful
```

Test failures appear as:

```
[  FAILED  ] TestSuite.TestName
 1 FAILED TEST
Status: attempted
Fail-Stage: build
```

The test binary also runs under valgrind on non-ARM hosts (when not cross-compiling), so memory errors will cause exit code 180.
