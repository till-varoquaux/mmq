// -*- C++ -*-
/*
 * Copyright 2013 Humor Rainbow inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * \file
 * MMaped fifo queues...
 *
 * @author Till Varoquaux <till@okcupid.com>
 */
// TODO(till): Write doc on error management.
// TODO(till): Write doc on iterator invalidation.
// TODO(till): Add noexcept and noexcept_if tags
// TODO(till): Unable anonymous mode with a trick like std::nothrow
#pragma once

#ifdef MMQ_DBG
#define _MMQ_DBG(MSG) MMQ_DBG(MSG)
#else
#define _MMQ_DBG(MSG)
#endif  // MMQ_DBG

// Locking strategy:
//   The header is locked for writing by the writing process
//   The body is locked for reading by the reading processes
//       When the data is flushed we try to lock the body for writing
//            If that fails we append data only (keep a running count of the
//            offset).

#include "lin_fifo.h"
#include <cstdlib>
extern "C" {
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <error.h>
}
#include <cerrno>

/// \cond DOC_INTERNAL
#ifdef MMQ_NOEXCEPT
#define UFAIL(_err, ...)                                        \
  (__extension__({                                              \
      int __err = _err;                                         \
      error_at_line(0, __err, __FILE__, __LINE__, __VA_ARGS__); \
      __err;                                                    \
    }))

#define UOK 0

#define URETTYPE int

#define URETONERR(_expr) do {                   \
  int __err = _expr;                            \
  if (__err)                                    \
    return __err;                               \
  } while (false)

#else

#include <system_error>

// TODO(till): handle weak vtable
class mmq_error: public std::system_error {
public:
  mmq_error(int ev, const char *etxt) :
    std::system_error(ev, std::system_category(), etxt) {}
};

#define UFAIL(_err, ...)                                \
  (__extension__({                                      \
      char __ebuf[256];                                 \
      snprintf(__ebuf, sizeof(__ebuf), __VA_ARGS__);    \
      throw mmq_error(_err, __ebuf);                    \
    }))

#define UOK

#define URETTYPE void

#define URETONERR(_expr) _expr

#endif  // NOEXCEPT


/**
 *
 */
template<typename _T>
class disksafe {
  typedef _T value_type;
 private:
  unsigned char _version;
  // 7 bits of padding... Put a header in here?
  value_type _vals[2];
 public:
  void set(const value_type &__v) {
    const unsigned char addr = (_version + 1) % 2;
    _vals[addr] = __v;
    // Memory fence to force the compiler to flush everything to memory.
    asm volatile("" : : : "memory");
    _version = addr;
    asm volatile("" : : : "memory");
  }

  const value_type &get() const {
    return _vals[_version];
  }
};


namespace {
  size_t _page_sz() {
    static const size_t page_sz = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    return page_sz;
  }
}

template <typename _T, uint64_t _MAGIC>
class _mmaped_queue_storage {
/*
  Unix error management macro. This is strongly inspired by TEMP_FAILURE_RETRY
 */

#define UCALL(expression)                                               \
  (__extension__({                                                      \
      decltype(expression) __result;                                    \
      do {                                                              \
        __result = expression;                                          \
      } while ((long int) __result == -1L && errno == EINTR);           \
      if ((long int)__result == -1L) {                                  \
        int err = errno;                                                \
        _clear(true);                                                   \
        return UFAIL(err, "unix error");                                \
      }                                                                 \
      __result; }))

public:
  typedef _lin_fifo_header<size_t> header_type;

private:
  struct _underlying {
    uint64_t magic;
    disksafe<header_type> hdr;
    _T vals[0];
  };

  _underlying *_storage = nullptr;

  // Size (in bytes) of the mmap'd region.
  size_t _sz = 0;
  int _fd = -1;

  static size_t _round_up_pg_sz(size_t __sz) {
    const size_t page_size = _page_sz();
    if (__sz % page_size != 0) {
      // Bump up to the next page size.
      __sz += page_size - (__sz % page_size);
    }
    return __sz;
  }

