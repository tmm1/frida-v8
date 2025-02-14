// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_CODE_MEMORY_ACCESS_H_
#define V8_COMMON_CODE_MEMORY_ACCESS_H_

#include "src/base/build_config.h"
#include "src/base/lazy-instance.h"
#include "src/base/macros.h"
#include "src/base/platform/platform.h"

namespace v8 {
namespace internal {

class CodePageCollectionMemoryModificationScope;
class CodePageMemoryModificationScope;
class CodeSpaceMemoryModificationScope;
class RwxMemoryWriteScopeForTesting;
namespace wasm {
class CodeSpaceWriteScope;
}

// This scope is a wrapper for APRR/MAP_JIT machinery on MacOS on ARM64
// ("Apple M1"/Apple Silicon) or Intel PKU (aka. memory protection keys)
// with respective low-level semantics.
//
// The semantics on MacOS on ARM64 is the following:
// The scope switches permissions between writable and executable for all the
// pages allocated with RWX permissions. Only current thread is affected.
// This achieves "real" W^X and it's fast (see pthread_jit_write_protect_np()
// for details).
// By default it is assumed that the state is executable.
// It's also assumed that the process has the "com.apple.security.cs.allow-jit"
// entitlement.
//
// The semantics on Intel with PKU support is the following:
// When Intel PKU is available, the scope switches the protection key's
// permission between writable and not writable. The executable permission
// cannot be retracted with PKU. That is, this "only" achieves write
// protection, but is similarly thread-local and fast.
//
// On other platforms the scope is a no-op and thus it's allowed to be used.
//
// The scope is reentrant and thread safe.
class V8_NODISCARD RwxMemoryWriteScope {
 public:
  // The comment argument is used only for ensuring that explanation about why
  // the scope is needed is given at particular use case.
  V8_INLINE explicit RwxMemoryWriteScope(const char* comment);
  V8_INLINE ~RwxMemoryWriteScope();

  // Disable copy constructor and copy-assignment operator, since this manages
  // a resource and implicit copying of the scope can yield surprising errors.
  RwxMemoryWriteScope(const RwxMemoryWriteScope&) = delete;
  RwxMemoryWriteScope& operator=(const RwxMemoryWriteScope&) = delete;

  // Returns true if current configuration supports fast write-protection of
  // executable pages.
  V8_INLINE static bool IsSupported();

#if V8_HAS_PKU_JIT_WRITE_PROTECT
  static int memory_protection_key() { return memory_protection_key_; }

  static void InitializeMemoryProtectionKey();

  static bool IsPKUWritable();
#endif  // V8_HAS_PKU_JIT_WRITE_PROTECT

 private:
  friend class CodePageCollectionMemoryModificationScope;
  friend class CodePageMemoryModificationScope;
  friend class CodeSpaceMemoryModificationScope;
  friend class RwxMemoryWriteScopeForTesting;
  friend class wasm::CodeSpaceWriteScope;

  // {SetWritable} and {SetExecutable} implicitly enters/exits the scope.
  // These methods are exposed only for the purpose of implementing other
  // scope classes that affect executable pages permissions.
  V8_INLINE static void SetWritable();
  V8_INLINE static void SetExecutable();

#if V8_HAS_PTHREAD_JIT_WRITE_PROTECT || V8_HAS_PKU_JIT_WRITE_PROTECT
  // This counter is used for supporting scope reentrance.
  static base::LazyInstance<base::ThreadLocalInt>::type
      code_space_write_nesting_level_;
#endif  // V8_HAS_PTHREAD_JIT_WRITE_PROTECT || V8_HAS_PKU_JIT_WRITE_PROTECT

#if V8_HAS_PKU_JIT_WRITE_PROTECT
  static int memory_protection_key_;
#if DEBUG
  static bool pkey_initialized;
#endif
#endif  // V8_HAS_PKU_JIT_WRITE_PROTECT
};

// This class is a no-op version of the RwxMemoryWriteScope class above.
// It's used as a target type for other scope type definitions when a no-op
// semantics is required.
class V8_NODISCARD NopRwxMemoryWriteScope final {
 public:
  V8_INLINE explicit NopRwxMemoryWriteScope(const char* comment) {
    // Define a constructor to avoid unused variable warnings.
  }
};

// Sometimes we need to call a function which will / might spawn a new thread,
// like {JobHandle::NotifyConcurrencyIncrease}, while holding a
// {RwxMemoryWriteScope}. This is problematic since the new thread will inherit
// the parent thread's PKU permissions.
// The {ResetPKUPermissionsForThreadSpawning} scope will thus reset the PKU
// permissions as long as it is in scope, such that it is safe to spawn new
// threads.
class V8_NODISCARD ResetPKUPermissionsForThreadSpawning {
 public:
#if V8_HAS_PKU_JIT_WRITE_PROTECT
  ResetPKUPermissionsForThreadSpawning();
  ~ResetPKUPermissionsForThreadSpawning();

 private:
  bool was_writable_;
#else
  // Define an empty constructor to avoid "unused variable" warnings.
  ResetPKUPermissionsForThreadSpawning() {}
#endif
};

// Same as the RwxMemoryWriteScope but without inlining the code.
// This is a workaround for component build issue (crbug/1316800), when
// a thread_local value can't be properly exported.
class V8_NODISCARD RwxMemoryWriteScopeForTesting final
    : public RwxMemoryWriteScope {
 public:
  V8_EXPORT_PRIVATE RwxMemoryWriteScopeForTesting();
  V8_EXPORT_PRIVATE ~RwxMemoryWriteScopeForTesting();

  // Disable copy constructor and copy-assignment operator, since this manages
  // a resource and implicit copying of the scope can yield surprising errors.
  RwxMemoryWriteScopeForTesting(const RwxMemoryWriteScopeForTesting&) = delete;
  RwxMemoryWriteScopeForTesting& operator=(
      const RwxMemoryWriteScopeForTesting&) = delete;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_CODE_MEMORY_ACCESS_H_
