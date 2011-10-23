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
VENDOR_SAMSUNG_DIR = os.path.abspath(os.path.join(LOCAL_DIR, '../../../vendor/samsung'))

import edify_generator
import spica_common as common

class EdifyGenerator(edify_generator.EdifyGenerator):
  def Mount(self, mount_point):
    """Mount the partition with the given mount_point."""
    fstab = self.info.get("fstab", None)
    if fstab:
      p = fstab[mount_point]
      self.script.append('mount("%s", "%s", "%s", "%s");' %
                         (p.fs_type, common.PARTITION_TYPES[p.fs_type],
                          p.device, p.mount_point))
      self.mounts.add(p.mount_point)

  def FormatPartition(self, partition):
    """Format the given partition, specified by its mount point (eg,
    "/system")."""

    fstab = self.info.get("fstab", None)
    if fstab:
      p = fstab[partition]
      self.script.append('format("%s", "%s", "%s");' %
                         (p.fs_type, common.PARTITION_TYPES[p.fs_type], p.device))

  def WriteRawImage(self, mount_point, fn):
    """Write the given package file into the partition for the given
    mount point."""

    fstab = self.info["fstab"]
    if fstab:
      p = fstab[mount_point]
      partition_type = common.PARTITION_TYPES[p.fs_type]
      args = {'device': p.device, 'fn': fn}
      if partition_type == "MTD":
        self.script.append(
            ('assert(package_extract_file("%(fn)s", "/tmp/%(device)s.img"),\n'
             '       write_raw_image("/tmp/%(device)s.img", "%(device)s"),\n'
             '       delete("/tmp/%(device)s.img"));') % args)
      elif partition_type == "EMMC":
        self.script.append(
            'package_extract_file("%(fn)s", "%(device)s");' % args)
      elif partition_type == "UBI":
        self.script.append(
            'package_extract_file("%(fn)s", "%(device)s");' % args)
      elif partition_type == "BML":
	        self.script.append(
            ('assert(package_extract_file("%(fn)s", "/tmp/%(device)s.img"),\n'
             '       write_raw_image("/tmp/%(device)s.img", "%(device)s"),\n'
             '       delete("/tmp/%(device)s.img"));') % args)
      else:
        raise ValueError("don't know how to write \"%s\" partitions" % (p.fs_type,))

    else:
      # backward compatibility with older target-files that lack recovery.fstab
      if self.info["partition_type"] == "MTD":
        partition_type, partition = common.GetTypeAndDevice(mount_point, self.info)
        self.script.append(
            ('assert(package_extract_file("%(fn)s", "/tmp/%(partition)s.img"),\n'
             '       write_raw_image("/tmp/%(partition)s.img", "%(partition)s"),\n'
             '       delete("/tmp/%(partition)s.img"));')
            % {'partition': partition, 'fn': fn})
      elif self.info["partition_type"] == "EMMC":
        partition_type, partition = common.GetTypeAndDevice(mount_point, self.info)
        self.script.append(
            ('package_extract_file("%(fn)s", "%(dir)s%(partition)s");')
            % {'partition': partition, 'fn': fn,
               'dir': self.info.get("partition_path", ""),
               })
      elif self.info["partition_type"] == "UBI":
        partition_type, partition = common.GetTypeAndDevice(mount_point, self.info)
        self.script.append(
            ('package_extract_file("%(fn)s", "%(dir)s%(partition)s");')
            % {'partition': partition, 'fn': fn,
               'dir': self.info.get("partition_path", ""),
               })
      else:
        raise ValueError("don't know how to write \"%s\" partitions" %
                         (self.info["partition_type"],))

  def WriteImageToDevice(self, dev, image):
    """Write the given package file into the given partition."""

    args = {'dev': dev, 'image': image}

    self.script.append(
            ('package_extract_file("write.sh", "/tmp/write.sh");\n'
            'set_perm(0, 0, 0777, "/tmp/write.sh");\n'
            'package_extract_file("%(image)s", "/tmp/%(image)s");\n'
            'run_program("/tmp/write.sh", "%(dev)s", "/tmp/%(image)s");') % args)

