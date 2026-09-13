# GitHub setup for `nemarpuc/Libaoa_hid`

This file describes a clean first publication as `v0.1.0`. Delete the failed
remote repository before following it; no old commits, tags, Actions runs, or
Releases are reused. It does **not** assert that the new repository has been
created or that a successful Release exists.

## 0. Remove the failed repository

In the existing GitHub repository, open **Settings → General → Danger Zone →
Delete this repository**, enter `nemarpuc/Libaoa_hid`, and confirm deletion.
This permanently removes the old remote commits, tags, workflow runs, and any
draft Release. Keep the corrected ZIP locally before deleting it.

## 1. Create the empty repository

1. Sign in to GitHub as `nemarpuc` and open
   [Create a new repository](https://github.com/new).
2. Set **Repository name** to `Libaoa_hid` with exactly that spelling and case.
3. Public visibility is recommended for an open-source MIT release and avoids
   private-repository Actions minute charges.
4. Do not initialize the repository with a README, `.gitignore`, or license;
   all three are already supplied by this tree.

The release workflow deliberately refuses to publish from any repository other
than `nemarpuc/Libaoa_hid`. The product, library, and ABI names remain
`libaoahid`, `aoahid.dll`/`libaoahid.so`, and `aoahid_*`.

## 2. Make the first push

Run these commands from the directory containing this file:

```sh
git init -b main
git config user.name "nemarpuc"
git config user.email "YOUR_GITHUB_EMAIL"  # replace with the email on your GitHub account
git add -A
git status --short
git commit -m "Initial libaoahid 0.1.0 source"
git remote add origin https://github.com/nemarpuc/Libaoa_hid.git
git push -u origin main
```

Before committing, `git status --short` must not list local output trees such
as `build-manual/`, `build-*`, `dist/`, `out/`, `.vcpkg/`, or
`.pytest_cache/`. They are ignored by the supplied `.gitignore` and are not
release source.

## 3. Repository settings

In **Settings → Actions → General**:

- enable GitHub Actions;
- allow the actions used by this repository. The workflows pin every external
  action to a complete 40-character commit SHA; required publishers are
  `actions`, `gradle`, and `pypa`;
- keep the default `GITHUB_TOKEN` permission restricted to read access. The
  release workflow grants only its publishing job `contents: write`, while the
  documentation deployment grants only `pages: write` and `id-token: write`;
- leave **Allow GitHub Actions to create and approve pull requests** disabled.

If an organization policy prevents a workflow from requesting
`contents: write`, that policy must explicitly permit the Release workflow to
create a GitHub Release. No personal access token is required by the supplied
workflow.

The native matrix uses `ubuntu-22.04`, `ubuntu-22.04-arm`,
`windows-2025-vs2026`, and `windows-11-vs2026-arm`. GitHub's
[official runner-image list](https://github.com/actions/runner-images#available-images)
listed all four labels on 2026-08-27; the
[Windows ARM64 GA notice](https://github.com/actions/runner-images/issues/14592)
also records Visual Studio 2026 and a CMake version supporting its generator.
Recheck that live list before tagging if publication is delayed substantially.

In **Settings → Pages**, select **GitHub Actions** as the source. A successful
Documentation workflow on `main` can then deploy to
`https://nemarpuc.github.io/Libaoa_hid/`. After Pages is enabled, create the
Actions repository variable `AOAHID_DEPLOY_PAGES` with the exact value `true`.
Until both settings exist, the workflow still builds and validates all
documentation but deliberately skips deployment instead of failing an unrelated
library release check.

In **Settings → Code security and analysis**, enable the dependency graph,
Dependabot alerts, secret scanning, and push protection where GitHub offers
them. For a public repository,
[GitHub CodeQL default setup](https://docs.github.com/code-security/code-scanning/enabling-code-scanning/configuring-default-setup-for-code-scanning)
is the preferred starting point: select C/C++ and GitHub Actions, then verify
its first scan. CodeQL is an additional security check and is not represented
as part of the native release gate until a successful repository run exists.

A dependency-review workflow is not pre-enabled because its API requires the
repository dependency graph, which cannot have a successful snapshot before
the first push. After the graph is populated, follow GitHub's
[dependency-review setup](https://docs.github.com/en/code-security/how-tos/secure-your-supply-chain/manage-your-dependency-security/configure-dependency-review-action)
and make that check required only after its first successful pull-request run.

## 4. Optional registry publishing

The native GitHub Release needs no repository variable or user-supplied secret.
Leave `AOAHID_PUBLISH_BINDINGS` absent (or not equal to `true`) for the first
release.

Only after ownership of both registry names has been confirmed:

- configure a PyPI Trusted Publisher for project `aoahid`, owner `nemarpuc`,
  repository `Libaoa_hid`, workflow `release.yml`, with no environment unless
  the workflow is changed to name one;
- add an Actions secret named `NUGET_API_KEY` scoped to publish only the
  `AoaHid` package;
- create the Actions repository variable `AOAHID_PUBLISH_BINDINGS` with the
  exact value `true`.

The Python wheel and NuGet package contain declarations only; native libraries
remain separate GitHub Release downloads.

## 5. Create release `v0.1.0`

Before tagging, a Linux developer machine with Ninja and a GNU or Clang
toolchain can reproduce the repository's ThreadSanitizer configuration:

```sh
cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan
```

This is a race-detection build, not a performance benchmark. It uses the
deterministic fake USB backend and therefore does not establish Android-device
latency or compatibility. The suite's exact allocation, synchronization,
event-operation, and blocking-wakeup checks are explained in
[LATENCY.md](docs/LATENCY.md). A successful local run is also not a substitute
for the exact-commit GitHub Actions result.

Wait for the **CI** workflow on the exact `main` commit to succeed. Its Linux
sanitizer matrix includes separate full-suite shared and static-only
`AOAHID_TSAN=ON` builds in addition to ASan/UBSan builds, and its quality job
builds the Doxygen site with warnings treated as errors. Then:

```sh
git status --short
git tag -a v0.1.0 -m "libaoahid 0.1.0"
git push origin v0.1.0
```

An empty `git status --short` output confirms that the tag points only to
committed content. The tag push starts both CI and Release. Release verifies
that all embedded versions equal `0.1.0`, builds and architecture-checks four
targets, assembles a draft, verifies every uploaded byte, and publishes only
after an exact-commit CI run succeeds.

The published release is expected to have 23 uploaded assets:

- eight complete native archives: shared and static variants for Linux x86_64,
  Linux AArch64, Windows x64, and Windows ARM64;
- eight matching SPDX 2.3 JSON sidecars;
- four `-runtime` bundles carrying the shared libraries plus the complete
  MIT and LGPL-2.1 license set and libusb's corresponding source;
- one deterministic `libaoahid-0.1.0-source.tar.gz` tagged-tree archive;
- `release-manifest.json` and `SHA256SUMS`.

GitHub's automatically generated source-code links appear separately. A failed
workflow cannot remove the pushed tag, and the workflow will not overwrite an
existing release for that tag. Because this procedure starts with a newly
created empty repository, `v0.1.0` is unique and needs no force update.
