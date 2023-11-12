

#include "txn/mvcc_storage.h"

// Init the storage
void MVCCStorage::InitStorage()
{
  for (int i = 0; i < 1000000; i++)
  {
    Write(i, 0, 0);
    Mutex *key_mutex = new Mutex();
    mutexs_[i] = key_mutex;
  }
}

// Free memory.
MVCCStorage::~MVCCStorage()
{
  for (unordered_map<Key, deque<Version *> *>::iterator it = mvcc_data_.begin();
       it != mvcc_data_.end(); ++it)
  {
    delete it->second;
  }

  mvcc_data_.clear();

  for (unordered_map<Key, Mutex *>::iterator it = mutexs_.begin();
       it != mutexs_.end(); ++it)
  {
    delete it->second;
  }

  mutexs_.clear();
}

// Lock the key to protect its version_list. Remember to lock the key when you read/update the version_list
void MVCCStorage::Lock(Key key)
{
  mutexs_[key]->Lock();
}

// Unlock the key.
void MVCCStorage::Unlock(Key key)
{
  mutexs_[key]->Unlock();
}

// MVCC Read
bool MVCCStorage::Read(Key key, Value *result, int txn_unique_id)
{
  //
  // Implement this method!

  // Hint: Iterate the version_lists and return the verion whose write timestamp
  // (version_id) is the largest write timestamp less than or equal to txn_unique_id.

  // If there exists a record for the specified key, sets '*result' equal to
  // the value associated with the key and returns true, else returns false;
  // The third parameter is the txn_unique_id(txn timestamp), which is used for MVCC.
  if (mvcc_data_.find(key) == mvcc_data_.end())
  {
    return false;
  }

  int maxVer = -1;
  for (auto it = mvcc_data_[key]->begin(); it != mvcc_data_[key]->end(); it++)
  {
    if ((*it)->version_id_ <= txn_unique_id && (*it)->version_id_ > maxVer)
    {
      maxVer = std::max(maxVer, (*it)->version_id_);
      (*it)->max_read_id_ = std::max((*it)->max_read_id_, txn_unique_id);
      *result = (*it)->value_;
    }
  }

  return true;
}

// Check whether apply or abort the write
bool MVCCStorage::CheckWrite(Key key, int txn_unique_id)
{
  //
  // Implement this method!

  // Hint: Before all writes are applied, we need to make sure that each write
  // can be safely applied based on MVCC timestamp ordering protocol. This method
  // only checks one key, so you should call this method for each key in the
  // write_set. Return true if this key passes the check, return false if not.
  // Note that you don't have to call Lock(key) in this method, just
  // call Lock(key) before you call this method and call Unlock(key) afterward.

  int maxVer = -1;
  Version maxVerValue = Version();
  for (auto element : *mvcc_data_[key])
  {
    if (element->version_id_ > maxVer)
    {
      maxVer = element->version_id_;
      maxVerValue = *element;
    }
  }
  return maxVerValue.max_read_id_ <= txn_unique_id;
}

// MVCC Write, call this method only if CheckWrite return true.
void MVCCStorage::Write(Key key, Value value, int txn_unique_id)
{
  //
  // Implement this method!

  // Hint: Insert a new version (malloc a Version and specify its value/version_id/max_read_id)
  // into the version_lists. Note that InitStorage() also calls this method to init storage.
  // Note that you don't have to call Lock(key) in this method, just
  // call Lock(key) before you call this method and call Unlock(key) afterward.
  // Note that the performance would be much better if you organize the versions in decreasing order.
  Version *new_version = new Version();
  new_version->value_ = value;
  new_version->version_id_ = txn_unique_id;
  new_version->max_read_id_ = txn_unique_id;

  if (mvcc_data_.find(key) == mvcc_data_.end())
  {
    deque<Version *> *version_list = new deque<Version *>();
    version_list->push_back(new_version);
    mvcc_data_[key] = version_list;
  }
  else
  {
    // Cek apakah ini update
    for (auto it = mvcc_data_[key]->begin(); it != mvcc_data_[key]->end(); it++)
    {
      if ((*it)->version_id_ == txn_unique_id)
      {
        (*it)->value_ = value;
        return;
      }
    }
    mvcc_data_[key]->push_back(new_version);
  }
}
