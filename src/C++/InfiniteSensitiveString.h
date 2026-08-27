/****************************************************************************
** Copyright (c) 2001-2014
**
** This file is part of the QuickFIX FIX Engine
**
** This file may be distributed under the terms of the quickfixengine.org
** license as defined by quickfixengine.org and appearing in the file
** LICENSE included in the packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://www.quickfixengine.org/LICENSE for licensing information.
**
** Contact ask@quickfixengine.org if any conditions of this licensing are
** not clear to you.
**
****************************************************************************/

#pragma once

#include <memory>
#include <string>
#include <utility>

namespace FIX {
class InfiniteSensitiveString {
public:
  InfiniteSensitiveString() noexcept = default;

  InfiniteSensitiveString(const char *source) {
    if (source && *source) {
      m_value = std::make_unique<std::string>(source);
    }
  }

  InfiniteSensitiveString(const std::string &source) {
    if (!source.empty()) {
      m_value = std::make_unique<std::string>(source);
    }
  }

  InfiniteSensitiveString(std::string &&source) {
    try {
      if (!source.empty()) {
        m_value = std::make_unique<std::string>(source);
      }
    } catch (...) {
      erase(source);
      throw;
    }
    erase(source);
  }

  InfiniteSensitiveString(const InfiniteSensitiveString &other) {
    if (!other.empty()) {
      m_value = std::make_unique<std::string>(other.value());
    }
  }

  InfiniteSensitiveString(InfiniteSensitiveString &&) noexcept = default;

  InfiniteSensitiveString &operator=(const InfiniteSensitiveString &other) {
    if (this != &other) {
      InfiniteSensitiveString replacement(other);
      m_value.swap(replacement.m_value);
    }
    return *this;
  }

  InfiniteSensitiveString &operator=(InfiniteSensitiveString &&other) noexcept {
    if (this != &other) {
      wipe();
      m_value = std::move(other.m_value);
    }
    return *this;
  }

  InfiniteSensitiveString &operator=(const std::string &source) {
    InfiniteSensitiveString replacement(source);
    m_value.swap(replacement.m_value);
    return *this;
  }

  InfiniteSensitiveString &operator=(std::string &&source) {
    InfiniteSensitiveString replacement(std::move(source));
    m_value.swap(replacement.m_value);
    return *this;
  }

  InfiniteSensitiveString &operator=(const char *source) {
    InfiniteSensitiveString replacement(source);
    m_value.swap(replacement.m_value);
    return *this;
  }

  ~InfiniteSensitiveString() { wipe(); }

  operator const std::string &() const noexcept { return value(); }

  const char *data() const noexcept { return value().data(); }
  char *data() { return value().data(); }
  std::size_t size() const noexcept { return value().size(); }
  bool empty() const noexcept { return value().empty(); }
  std::string::size_type find(const std::string &needle, std::string::size_type position = 0) const {
    return value().find(needle, position);
  }
  std::string::iterator begin() { return value().begin(); }
  std::string::iterator end() { return value().end(); }
  std::string::const_iterator begin() const noexcept { return value().begin(); }
  std::string::const_iterator end() const noexcept { return value().end(); }
  void clear() noexcept {
    if (m_value) {
      erase(*m_value);
    }
  }
  friend bool operator==(const InfiniteSensitiveString &left, const InfiniteSensitiveString &right) {
    return left.value() == right.value();
  }
  friend bool operator==(const InfiniteSensitiveString &left, const std::string &right) {
    return left.value() == right;
  }
  friend bool operator==(const std::string &left, const InfiniteSensitiveString &right) {
    return left == right.value();
  }
  friend bool operator==(const InfiniteSensitiveString &left, const char *right) { return left.value() == right; }
  friend bool operator==(const char *left, const InfiniteSensitiveString &right) { return left == right.value(); }
  friend bool operator!=(const InfiniteSensitiveString &left, const InfiniteSensitiveString &right) {
    return !(left == right);
  }
  friend bool operator!=(const InfiniteSensitiveString &left, const std::string &right) { return !(left == right); }
  friend bool operator!=(const std::string &left, const InfiniteSensitiveString &right) { return !(left == right); }
  friend bool operator!=(const InfiniteSensitiveString &left, const char *right) { return !(left == right); }
  friend bool operator!=(const char *left, const InfiniteSensitiveString &right) { return !(left == right); }

private:
  static void erase(std::string &value) noexcept {
    volatile char *cursor = value.empty() ? nullptr : &value[0];
    for (std::size_t index = 0; index < value.size(); ++index) {
      cursor[index] = 0;
    }
    value.clear();
  }

  std::string &value() {
    if (!m_value) {
      m_value = std::make_unique<std::string>();
    }
    return *m_value;
  }

  const std::string &value() const noexcept {
    static const std::string empty;
    return m_value ? *m_value : empty;
  }

  void wipe() noexcept {
    if (m_value) {
      erase(*m_value);
    }
  }

  std::unique_ptr<std::string> m_value;
};
} // namespace FIX
