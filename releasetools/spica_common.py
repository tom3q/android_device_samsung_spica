#
# Copyright (C) 2008 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os, sys

LOCAL_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
RELEASETOOLS_DIR = os.path.abspath(os.path.join(LOCAL_DIR, '../../../build/tools/releasetools'))

# Add releasetools directory to python path
sys.path.append(RELEASETOOLS_DIR)

from common import *

def load_module_from_file(module_name, filename):
    import imp
    f = open(filename, 'r')
    module = imp.load_module(module_name, f, filename, ('', 'U', 1))
    f.close()
    return module

# map recovery.fstab's fs_types to mount/format "partition types"
PARTITION_TYPES = { "yaffs2": "MTD", "mtd": "MTD",
                    "ext2": "EMMC", "ext3": "EMMC",
                    "ext4": "EMMC", "vfat": "EMMC",
                    "emmc": "EMMC", "bml" : "BML",
                    "ubifs" : "UBI" }

def GetTypeAndDevice(mount_point, info):
  fstab = info["fstab"]
  if fstab:
    return PARTITION_TYPES[fstab[mount_point].fs_type], fstab[mount_point].device
  else:
    return None
