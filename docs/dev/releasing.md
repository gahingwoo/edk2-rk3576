# Releasing

English | [简体中文](releasing.zh-CN.md)

## A release

Releases are made by hand with `gh release create`; no workflow publishes
them.

1. Pick the commit on `main` and build every board from it
   (`scripts/build.sh` and `scripts/package.sh` for `rock4d`, `cm5io`,
   `tspi3m`), or take the images of that commit's `ci` build.
2. Name the files `<Board>-<medium>-edk2-<ver>.img` and add
   `SHA256SUMS.txt`, the `.sfdisk` tables and `OFL-1.1.txt` (the licence of
   the built-in Chinese font).
3. Write the notes: what changed, a file table, and "How it was tested" per
   board, saying which board ran which image.
4. Tag the commit and publish.

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
Maskrom Studio's CI tier writes from it.
