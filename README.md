# Licensebox CLI for shell scripts

Linux native application licensing for POSIX shell, Bash, Fish and PowerShell. The `licensebox` executable delegates to the maintained Licensebox C v0.4.0 library. It contains no server administration functionality, private signing keys or application source.

Build/install the native library with `bash scripts/install-native.sh /your/prefix`, then build the CLI:

```sh
export LICENSEBOX_LIB_DIR=/your/prefix/lib
export LD_LIBRARY_PATH=/your/prefix/lib
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
cmake --install build --prefix /your/prefix
```

Requires Linux, a C compiler, CMake 3.20+, json-c, OpenSSL and libcurl. On disposable Ubuntu CI, `bash ci/setup.sh` installs the CLI build prerequisites; the native dependency is installed separately. No other platform is validated. Add `/your/prefix/bin` to PATH. The executable resolves `liblicensebox.so` using the Linux loader; `LICENSEBOX_LIBRARY` is a CI convention and does not override that loader. The binary install contains only the executable and LICENSE.

Create a JSON file containing administrator-provided **public** verification keys (`ed25519-mldsa65:...`):

```json
{"current":"YOUR_UNPADDED_BASE64URL_PUBLIC_KEY"}
```

Keep private signing keys out of applications, shell arguments and this file. Global options precede the command:

```sh
licensebox --product your-product --keys /path/public-keys.json \
  --version 1.0 --directory /private/application/state \
  sign-in --timeout 120
licensebox --product your-product --keys /path/public-keys.json require export
```

Optional global options are `--version APP_VERSION`, `--directory DIR`, `--server URL` and `--ca-bundle PEM`. Omitting them uses the native defaults. HTTPS certificate and hostname verification remain enabled. HTTP is accepted only for loopback development servers. There are no clock, machine-binding or identity override flags/environment variables. Native identity is mandatory; missing identity fails closed. `--help` and `--sdk-version` are standalone informational commands.

| Command | Arguments | Successful stdout |
| --- | --- | --- |
| `require` | `MODULE` | `authorized` |
| `status` | none | verified product, modules and expiry JSON |
| `sign-in` | optional `--timeout SECONDS` | `authorized` |
| `logout` | none | `logged_out` |
| `modules` | none | modules JSON |
| `request` | `MODULE...` and optional `--note TEXT` | submitted request JSON |
| `requests` | none | requests JSON |
| `access-link` | none | private URL, explicitly requested |
| `record` | `COMMAND (--success\|--failed) --elapsed SECONDS` | `recorded` |

Only `require MODULE` authorizes a protected module. Check its exit status before opening user files. Exit zero means that command succeeded; native failures use exit 1, invalid arguments use exit 2, and cancellation uses exit 130. Errors use the stable stderr form `licensebox: error_code`. Never infer authorization from stdout or a different command's success. `record` collects fixed command counts only when enabled by the server and does not authorize work. Elapsed values are seconds; never record filenames or command arguments.

A protected check may renew a lease. On a fresh installation, native protected calls can initiate device sign-in; explicitly run `sign-in` before unattended automation. Sign-in displays the verification URI and user code on stderr; the private device code is never printed. There is no lease-output command. `require` and `sign-in` print only `authorized`, while `status` omits the installation public key, native binding, user/email and token. Treat explicit `access-link` output as a credential. No command executes application processes: the calling shell controls protected work. [POSIX](examples/posix.sh), [Bash](examples/bash.sh), [Fish](examples/fish.fish) and [PowerShell](examples/powershell.ps1) examples preserve argument boundaries.

Verified unexpired cached authorization can cover eligible transport outages. Explicit policy refusals persist and block offline use. Expired leases need a connection; expiry does not itself require email authentication. Logout preserves the installation key and saved clock. There is no background heartbeat or automatic protected-process execution.

Run `bash ci/test.sh` for the installed production CLI integration tests. They use real native identity, synthetic signing keys and a loopback server, verify signed requests, exercise all commands, Unicode, usage, persisted refusals, permissions, and ensure protected checks never print leases or private device codes.

The separate test-only `build/adapter` supplies trusted synthetic clock/binding callbacks and exercises the same production operation dispatcher through all 69 shared protocol checks:

```sh
python3 ../protocol/conformance/run.py --adapter bash "$PWD/ci/adapter.sh"
```

The shared suite validates the common dispatcher/native contract; installed production flag parsing and output behavior are covered separately by `tests/integration.py`. The adapter is never installed or exposed as a CLI option. All test keys, records, URLs and hosts are synthetic.

## Optional shell checkpoints

```sh
if licensebox --product myapp --keys public-keys.json heartbeat core; then
    # Process only the next bounded protected unit here.
    :
else
    # Pause safely and preserve application results.
    exit 1
fi
```

`heartbeat MODULE` forces one signed three-second renewal without interactive
sign-in, key creation/rotation or seat transfer. It prints only `authorized` on
successful authorization; lease JSON, keys and sign-in secrets are never printed.
Its nonzero exit and stable stderr code fail closed. An eligible outage can
succeed only while the verified saved lease remains unexpired; a refusal or
missing module cannot reuse old authorization. It also works with legacy leases
without a heartbeat policy. Missing state returns `not_signed_in`.

The shell owns scheduling and protected commands; the CLI never launches them.
Use `status` for the verified signed expiry and stop by that deadline, even if a
checkpoint is slow. No failed checkpoint extends it. No background process is
started by normal require/sign-in/status commands. Configure server heartbeat
policy for shorter contact windows; a bounded command-start check alone does not
monitor arbitrarily long work.
