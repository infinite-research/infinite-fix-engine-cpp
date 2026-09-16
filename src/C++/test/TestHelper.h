#ifndef FIX_TEST_HELPER_H
#define FIX_TEST_HELPER_H

#include <Application.h>
#include <DataDictionary.h>
#include <FileLog.h>
#include <FileStore.h>
#include <Log.h>
#include <SessionSettings.h>
#include <iostream>

namespace FIX {
struct TestSettings {
  static const uint16_t port;
  static FIX::SessionSettings sessionSettings;
  static std::string specPath;

  static std::string pathForSpec(const std::string &spec);
};

class TestApplication : public NullApplication {
public:
  void fromApp(const Message &, const SessionID &)
      EXCEPT(FieldNotFound, IncorrectDataFormat, IncorrectTagValue, UnsupportedMessageType) {}
  void onRun() {}
};

class FileStoreTestAccess {
public:
  static void setCachedSequenceNumbers(FileStore &store, SEQNUM sender, SEQNUM target) {
    store.m_cache.setNextSenderMsgSeqNum(sender);
    store.m_cache.setNextTargetMsgSeqNum(target);
  }
};

class TestFileStoreFactory : public FileStoreFactory {
public:
  using FileStoreFactory::FileStoreFactory;

  MessageStore *create(const UtcTimeStamp &now, const SessionID &id) override {
    return m_store = static_cast<FileStore *>(FileStoreFactory::create(now, id));
  }

  FileStore &store() const { return *m_store; }

private:
  FileStore *m_store = nullptr;
};

inline void deleteSession(std::string sender, std::string target) {
  file_unlink(("store/FIX.4.2-" + sender + "-" + target + ".messages").c_str());
  file_unlink(("store/FIX.4.2-" + sender + "-" + target + ".header").c_str());
  file_unlink(("store/FIX.4.2-" + sender + "-" + target + ".seqnums").c_str());
  file_unlink(("store/FIX.4.2-" + sender + "-" + target + ".session").c_str());
}

inline void destroySocket(socket_handle s) {
  socket_close(s);
  socket_invalidate(s);
}

socket_handle inline createSocket(uint16_t port, const char *address) {
  socket_handle sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

  sockaddr_in addr;
  addr.sin_family = PF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(address);

  int result = ::connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  if (result != 0) {
    destroySocket(sock);
    return INVALID_SOCKET_HANDLE;
  }
  return sock;
}

class ExeceptionStore : public MessageStore {
public:
  virtual ~ExeceptionStore() {};

  bool set(SEQNUM, const std::string &) EXCEPT(IOException) { throw IOException("set threw an IOException"); }
  void get(SEQNUM, SEQNUM, std::vector<std::string> &) const EXCEPT(IOException) {
    throw IOException("get threw an IOException");
  }
  SEQNUM getNextSenderMsgSeqNum() const EXCEPT(IOException) { return 0; };
  SEQNUM getNextTargetMsgSeqNum() const EXCEPT(IOException) { return 0; };
  void setNextSenderMsgSeqNum(SEQNUM value) EXCEPT(IOException) {};
  void setNextTargetMsgSeqNum(SEQNUM value) EXCEPT(IOException) {};
  void incrNextSenderMsgSeqNum() EXCEPT(IOException) {};
  void incrNextTargetMsgSeqNum() EXCEPT(IOException) {};

  UtcTimeStamp getCreationTime() const EXCEPT(IOException) { return UtcTimeStamp::now(); };

  void reset(const UtcTimeStamp &) EXCEPT(IOException) { throw IOException("reset IOException"); };
  void refresh() EXCEPT(IOException) {};
};

class ExceptionMessageStoreFactory : public MessageStoreFactory {
public:
  ExceptionMessageStoreFactory(const SessionSettings &settings)
      : m_settings(settings) {};
  ExceptionMessageStoreFactory(const std::string &path)
      : m_path(path) {};

  MessageStore *create(const UtcTimeStamp &, const SessionID &) { return new ExeceptionStore(); };
  void destroy(MessageStore *pStore) { delete pStore; }

private:
  std::string m_path;
  SessionSettings m_settings;
};

} // namespace FIX

#endif
