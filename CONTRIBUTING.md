# Contributing to GUARD-MESH

GUARD-MESH is a Guardian-focused fork of
[wadamesh](https://github.com/ALLFATHER-BV/wadamesh), maintained by
[@bubakbubak500](https://github.com/bubakbubak500). Development and contributions
are by invitation and follow Guardian's needs. Unsolicited pull requests may be
closed without review. This contribution policy does not restrict the rights
granted by the project's license.

## Access and review policy

- Only the repository owner has write/admin access initially. Additional access
  is granted only at the owner's discretion.
- Changes to `main` go through a pull request requiring **one approval**, from
  `@bubakbubak500`, the code owner for all files. That approval satisfies both the
  approval count and the code-owner requirement; no second reviewer is required.
- New commits dismiss an existing approval so the owner reviews the final change.
  A separate approval of the most recent push is not required.
- The owner retains administrator bypass, including for their own pull requests;
  GitHub does not allow authors to approve their own pull requests.
- Force pushes and deletion of `main` are blocked by branch protection. Required
  status checks and a merge queue are not part of the initial setup.
- Public visibility allows others to fork the code or propose a pull request;
  it does not grant them write or merge access to this repository.

## License of contributions

GUARD-MESH retains wadamesh's **GPL-3.0-or-later** license (see [LICENSE](LICENSE)). By
submitting a contribution you agree that it is licensed under the same terms
(inbound = outbound). This keeps every build and fork open.

Sign off your commits to certify you wrote (or have the right to submit) the
change, per the [Developer Certificate of Origin](https://developercertificate.org/):

```
git commit -s -m "..."     # adds a Signed-off-by line
```

New source files should carry an SPDX header:

```c
// SPDX-License-Identifier: GPL-3.0-or-later
```

Files derived from MeshCore keep their original **MIT** header — don't relicense
upstream code; only your own additions are GPL.

## How we work

A few principles (borrowed from how this project is built):

- **One topic per pull request.** Small, focused changes that do one thing are
  easy to review and easy to revert. Open a separate PR per concern.
- **Discuss next steps, keep the actual change scoped.** It's great to note what
  could come next — just don't bundle it into the same diff.
- **Prefer pulling in a library over hand-rolling.** If a maintained library
  solves it, depend on it (and add it to [NOTICE](NOTICE)) rather than reinventing.
- **Don't regress what ships.** wadamesh runs on real devices; changes should keep
  both boards (LilyGo T-Deck and Heltec V4 TFT) building and behaving.

## Building

The firmware is a PlatformIO project that depends on a MeshCore fork via
`lib_deps`. See [README.md](README.md) and [DEVICES.md](DEVICES.md) for the
upstream build instructions and supported boards.

## Reporting issues

For Guardian-specific work, use this fork's issue tracker and include your
board, firmware version, and steps to reproduce. Report an issue upstream only
after reproducing it on unmodified upstream firmware.
