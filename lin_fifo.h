// -*- C++ -*-
/*
 * Copyright 2013 Humor rainbow Inc.
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

/*
 * @author Till Varoquaux <till@okcupid.com>
 */

#pragma once
#include <vector>
#include <cstdint>
#include <cassert>
#include <type_traits>
#include <utility>
#include <new>

#ifdef _LFO_DBG
#  define _LFO_DBG(MSG) _LFO_DBG(MSG)
#elif defined _MMQ_DBG
#  define _LFO_DBG(MSG) _MMQ_DBG(MSG)
#else
#  define _LFO_DBG(MSG)
#endif  // _LFO_DBG


/// \cond DOC_INTERNAL
template<typename _Size>
struct _lin_fifo_header {
  typedef _Size size_type;
  size_type circ_bufend;
  size_type circ_len;
  size_type circ_start;
  size_type ovf_idx;
  size_type ovf_bufend;

  void clear(_Size __sz = 0) {
    circ_bufend = __sz;
    ovf_bufend = __sz;
    ovf_idx = __sz;
    circ_len = 0;
    circ_start = 0;
  }
};
/// \endcond DOC_INTERNAL

/**
 * \brief FIFO queues backed by a resizeable random access container.
 *
 * This implements a container adaptor for FIFO queue backed by a container
 * that has random access but can only grow in one direction. The adaptor does
 * not move elements during resizing (however the underlying container might).
 *
 */
template<typename _T,
         typename _Container = std::vector<_T>,
         typename _Container::size_type _BLOCK_SIZE = 1024>
class lin_fifo {
public:
  typedef _T         value_type;
  typedef _Container container_type;
  typedef typename container_type::size_type
  size_type;
  typedef typename container_type::reference
  reference;
  typedef typename container_type::const_reference
  const_reference;

protected:
  typedef _lin_fifo_header<size_type> header_type;

  container_type _buf;
  header_type _hdr;
  bool _autoshrink = false;
  const header_type& header() { return _hdr; }

  lin_fifo(container_type &&__buf, header_type __hdr):
    _buf(std::forward(__buf)), _hdr(__hdr) {}

protected:
  void _circ_pop() {
    assert(_hdr.circ_len > 0);

    if (std::has_trivial_destructor<value_type>::value)
      _buf[_hdr.circ_start].~value_type();

    _hdr.circ_start = (_hdr.circ_start+1) % _hdr.circ_bufend;
    _hdr.circ_len--;
  }

  reference _circ_tail() {
    return _buf[_hdr.circ_start];
  }

  const_reference _circ_tail() const {
    return _buf[_hdr.circ_start];
  }

  size_type _circ_push_pos() {
    assert(_hdr.circ_len < _hdr.circ_bufend);
    return (_hdr.circ_start + _hdr.circ_len) % _hdr.circ_bufend;
  }

  void _lin_grow() {
    const size_type new_sz = _hdr.ovf_bufend + _BLOCK_SIZE;
    _LFO_DBG("Growing the underlying buffer to:" << new_sz << " elements.");
    _buf.resize(new_sz);
    _hdr.ovf_bufend = new_sz;
  }

  void _shrink_to_fit() {
    if (_hdr.ovf_bufend == _hdr.circ_bufend &&
        _hdr.circ_start + _hdr.circ_len < _hdr.circ_bufend &&
        _autoshrink) {
      // We leave a bit of headroom...
      size_type new_sz = _hdr.circ_start + _hdr.circ_len + _hdr.circ_len / 4;
      if (new_sz < _hdr.ovf_bufend) {
        _LFO_DBG("Shrinking the underlying buffer to:" << new_sz
                 << " elements.");
        _buf.resize(new_sz);
        _hdr.circ_bufend = new_sz;
        _hdr.ovf_idx = new_sz;
        _hdr.ovf_bufend = new_sz;
      }
    }
  }

  void _circularize() {
    _LFO_DBG("Swallowing the overflow back in the circular buffer.");
    _hdr.circ_len = _hdr.ovf_idx - _hdr.circ_bufend;
    _hdr.circ_start = _hdr.circ_bufend;
    _hdr.circ_bufend = _hdr.ovf_bufend;
    _hdr.ovf_idx = _hdr.ovf_bufend;
  }

  size_type _ovf_push_pos() {
    if (_hdr.ovf_idx == _hdr.ovf_bufend) {
      _lin_grow();
    }
    return _hdr.ovf_idx;
  }

  size_type _get_push_pos() {
    if (_hdr.circ_bufend == _hdr.ovf_bufend &&
        _hdr.circ_len < _hdr.circ_bufend) {
      return _circ_push_pos();
    }
    return _ovf_push_pos();
  }

