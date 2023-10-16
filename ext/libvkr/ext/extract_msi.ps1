# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

# We have to replace forward slashes because msiexec only understands backslashes.
$archive=$args[0] -replace '[/]','\'
$target=$args[1] -replace '[/]','\'
echo "Extracting $archive to $target ..."
msiexec.exe /a "$archive" /qn TARGETDIR="$target"
