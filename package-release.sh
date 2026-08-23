#!/bin/bash
# Builds everything fresh and stages release-ready disk images in
# Disks/ with clean names. Disks/ is covered by the existing *.dsk
# and *.hda .gitignore patterns -- this is local staging, not
# something committed to git or published anywhere by itself.
set -e

cd "$(dirname "$0")"

DISKS_DIR="Disks"

echo "=== Building and deploying to test images ==="
./deploy.sh

echo
echo "=== Converting to a BlueSCSI device image ==="
./build-bluescsi-image.sh

echo
echo "=== Staging release artifacts in $DISKS_DIR/ ==="
mkdir -p "$DISKS_DIR"
# "HD1_" prefix matches BlueSCSI's filename convention for assigning
# an image to SCSI ID 1, so this file can be dropped onto an SD card
# as-is with no renaming needed.
cp "vmac/DodgerType.hda" "$DISKS_DIR/HD1_DodgerType.hda"
cp "vmac/ArtfulType_20M.dsk" "$DISKS_DIR/DodgerType-20MB.dsk"
cp "vmac/Mac-800K.dsk" "$DISKS_DIR/DodgerType-800K.dsk"

echo
echo "Done. Contents of $DISKS_DIR/:"
ls -la "$DISKS_DIR"