  size_type _commit_push_pos() {
    if (_hdr.circ_bufend == _hdr.ovf_bufend &&
        _hdr.circ_len < _hdr.circ_bufend) {
      return _hdr.circ_len++;
    }
    return _hdr.ovf_idx++;
  }

  // Returns one pos beyond the last index. The result might not point to
  // anything valid. This function is only useful for iterators.
  // TODO(till): move in the it as a static func
  size_type _end_pos() const {
    if (_hdr.circ_bufend < _hdr.ovf_bufend
        || _hdr.circ_len + 1 >= _hdr.circ_bufend) {
      return _hdr.ovf_idx;
    } else {
      return _hdr.circ_bufend;
    }
  }

  size_type _to_abs_pos(const size_type __pos) const {
    if (__pos >= _hdr.circ_len) {
      return _hdr.circ_bufend - _hdr.circ_len + __pos;
    }
    return (_hdr.circ_start + __pos) % _hdr.circ_bufend;
  }

  // Needs testing...
  size_type _to_rel_pos(const size_type __pos) const {
    if (__pos >= _hdr.circ_bufend) {
      return __pos - _hdr.circ_bufend + _hdr.circ_len;
    } else if (__pos >= _hdr.circ_start) {
      return __pos - _hdr.circ_start;
    } else {
      return __pos + _hdr.circ_bufend - _hdr.circ_len;
    }
  }

  // The iterators are invalidated by pop and push
  // only if the element they where pointing to is no more in the range
  // [begin, end[
  template<typename _Elt, typename _Base>
  class _iterator {
    _Base *_parent;
    size_type _pos;
  public:
    typedef _Elt  value_type;
    typedef _Elt& reference;
    typedef _Elt* pointer;
    typedef std::bidirectional_iterator_tag iterator_category;
    typedef typename std::make_signed<size_type>::type difference_type;
    _iterator(): _parent(nullptr), _pos(0) {}

    _iterator(_Base *__par, size_type __p):
      _parent(__par),
      _pos(__p)
    {}

    size_type _circ_last() const {
      return
        (_parent->_hdr.circ_start + _parent->_hdr.circ_len - 1) %
        _parent->_hdr.circ_bufend;
    }

    _iterator(const _iterator& __it):
       _parent(__it._parent),
       _pos(__it._pos)
    {}

    // Needs testing (especially with the {r,}{begin,end} cases)
    _iterator& operator++() {
      if (_pos == _circ_last()) {
        _pos = _parent->_hdr.circ_bufend;
      } else if (_pos + 1 == _parent->_hdr.circ_bufend) {
        _pos = 0;
      } else {
        _pos++;
      }
      return *this;
    }

    _iterator& operator--() {
      if (_pos == _parent->_hdr.circ_bufend) {
        _pos = _circ_last();
      } else if (_pos == 0) {
        _pos = _parent->_hdr.circ_bufend - 1;
      } else {
        _pos--;
      }
      return *this;
    }

    _iterator operator++(int) {
      _iterator i = *this;
      ++(*this);
      return i;
    }

    _iterator operator--(int) {
      _iterator i = *this;
      --(*this);
      return i;
    }

    reference operator*() { return _parent->_buf[_pos]; }

    pointer operator->() { return &(_parent->_buf[_pos]); }

    friend bool operator==(const _iterator& __x, const _iterator& __y) {
      return
        __x._parent == __y._parent &&
        __x._pos == __y._pos;
    }

    friend bool operator!=(const _iterator& __x, const _iterator& __y) {
      return !(__x == __y);
    }

    _iterator& operator=(const _iterator& __rhs) {
      _parent = __rhs._parent;
      _pos = __rhs._pos;
      return *this;
    }
  };

public:
  typedef _iterator<value_type, lin_fifo>             iterator;
  typedef _iterator<const value_type, const lin_fifo> const_iterator;
  typedef std::reverse_iterator<iterator>             reverse_iterator;
  typedef std::reverse_iterator<const_iterator>       const_reverse_iterator;

  /**
   * Returns an iterator to the beginning.
   */
  iterator begin() noexcept {
    return iterator(this, _hdr.circ_start);
  }

  /**
   * Returns an iterator to the beginning.
   */
  iterator end() noexcept {
    return iterator(this, _end_pos());
  }

  /**
   * Returns a const iterator to the beginning.
   */
  const_iterator begin() const noexcept {
    return const_iterator(this, _hdr.circ_start);
  }

  /**
   * Returns a const iterator to the end.
   */
  const_iterator end() const noexcept {
    return const_iterator(this, _end_pos());
  }

  /**
   * Returns a const iterator to the beginning.
   */
  const_iterator cbegin() const noexcept {
    return begin();
  }