  // In: number of elements, Out size (in bytes) of the region to mmap
  static size_t _to_mmap_size(size_t __sz) {
    return _round_up_pg_sz(sizeof(_underlying) + __sz * sizeof(_T));
  }

  static const size_t __default_size = 1024;

  void _set_init_size(size_t __elt_sz) {
    header_type h;
    h.clear(__elt_sz);
    _storage->magic = _MAGIC;
    sync(h);
  }

  URETTYPE _get_wlock() {
    struct flock fl;
    fl.l_type   = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 4;
    fl.l_pid    = getpid();

    if (fcntl(_fd, F_SETLK, &fl) == -1) {
      int err = errno;
      _clear(true);
      if (err == EAGAIN || err == EACCES) {
        return UFAIL(EACCES, "Write lock already held by pid: %u", fl.l_pid);
      }
      return UFAIL(err, "failed to acquire lock on file");
    }
    return UOK;
  }

  struct flock _excl_lock(short l_type) {
    struct flock fl;
    fl.l_type   = l_type;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 4;
    fl.l_len    = 4;
    fl.l_pid    = getpid();
    return fl;
  }

  URETTYPE _get_rlock() {
    struct flock fl = _excl_lock(F_RDLCK);
    (void) UCALL(fcntl(_fd, F_SETLKW, &fl));
    return UOK;
  }

  URETTYPE _clear(bool noerr = false) {
    int err = 0;
    // mmap or mremap died
    if (_storage == MAP_FAILED) {
      _storage = nullptr;
    } else if (_storage && (munmap(_storage, _sz) == -1)) {
      err = errno;
    }
    if (_fd != -1 && TEMP_FAILURE_RETRY(::close(_fd)) == -1L) {
      err = errno;
    }
    _fd = -1;
    _storage = nullptr;
    if (!noerr && err)
      return UFAIL(err, "unix error");
    return UOK;
  }

public:
  typedef size_t    size_type;
  typedef _T        value_type;
  typedef _T&       reference;
  typedef const _T& const_reference;

  size_t header_size() const {
    return sizeof(_underlying);
  }

  ~_mmaped_queue_storage() { _clear(true); }

  URETTYPE get_excl_lock(pid_t *pid) {
    if (_fd == -1)
      return UOK;
    struct flock fl = _excl_lock(F_WRLCK);
    if (fcntl(_fd, F_SETLK, &fl) == -1) {
      int err = errno;
      if (err == EAGAIN || err == EACCES) {
        _MMQ_DBG("Body lock already held by pid: %u" << fl.l_pid);
        if (pid != nullptr)
          *pid = fl.l_pid;
        return UOK;
      }
      _clear(true);
      return UFAIL(err,
                   "Unix error while acquiring exclusive write lock on file");
    }
    if (pid != nullptr)
      *pid = 0;
    return UOK;
  }

  URETTYPE clear_excl_lock() {
    if (_fd == -1)
      return UOK;
    struct flock fl = _excl_lock(F_UNLCK);
    (void) UCALL(fcntl(_fd, F_SETLKW, &fl));
    return UOK;
  }

  URETTYPE resize(size_t __elt_sz) {
    if (!_storage) return UOK;
    const size_t new_sz = _to_mmap_size(__elt_sz);
    if (_sz == new_sz)
      return UOK;
    _MMQ_DBG("Resizing underlying file from: " << _sz << " to " << new_sz);
    if (_fd != -1) {
      (void) UCALL(ftruncate(_fd, new_sz));
    }
    _storage = reinterpret_cast<_underlying*>
      (UCALL((mremap(_storage, _sz, new_sz, MREMAP_MAYMOVE))));
    (void) UCALL(madvise(_storage, _sz, MADV_SEQUENTIAL));
    _sz = new_sz;
    return UOK;
  }

  reference operator[](const size_t i) {
    return _storage->vals[i];
  }

