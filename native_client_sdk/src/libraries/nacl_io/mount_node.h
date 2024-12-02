/* Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef LIBRARIES_NACL_IO_MOUNT_NODE_H_
#define LIBRARIES_NACL_IO_MOUNT_NODE_H_

#include <string>

#include "nacl_io/error.h"
#include "nacl_io/osstat.h"
#include "sdk_util/ref_object.h"

struct dirent;
struct stat;
class Mount;

// NOTE: The KernelProxy is the only class that should be setting errno. All
// other classes should return Error (as defined by nacl_io/error.h).
class MountNode : public RefObject {
 protected:
  explicit MountNode(Mount* mount);
  virtual ~MountNode();

 protected:
  // Initialize with node specific flags, in this case stat permissions.
  virtual Error Init(int flags);
  virtual void Destroy();

 public:
  // Normal OS operations on a node (file), can be called by the kernel
  // directly so it must lock and unlock appropriately.  These functions
  // must not be called by the mount.
  virtual Error FSync();
  // It is expected that the derived MountNode will fill with 0 when growing
  // the file.
  virtual Error FTruncate(off_t length);
  // Assume that |out_bytes| is non-NULL.
  virtual Error GetDents(size_t offs,
                         struct dirent* pdir,
                         size_t count,
                         int* out_bytes);
  // Assume that |stat| is non-NULL.
  virtual Error GetStat(struct stat* stat);
  // Assume that |arg| is non-NULL.
  virtual Error Ioctl(int request, char* arg);
  // Assume that |buf| and |out_bytes| are non-NULL.
  virtual Error Read(size_t offs, void* buf, size_t count, int* out_bytes);
  // Assume that |buf| and |out_bytes| are non-NULL.
  virtual Error Write(size_t offs,
                      const void* buf,
                      size_t count,
                      int* out_bytes);
  // Assume that |addr| and |out_addr| are non-NULL.
  virtual Error MMap(void* addr,
                     size_t length,
                     int prot,
                     int flags,
                     size_t offset,
                     void** out_addr);

  virtual int GetLinks();
  virtual int GetMode();
  virtual int GetType();
  // Assume that |out_size| is non-NULL.
  virtual Error GetSize(size_t *out_size);
  virtual bool IsaDir();
  virtual bool IsaFile();
  virtual bool IsaTTY();

 protected:
  // Directory operations on the node are done by the Mount. The mount's lock
  // must be held while these calls are made.

  // Adds or removes a directory entry updating the link numbers and refcount
  // Assumes that |node| is non-NULL.
  virtual Error AddChild(const std::string& name, MountNode* node);
  virtual Error RemoveChild(const std::string& name);

  // Find a child and return it without updating the refcount
  // Assumes that |out_node| is non-NULL.
  virtual Error FindChild(const std::string& name, MountNode** out_node);
  virtual int ChildCount();

  // Update the link count
  virtual void Link();
  virtual void Unlink();

 protected:
  struct stat stat_;
  Mount* mount_;

  friend class Mount;
  friend class MountDev;
  friend class MountHtml5Fs;
  friend class MountHttp;
  friend class MountMem;
  friend class MountNodeDir;
};

#endif  // LIBRARIES_NACL_IO_MOUNT_NODE_H_
