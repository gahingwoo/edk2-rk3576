# Releasing

English | [简体中文](releasing.zh-CN.md)

## A release

Releases are made by hand with `gh release create`; no workflow publishes
them.

1. Pick the commit on `main` and take its build: download the artifacts
   of that commit's build run, or build every board yourself
   (`scripts/build.sh`, `scripts/build-spl.sh`, `scripts/package.sh` and
   `scripts/features.py board` for `rock4d`, `cm5io`, `tspi3m`).

   ```sh
   gh run download <run-id> -R gahingwoo/edk2-rk3576 -D artifacts
   scripts/features.py release --tag 0.5.0 --out rel artifacts   # or: ... rel out
   ```

   `rel/` then has the images named `<Board>-<medium>-edk2-<tag>.img`, the
   `.sfdisk` tables, `FEATURES.json` and `SHA256SUMS.txt`; `features-notes.md`
   next to it has the feature table. Add `OFL-1.1.txt` (the licence of the
   built-in Chinese font) and run `sha256sum` again if you add files.
2. Write the notes: what changed, a file table, the feature table from
   `features-notes.md`, and "How it was tested" per board, saying which
   board ran which image.
3. Tag the commit and publish: `gh release create <tag> rel/* ...`.

`FEATURES.json` is what Maskrom Studio reads to know what each image can do
([features-json.md](features-json.md)). Every board needs a
`features.json` from the same commit, so a board cannot carry an image from
an older release.

Every release must carry exactly one `ROCK4D-spi-edk2-*.img`, one
`CM5IO-emmc-edk2-*.img` and one `TaishanPi3M-emmc-edk2-*.img`. The web
flasher (gahingwoo/edk2-webflash) picks the images of the latest release by
those name patterns at each deploy, and its daily deploy fails otherwise. A
release that does not rebuild a board carries that board's previous images.

flash.gahingwoo.com picks up a new release at its next daily run, or at once
with a manual run of its deploy workflow.

## The rolling ci release

`build.yml` replaces the pre-release tagged `ci` after every successful
build of `main` (see [flashing](../flash.md#the-rolling-ci-build)).
Maskrom Studio's CI tier writes from it. It carries `FEATURES.json`, and
its notes end with the feature table. Runs that are not on `main` make the
same files in the job "Release files (dry run)" (artifact
`release-dry-run`) without publishing them.
