// See www.openfst.org for extensive documentation on this weighted
// finite-state transducer library.

#ifndef FST_LIB_GENERIC_REGISTER_H_
#define FST_LIB_GENERIC_REGISTER_H_

#ifndef FST_NO_DYNAMIC_LINKING
#include <dlfcn.h>
#include <fst/compat.h>
#endif
#include <map>
#include <string>

#include <fst/types.h>

// Generic class representing a globally-stored correspondence between
// objects of KeyType and EntryType.
// KeyType must:
//  a) be such as can be stored as a key in a std::map<>
//  b) be concatenable with a const char* with the + operator
//     (or you must subclass and redefine LoadEntryFromSharedObject)
// EntryType must be default constructible.
//
// The third template parameter should be the type of a subclass of this class
// (think CRTP). This is to allow GetRegister() to instantiate and return
// an object of the appropriate type.

namespace fst {

template <class KeyType, class EntryType, class RegisterType>
class GenericRegister {
 public:
  typedef KeyType Key;
  typedef EntryType Entry;

  static RegisterType *GetRegister() {
    FstOnceInit(&register_init_, &RegisterType::Init);

    return register_;
  }

  void SetEntry(const KeyType &key, const EntryType &entry) {
    MutexLock l(register_lock_);

    register_table_.insert(std::make_pair(key, entry));
  }

  EntryType GetEntry(const KeyType &key) const {
    const EntryType *entry = LookupEntry(key);
    if (entry) {
      return *entry;
    } else {
      return LoadEntryFromSharedObject(key);
    }
  }

  virtual ~GenericRegister() {}

 protected:
  // Override this if you want to be able to load missing definitions from
  // shared object files.
  virtual EntryType LoadEntryFromSharedObject(const KeyType &key) const {
#ifdef FST_NO_DYNAMIC_LINKING
    return EntryType();
#else
    string so_filename = ConvertKeyToSoFilename(key);

    void *handle = dlopen(so_filename.c_str(), RTLD_LAZY);
    if (handle == nullptr) {
      LOG(ERROR) << "GenericRegister::GetEntry: " << dlerror();
      return EntryType();
    }
#ifdef RUN_MODULE_INITIALIZERS
    RUN_MODULE_INITIALIZERS();
#endif

    // We assume that the DSO constructs a static object in its global
    // scope that does the registration. Thus we need only load it, not
    // call any methods.
    const EntryType *entry = this->LookupEntry(key);
    if (entry == nullptr) {
      LOG(ERROR) << "GenericRegister::GetEntry: "
                 << "lookup failed in shared object: " << so_filename;
      return EntryType();
    }
    return *entry;
#endif  // FST_NO_DYNAMIC_LINKING
  }

  // Override this to define how to turn a key into an SO filename.
  virtual string ConvertKeyToSoFilename(const KeyType &key) const = 0;

  virtual const EntryType *LookupEntry(const KeyType &key) const {
    MutexLock l(register_lock_);

    typename RegisterMapType::const_iterator it = register_table_.find(key);

    if (it != register_table_.end()) {
      return &it->second;
    } else {
      return nullptr;
    }
  }

 private:
  typedef std::map<KeyType, EntryType> RegisterMapType;

  static void Init() {
    register_lock_ = new Mutex;
    register_ = new RegisterType;
  }

  static FstOnceType register_init_;
  static Mutex *register_lock_;
  static RegisterType *register_;

  RegisterMapType register_table_;
};

template <class KeyType, class EntryType, class RegisterType>
FstOnceType GenericRegister<KeyType, EntryType,
                               RegisterType>::register_init_ = FST_ONCE_INIT;

template <class KeyType, class EntryType, class RegisterType>
Mutex *GenericRegister<KeyType, EntryType, RegisterType>::register_lock_ =
    nullptr;

template <class KeyType, class EntryType, class RegisterType>
RegisterType *GenericRegister<KeyType, EntryType, RegisterType>::register_ =
    nullptr;

//
// GENERIC REGISTRATION
//

// Generic register-er class capable of creating new register entries in the
// given RegisterType template parameter. This type must define types Key
// and Entry, and have appropriate static GetRegister() and instance
// SetEntry() functions. An easy way to accomplish this is to have RegisterType
// be the type of a subclass of GenericRegister.
template <class RegisterType>
class GenericRegisterer {
 public:
  typedef typename RegisterType::Key Key;
  typedef typename RegisterType::Entry Entry;

  GenericRegisterer(Key key, Entry entry) {
    RegisterType *reg = RegisterType::GetRegister();
    reg->SetEntry(key, entry);
  }
};

}  // namespace fst

#endif  // FST_LIB_GENERIC_REGISTER_H_