  /**
   * Returns a const iterator to the end.
   */
  const_iterator cend() const noexcept {
    return end();
  }


  reverse_iterator rbegin() noexcept {
    return reverse_iterator(end());
  }

  const_reverse_iterator rbegin() const noexcept  {
    return const_reverse_iterator(end());
  }


  reverse_iterator rend() noexcept {
    return reverse_iterator(begin());
  }


  const_reverse_iterator rend() const noexcept  {
    return const_reverse_iterator(begin());
  }

  const_reverse_iterator crbegin() const noexcept {
    return const_reverse_iterator(end());
  }

  const_reverse_iterator crend() const noexcept {
    return const_reverse_iterator(begin());
  }

  /**
   * Return the number of elements in the container.
   */
  size_type size() const noexcept {
    return _hdr.circ_len + _hdr.ovf_idx - _hdr.circ_bufend;
  }

  /**
   * Test whether the container is empty.
   *
   * Returns `true` if the fifo is empty (i.e. size is 0).
   */
  bool empty() const noexcept {
    return (_hdr.circ_len > 0) || (_hdr.ovf_idx < _hdr.circ_bufend);
  }

  /**
   * Access specified element.
   */
  reference operator[] (const size_type __i) {
    return _buf[_to_abs_pos(__i)];
  }

  /**
   * Access specified element.
   */
  const_reference operator[] (const size_type __i) const {
    if (__i >= _hdr.circ_len) {
      return _buf[_hdr.circ_bufend - _hdr.circ_len + __i];
    }
    return _buf[(_hdr.circ_start + __i) % (_hdr.circ_bufend)];
  }

  /**
   * Insert element at the end.
   */
  void push(const value_type &__v) {
    new(&_buf[_get_push_pos()]) value_type(__v);
    _commit_push_pos();
  }

  /**
   * Insert element at the end.
   */
  void push(value_type &&__v) {
    new(&_buf[_get_push_pos()]) value_type(std::forward<value_type>(__v));
    _commit_push_pos();
  }

  /**
   * Construct an element at the end.
   */
  template <class... _Args>
  void emplace(_Args&&... __args) {
    new(&_buf[_get_push_pos()]) value_type(std::forward<_Args>(__args)...);
    _commit_push_pos();
  }

  /**
   * Removes the first element.
   */
  inline void pop() noexcept {
    assert(_hdr.circ_len != 0);
    _hdr.circ_len--;
    if (_hdr.circ_len == 0 && _hdr.circ_bufend != _hdr.ovf_bufend) {
      _circularize();
    } else {
      _hdr.circ_start++;
      if (_hdr.circ_start == _hdr.circ_bufend) {
        _hdr.circ_start = 0;
        _LFO_DBG("The circular buffer has just complete a full loop!");
        _shrink_to_fit();
      }
    }
  }

  /**
   *
   */
  void popn(size_type __n) {
    assert(__n <= size());
    if (!std::has_trivial_destructor<value_type>::value) {
      for (size_type i = 0; i < __n; ++i) {
        pop();
      }
      return;
    }
    if (__n >= _hdr.circ_len) {
      if (_hdr.circ_len > 0) {
        __n -= _hdr.circ_len;
        _hdr.circ_len = 0;
        if (_hdr.circ_bufend != _hdr.ovf_bufend) {
          _circularize();
        }
      }
    }
    _hdr.circ_len -= __n;
    _hdr.circ_start = (_hdr.circ_start+__n);
    if (_hdr.circ_start >= _hdr.circ_bufend) {
      _LFO_DBG("The circular buffer has just complete a full loop!");
      _hdr.circ_start -= _hdr.circ_bufend;
      _shrink_to_fit();
    }
    // while (__n > 0) {
    //   pop();
    //   __n--;
    // }
  }

  void reserve(size_type _sz) {
    if (_hdr.ovf_bufend < _sz) {
      _LFO_DBG("Reserving " << _sz << " slots.");
      _buf.resize(_sz);
      _hdr.ovf_bufend = _sz;
    }
  }

  size_type capacity() {
    return _hdr.ovf_bufend;
  }

  void turn_on_autoshrink() {
    _autoshrink = true;
  }


  /**
   * Access the first element.
   */
  reference front() {
    return _circ_tail();
  }

  /**
   * Access the last element.
   */
  const_reference front() const {
    return _circ_tail();
  }

  ~lin_fifo() {
    if (!std::has_trivial_destructor<value_type>::value) {
      popn(size());
    }
  }

  explicit lin_fifo(size_type __sz = 64) {
    if (__sz > 0)
      _buf.resize(__sz);
    _hdr.clear(__sz);
  }
};

#undef _LFO_DBG