  const_reference operator[](const size_t i) const {
    return _storage->vals[i];
  }

  const header_type& header() const {
    return _storage->hdr.get();
  }

  bool not_mapped() const {
    return _storage == nullptr;
  }

  URETTYPE sync(const header_type& _v) {
    (void) UCALL(msync(_storage, _sz, MS_SYNC));
    _storage->hdr.set(_v);
    (void) UCALL(msync(_storage, sizeof(_underlying), MS_SYNC));
    return UOK;
  }

  URETTYPE async_flush() {
    (void) UCALL(msync(_storage, _sz, MS_ASYNC));
    return UOK;
  }

  _mmaped_queue_storage() : _storage(nullptr),
                            _sz(_to_mmap_size(__default_size)),
                            _fd(-1)
  {}

  _mmaped_queue_storage(_mmaped_queue_storage &&__rhs) :
    _sz(__rhs.sz), _storage(std::forward(__rhs._storage)) {
    __rhs._storage = nullptr;
  }

  URETTYPE set_annon_map() {
    URETONERR(_clear());
    _sz = _to_mmap_size(__default_size);
    _storage =
      reinterpret_cast<_underlying*>
      (UCALL(mmap(nullptr, _sz, PROT_READ | PROT_WRITE,
                  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)));
    _set_init_size(__default_size);
    return UOK;
  }

  // TODO(till): use a tmp file and move into place once it's initialised.
  URETTYPE set_new_file(const char* fname) {
    _MMQ_DBG("Opening new file: " << fname);
    URETONERR(_clear());
    _fd = UCALL(open(fname, O_RDWR | O_CREAT | O_EXCL, 0666));
    _sz = _to_mmap_size(__default_size);
    (void) UCALL(ftruncate(_fd, _sz));
    URETONERR(_get_wlock());
      _map_fd_storage(PERMISSION::SHARED);
    _set_init_size(__default_size);
    return UOK;
  }

  URETTYPE close() {
    URETONERR(_clear());
  }

  enum class PERMISSION { SHARED, PRIVATE, READONLY };

  URETTYPE _map_fd_storage(PERMISSION perm) {
    assert(_storage == nullptr);
    int prot, flags;
    switch (perm) {
    case PERMISSION::SHARED:
      flags = MAP_SHARED;
      prot = PROT_READ | PROT_WRITE;
      break;
    case PERMISSION::PRIVATE:
      flags = MAP_PRIVATE;
      prot = PROT_READ | PROT_WRITE;
      break;
    case PERMISSION::READONLY:
      flags = MAP_PRIVATE;
      prot = PROT_READ;
      break;
    }
    _storage = reinterpret_cast<_underlying*>
      (UCALL(mmap(nullptr, _sz, prot, flags, _fd, 0)));
    (void) UCALL(madvise(_storage, _sz, MADV_SEQUENTIAL));
    return UOK;
  }

  URETTYPE set_existing_file(const char* fname,
                        PERMISSION perm = PERMISSION::SHARED) {
    _MMQ_DBG("Opening existing file: " << fname);
    URETONERR(_clear());
    int flags;
    switch (perm) {
    case PERMISSION::SHARED:
      flags = O_RDWR;
      break;
    case PERMISSION::PRIVATE:
    case PERMISSION::READONLY:
      flags = O_RDONLY;
      break;
    }
    _fd = UCALL(open(fname, flags, 0666));
    struct stat sbuf;
    (void) UCALL(fstat(_fd, &sbuf));
    _sz = sbuf.st_size - sbuf.st_size % _page_sz();
    if (_sz < sizeof(_underlying)) {
      _clear(true);
      return UFAIL(EINVAL, "File too small to be opened as a valid mmq");
    }
    switch (perm) {
    case PERMISSION::SHARED:
      URETONERR(_get_wlock());
      break;
    case PERMISSION::PRIVATE:
    case PERMISSION::READONLY:
      URETONERR(_get_rlock());
      break;
    }
    URETONERR(_map_fd_storage(perm));
    const size_t max_elt_idx =
      (_sz - sizeof(_underlying)) / sizeof(value_type);
    if (max_elt_idx < _storage->hdr.get().ovf_bufend) {
      _clear(true);
      return UFAIL(EINVAL, "Couldn't map mmq file. It seems truncated");
    }
    if (_storage->magic != _MAGIC) {
      _clear(true);
      return UFAIL(EINVAL, "Bad magic number while opening mmq file");
    }
    return UOK;
  }

