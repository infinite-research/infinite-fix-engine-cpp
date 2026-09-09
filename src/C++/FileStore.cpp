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

#ifdef _MSC_VER
#include "stdafx.h"
#else
#include "config.h"
#endif

#include "FileStore.h"
#include "Parser.h"
#include "SessionID.h"
#include "Utility.h"
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <inttypes.h>
#include <memory>
#include <sys/stat.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#ifdef _MSC_VER
#define FILE_SEEK _fseeki64
#define FILE_TELL _ftelli64
#else
#define FILE_SEEK fseeko
#define FILE_TELL ftello
#endif

namespace {
auto const seqNumFileFormat = "%" + std::to_string(std::numeric_limits<uint64_t>::digits10 + 1) + "."
                              + std::to_string(std::numeric_limits<uint64_t>::digits10 + 1) + SCNu64;

auto const seqNumPairFileFormat = (seqNumFileFormat + " : " + seqNumFileFormat);

} // namespace
namespace FIX {
FileStore::FileStore(const UtcTimeStamp &now, std::string path, const SessionID &sessionID)
    : m_cache(now),
      m_msgFile(0),
      m_headerFile(0),
      m_seqNumsFile(0),
      m_sessionFile(0) {
  file_mkdir(path.c_str());

  if (path.empty()) {
    path = ".";
  }
  const std::string &begin = sessionID.getBeginString().getString();
  const std::string &sender = sessionID.getSenderCompID().getString();
  const std::string &target = sessionID.getTargetCompID().getString();
  const std::string &qualifier = sessionID.getSessionQualifier();

  std::string sessionid = begin + "-" + sender + "-" + target;
  if (qualifier.size()) {
    sessionid += "-" + qualifier;
  }

  std::string prefix = file_appendpath(path, sessionid + ".");

  m_msgFileName = prefix + "body";
  m_headerFileName = prefix + "header";
  m_seqNumsFileName = prefix + "seqnums";
  m_sessionFileName = prefix + "session";

  try {
    open(false);
  } catch (IOException &e) {
    throw ConfigError(e.what());
  }
}

FileStore::~FileStore() {
  if (m_msgFile) {
    fclose(m_msgFile);
  }
  if (m_headerFile) {
    fclose(m_headerFile);
  }
  if (m_seqNumsFile) {
    fclose(m_seqNumsFile);
  }
  if (m_sessionFile) {
    fclose(m_sessionFile);
  }
}

void FileStore::open(bool deleteFile) {
  const std::array<FILE **, 4> handles{{&m_msgFile, &m_headerFile, &m_seqNumsFile, &m_sessionFile}};
  const std::array<std::string, 4> names{{m_msgFileName, m_headerFileName, m_seqNumsFileName, m_sessionFileName}};
  if (deleteFile) {
    for (std::size_t i = 0; i < handles.size(); ++i) {
      if (*handles[i]) {
        fclose(*handles[i]);
        *handles[i] = nullptr;
      }
      file_unlink(names[i].c_str());
    }
  }

  using File = std::unique_ptr<FILE, int (*)(FILE *)>;
  std::array<File, 4> files{
      {File(nullptr, fclose), File(nullptr, fclose), File(nullptr, fclose), File(nullptr, fclose)}};
  std::array<FILE *, 4> previous{};
  for (std::size_t i = 0; i < files.size(); ++i) {
    previous[i] = *handles[i];
#ifdef _MSC_VER
    // Preserve the existing deny-write share mode during refresh.
    if (previous[i]) {
      const int descriptor = _dup(_fileno(previous[i]));
      if (descriptor == -1) {
        throw IOException("Could not duplicate file: " + names[i]);
      }
      if (_setmode(descriptor, _O_BINARY) == -1) {
        _close(descriptor);
        throw IOException("Could not set binary mode for duplicate file: " + names[i]);
      }
      files[i].reset(_fdopen(descriptor, "r+b"));
      if (!files[i]) {
        _close(descriptor);
        throw IOException("Could not open duplicate file: " + names[i]);
      }
      if (FILE_SEEK(files[i].get(), 0, SEEK_SET)) {
        throw IOException("Could not rewind file: " + names[i]);
      }
    } else
#endif
    {
      files[i].reset(file_fopen(names[i].c_str(), "r+b"));
      if (!files[i] && errno != ENOENT) {
        throw IOException("Could not open file: " + names[i] + " " + error_strerror());
      }
    }
  }
  const bool newStore = !files[1] && !files[2] && !files[3] && !previous[0];
  if (!newStore && (!files[0] || !files[1] || !files[2] || !files[3])) {
    throw IOException("Incomplete file store artifacts");
  }
#ifdef _WIN32
  // Opening in text mode can truncate a trailing CTRL+Z. Translate only after opening the body safely.
  if (files[0] && _setmode(_fileno(files[0].get()), _O_TEXT) == -1) {
    throw IOException("Could not set message body text mode");
  }
#endif
  for (std::size_t i = 0; i < files.size(); ++i) {
    *handles[i] = files[i].get();
  }
  const bool newSeqNums = !m_seqNumsFile;
  const bool newSession = !m_sessionFile;
  try {
    populateCache();
    for (std::size_t i = 0; i < files.size(); ++i) {
      if (!files[i]) {
        files[i].reset(file_fopen(names[i].c_str(), "w+bx"));
        if (!files[i]) {
          throw IOException("Could not create file: " + names[i] + " " + error_strerror());
        }
#ifdef _WIN32
        if (i == 0 && _setmode(_fileno(files[i].get()), _O_TEXT) == -1) {
          throw IOException("Could not set message body text mode");
        }
#endif
        *handles[i] = files[i].get();
      }
    }
    if (newSeqNums) {
      setSeqNum();
    }
    if (newSession) {
      setSession();
    }
  } catch (...) {
    for (std::size_t i = 0; i < handles.size(); ++i) {
      *handles[i] = previous[i];
    }
    throw;
  }
  for (std::size_t i = 0; i < files.size(); ++i) {
    if (previous[i]) {
      fclose(previous[i]);
    }
    files[i].release();
  }
}

void FileStore::populateCache() {
  NumToOffset offsets;
  MemoryStore cache(m_cache.getCreationTime());
  uint64_t bodySize = 0;
  if (m_msgFile) {
#ifdef _MSC_VER
    struct _stat64 bodyStat;
    const int result = _fstat64(_fileno(m_msgFile), &bodyStat);
#else
    struct stat bodyStat;
    const int result = fstat(fileno(m_msgFile), &bodyStat);
#endif
    if (result != 0 || bodyStat.st_size < 0) {
      throw IOException("Unable to stat message body");
    }
    bodySize = static_cast<uint64_t>(bodyStat.st_size);
  }
  if (!m_headerFile && bodySize != 0) {
    throw IOException("Message body has no header file");
  }

  const auto skipSpace = [](FILE *file, int &c) {
    while (c != EOF && std::isspace(static_cast<unsigned char>(c))) {
      c = fgetc(file);
    }
  };
  const auto readNumber = [](FILE *file, int &c) {
    std::string digits;
    while (c >= '0' && c <= '9' && digits.size() < 20) {
      digits += static_cast<char>(c);
      c = fgetc(file);
    }
    uint64_t value;
    if (!UInt64Convertor::convert(digits, value)) {
      throw IOException("Invalid file store number");
    }
    return value;
  };

  if (m_headerFile) {
    int c = fgetc(m_headerFile);
    skipSpace(m_headerFile, c);
    while (c != EOF) {
      const auto sequence = readNumber(m_headerFile, c);
      if (c != ',') {
        throw IOException("Invalid message header row");
      }
      c = fgetc(m_headerFile);
      const auto offset = readNumber(m_headerFile, c);
      if (c != ',') {
        throw IOException("Invalid message header row");
      }
      c = fgetc(m_headerFile);
      const auto size = readNumber(m_headerFile, c);
      if ((c != EOF && !std::isspace(static_cast<unsigned char>(c))) || sequence == 0
          || offset > static_cast<uint64_t>(INT64_MAX) || size > SIZE_MAX || offset > bodySize
          || size > bodySize - offset
          || !offsets.emplace(sequence, OffsetSize(static_cast<int64_t>(offset), static_cast<std::size_t>(size)))
                  .second) {
        throw IOException("Invalid message header range or duplicate sequence");
      }
      skipSpace(m_headerFile, c);
    }
    if (ferror(m_headerFile)) {
      throw IOException("Unable to read message headers");
    }
  }

  if (m_seqNumsFile) {
    int c = fgetc(m_seqNumsFile);
    skipSpace(m_seqNumsFile, c);
    const auto sender = readNumber(m_seqNumsFile, c);
    skipSpace(m_seqNumsFile, c);
    if (c != ':') {
      throw IOException("Invalid sequence number pair");
    }
    c = fgetc(m_seqNumsFile);
    skipSpace(m_seqNumsFile, c);
    const auto target = readNumber(m_seqNumsFile, c);
    skipSpace(m_seqNumsFile, c);
    if (sender == 0 || target == 0 || c != EOF || ferror(m_seqNumsFile)) {
      throw IOException("Invalid sequence number pair");
    }
    cache.setNextSenderMsgSeqNum(sender);
    cache.setNextTargetMsgSeqNum(target);
  }

  if (m_sessionFile) {
    char time[22];
    int length = 0;
    int c = fgetc(m_sessionFile);
    skipSpace(m_sessionFile, c);
    if (c == EOF || ungetc(c, m_sessionFile) == EOF) {
      throw IOException("Invalid session timestamp");
    }
#ifdef HAVE_FSCANF_S
    int result = FILE_FSCANF(m_sessionFile, "%21s%n", time, 22, &length);
#else
    int result = FILE_FSCANF(m_sessionFile, "%21s%n", time, &length);
#endif
    c = fgetc(m_sessionFile);
    skipSpace(m_sessionFile, c);
    if (result != 1 || c != EOF || ferror(m_sessionFile)) {
      throw IOException("Invalid session timestamp");
    }
    try {
      cache.setCreationTime(UtcTimeStampConvertor::convert(std::string(time, length)));
    } catch (const FieldConvertError &) {
      throw IOException("Invalid session timestamp");
    }
  }
  m_cache = cache;
  m_offsets.swap(offsets);
}

MessageStore *FileStoreFactory::create(const UtcTimeStamp &now, const SessionID &sessionID) {
  if (m_path.size()) {
    return new FileStore(now, m_path, sessionID);
  }

  std::string path;
  Dictionary settings = m_settings.get(sessionID);
  path = settings.getString(FILE_STORE_PATH);
  return new FileStore(now, path, sessionID);
}

void FileStoreFactory::destroy(MessageStore *pStore) { delete pStore; }

bool FileStore::set(SEQNUM msgSeqNum, const std::string &msg) EXCEPT(IOException) {
  if (FILE_SEEK(m_msgFile, 0, SEEK_END)) {
    throw IOException("Cannot seek to end of " + m_msgFileName);
  }
  if (FILE_SEEK(m_headerFile, 0, SEEK_END)) {
    throw IOException("Cannot seek to end of " + m_headerFileName);
  }

  int64_t offset = FILE_TELL(m_msgFile);
  if (offset < 0) {
    throw IOException("Unable to get file pointer position from " + m_msgFileName);
  }
  std::size_t size = msg.size();

  if (fprintf(m_headerFile, "%" SCNu64 ",%" PRId64 ",%zu ", msgSeqNum, offset, size) < 0) {
    throw IOException("Unable to write to file " + m_headerFileName);
  }
  std::pair<NumToOffset::iterator, bool> it
      = m_offsets.insert(NumToOffset::value_type(msgSeqNum, std::make_pair(offset, size)));
  if (it.second == false) {
    it.first->second = std::make_pair(offset, size);
  }
  fwrite(msg.c_str(), sizeof(char), msg.size(), m_msgFile);
  if (ferror(m_msgFile)) {
    throw IOException("Unable to write to file " + m_msgFileName);
  }
  if (fflush(m_msgFile) == EOF) {
    throw IOException("Unable to flush file " + m_msgFileName);
  }
  if (fflush(m_headerFile) == EOF) {
    throw IOException("Unable to flush file " + m_headerFileName);
  }
  return true;
}

void FileStore::get(SEQNUM begin, SEQNUM end, std::vector<std::string> &result) const EXCEPT(IOException) {
  result.clear();
  std::string msg;
  for (auto i = begin; i <= end && i != 0; ++i) {
    if (get(i, msg)) {
      result.push_back(msg);
    }
  }
}

SEQNUM FileStore::getNextSenderMsgSeqNum() const EXCEPT(IOException) { return m_cache.getNextSenderMsgSeqNum(); }

SEQNUM FileStore::getNextTargetMsgSeqNum() const EXCEPT(IOException) { return m_cache.getNextTargetMsgSeqNum(); }

void FileStore::setNextSenderMsgSeqNum(SEQNUM value) EXCEPT(IOException) {
  m_cache.setNextSenderMsgSeqNum(value);
  setSeqNum();
}

void FileStore::setNextTargetMsgSeqNum(SEQNUM value) EXCEPT(IOException) {
  m_cache.setNextTargetMsgSeqNum(value);
  setSeqNum();
}

void FileStore::incrNextSenderMsgSeqNum() EXCEPT(IOException) {
  m_cache.incrNextSenderMsgSeqNum();
  setSeqNum();
}

void FileStore::incrNextTargetMsgSeqNum() EXCEPT(IOException) {
  m_cache.incrNextTargetMsgSeqNum();
  setSeqNum();
}

UtcTimeStamp FileStore::getCreationTime() const EXCEPT(IOException) { return m_cache.getCreationTime(); }

void FileStore::reset(const UtcTimeStamp &now) EXCEPT(IOException) {
  try {
    m_cache.reset(now);
    m_offsets.clear();
    open(true);
    setSession();
  } catch (std::exception &e) {
    throw IOException(e.what());
  }
}

void FileStore::refresh() EXCEPT(IOException) {
  try {
    open(false);
  } catch (std::exception &e) {
    throw IOException(e.what());
  }
}

void FileStore::setSeqNum() {
  rewind(m_seqNumsFile);
  fprintf(m_seqNumsFile, seqNumPairFileFormat.c_str(), getNextSenderMsgSeqNum(), getNextTargetMsgSeqNum());
  if (ferror(m_seqNumsFile)) {
    throw IOException("Unable to write to file " + m_seqNumsFileName);
  }
  if (fflush(m_seqNumsFile)) {
    throw IOException("Unable to flush file " + m_seqNumsFileName);
  }
}

void FileStore::setSession() {
  rewind(m_sessionFile);
  fprintf(m_sessionFile, "%s", UtcTimeStampConvertor::convert(m_cache.getCreationTime()).c_str());
  if (ferror(m_sessionFile)) {
    throw IOException("Unable to write to file " + m_sessionFileName);
  }
  if (fflush(m_sessionFile)) {
    throw IOException("Unable to flush file " + m_sessionFileName);
  }
}

bool FileStore::get(SEQNUM msgSeqNum, std::string &msg) const EXCEPT(IOException) {
  NumToOffset::const_iterator find = m_offsets.find(msgSeqNum);
  if (find == m_offsets.end()) {
    return false;
  }
  const OffsetSize &offset = find->second;
  if (FILE_SEEK(m_msgFile, offset.first, SEEK_SET)) {
    throw IOException("Unable to seek in file " + m_msgFileName);
  }
  std::string value(offset.second, '\0');
  const std::size_t count = fread(value.data(), 1, value.size(), m_msgFile);
  if (ferror(m_msgFile) || count != value.size()) {
    throw IOException("Unable to read complete message body");
  }
  msg = std::move(value);
  return true;
}

} // namespace FIX