  URETTYPE set_file(const char *fname, PERMISSION perm = PERMISSION::SHARED) {
    URETONERR(_clear());
    int fperm = (perm == PERMISSION::SHARED)? R_OK | W_OK: R_OK;
    int acc = access(fname, fperm);
    if (acc == 0) {
      return set_existing_file(fname, perm);
    } else if (acc == -1 && errno == ENOENT && perm == PERMISSION::SHARED) {
      return set_new_file(fname);
    }
    return UFAIL(errno, "access denied or file missing.");
  }
};

/// \endcond DOC_INTERNAL
template<class _T, uint64_t _MAGIC = 2628179991678588509ULL>
class mmaped_queue : private lin_fifo
<_T, _mmaped_queue_storage<_T, _MAGIC>, 200 * 1024 * 1024 / sizeof(_T)> {
private:
  typedef lin_fifo<_T, _mmaped_queue_storage<_T, _MAGIC>,
                   200 * 1024 * 1024 / sizeof(_T)> _parent;

  size_t _pop_offset = 0;

  using _parent::_buf;
  using _parent::_hdr;

public:
  class readonly_view: protected _parent {
  private:
    using _parent::_buf;
    using _parent::_hdr;

  public:
    typedef const typename _parent::value_type          value_type;
    typedef typename _parent::size_type                 size_type;
    typedef typename _parent::const_iterator            const_iterator;
    typedef typename _parent::const_reverse_iterator    const_reverse_iterator;
    typedef typename _parent::const_reference           const_reference;
    typedef const_reference                             reference;
    typedef const_iterator                              iterator;
    typedef const_reverse_iterator                      reverse_iterator;

    static_assert(std::has_trivial_destructor<value_type>::value,
                "The type argument to mmq should be trivially "
                " destructible.");

    bool closed() const {
      return !_buf.not_mapped();
    }

    explicit readonly_view(const char* file): _parent(0) {
      _buf.set_file(file, _parent::container_type::PERMISSION::READONLY);
      if (ok()) {
        _hdr = _buf.header();
      }
    }

    using _parent::empty;
    using _parent::capacity;
    using _parent::size;

    iterator begin()   const { return _parent::begin(); }
    iterator end()     const { return _parent::end(); }
    iterator cbegin()  const { return _parent::begin(); }
    iterator cend()    const { return _parent::end(); }
    reverse_iterator rbegin()  const { return _parent::rbegin(); }
    reverse_iterator rend()    const { return _parent::rend(); }
    reverse_iterator crbegin() const { return _parent::rbegin(); }
    reverse_iterator crend()   const { return _parent::rend(); }

    reference operator[](size_type i) const { return (*this)[i]; }
  };

  typedef const typename _parent::value_type       value_type;
  typedef typename _parent::size_type              size_type;
  typedef typename _parent::const_iterator         const_iterator;
  typedef typename _parent::const_reverse_iterator const_reverse_iterator;
  typedef typename _parent::const_reference        const_reference;
  typedef const_reference                          reference;
  typedef const_iterator                           iterator;
  typedef const_reverse_iterator                   reverse_iterator;

  // TODO: move into a cpp file (and stop being a header only library.)
  struct map_anonymous_t {};
  static const map_anonymous_t map_anonymous;

  /// \name Modifiers
  /// @{

  void pop() noexcept {
    _pop_offset++;
  }

  /// insert element at the end.
  void push(const value_type &v) {
    _parent::push(v);
  }

  /// insert element at the end.
  void push(value_type &&v) {
    _parent::push(std::forward<value_type>(v));
  }

  /// construct an element at the end.
  template <class... _Args>
  void emplace(_Args&&... args) {
    _parent::emplace(std::forward<_Args>(args)...);
  }

  /// @}

  bool ok() const noexcept {
    return !_buf.not_mapped();
  }

  void swap(mmaped_queue &q) noexcept {
    std::swap(_hdr, q._hdr);
    std::swap(_buf, q._buf);
  }

  mmaped_queue(map_anonymous_t) : _parent(0) {
    _buf.set_annon_map();
    _parent::_hdr = _buf.header();
  }

  mmaped_queue(const char* file) : _parent(0) {
    _buf.set_file(file);
    if (ok()) {
      _parent::_hdr = _buf.header();
    }
  }

  /// \name Accessors
  /// @{

  /// access the first element.
  reference front() const noexcept {
    return _parent::operator[](_pop_offset);
  }

  /// access the last element.
  reference back() const noexcept {
    return _parent::back();
  }

  /// access element by index.
  reference operator[](size_type __i) const noexcept {
    return _parent::operator[](_pop_offset + __i);
  }
  /// @}

  /// \name Iterators
  /// @{

  /// return an iterator to the first element.
  iterator begin() const noexcept {
    return iterator(this, _parent::_to_abs_pos(_pop_offset));
  }

  /// return an iterator past the last element.
  iterator end() const noexcept {
    return _parent::end();
  }

  iterator cbegin() const noexcept { return _parent::begin(); }
  iterator cend() const noexcept { return _parent::end(); }
  reverse_iterator rbegin()  const noexcept  { return _parent::rbegin(); }
  reverse_iterator rend()    const noexcept { return _parent::rend(); }
  reverse_iterator crbegin() const noexcept { return _parent::rbegin(); }
  reverse_iterator crend()   const noexcept { return _parent::rend(); }
  /// @}

  size_type size() const noexcept {
    return _parent::size() - _pop_offset;
  }


  /// Test whether we have any elements.
  /**
   * \return `true` if there are no elements in the queue.
   */
  bool empty() const noexcept {
    return size() == 0;
  }

  using _parent::reserve;

  /// Reserve storage.
  /**
   * Increase the size of the underlying storage.
   */
  void reserve_bytes(size_t sz ///< [in] Number of bytes to grow the underlying
                               /// storage to.
                     ) {
    const size_type elt_sz = (sz - _buf.header_size()) /
      sizeof(value_type);
    reserve(elt_sz);
  }

  /// Flush the data to the disk.
  /**
   * \returns a unix error number in case of failure. If the file is locked
   * UOK is returned and `pid` is set.
   */
  URETTYPE sync(pid_t *pid = nullptr, ///< [out] If the file is locked the `pid`
                /// of the locking process.
           bool ignore_lock = false ///< [in] Flush the file even if another
                                    /// process already has a read lock on that
                                    /// file.
           ) {
    pid_t my_pid = 0;
    if (!pid)
      pid = &my_pid;
    if (!ignore_lock) {
      URETONERR(_buf.get_excl_lock(pid));
      if (pid)
        return UOK;
    }
    _parent::popn(_pop_offset);
    URETONERR(_buf.sync(_hdr));
    _pop_offset = 0;
    if (!ignore_lock) {
      URETONERR(_buf.clear_excl_lock());
    }
    return UOK;
  }

  /**
   * \brief flush the file's content to the disk asynchronously.
   *
   * Tell the os to start flushing the dirty pages asynchronously to the disk.
   */
  void async_flush() {
    _buf.async_flush();
  }

  ~mmaped_queue() { }

  void close() { _buf.clear(); }
};

#undef UFAIL
#undef UOK
#undef UCALL
#undef _MMQ_DBG
