
// Lock manager implementing deterministic two-phase locking as described in
// 'The Case for Determinism in Database Systems'.

// Lock manager implementing deterministic two-phase locking as described in
// 'The Case for Determinism in Database Systems'.

#include "txn/lock_manager.h"

LockManagerA::LockManagerA(deque<Txn *> *ready_txns)
{
  ready_txns_ = ready_txns;
}

bool LockManagerA::WriteLock(Txn *txn, const Key &key)
{
  // Intinya masukin ke queue, kalau queuenya kosong dia jadi ownernya.
  // Tapi kalau queuenya ga kosong, dia harus ngantri.
  LockRequest *lock_request = new LockRequest(EXCLUSIVE, txn);

  if (lock_table_.find(key) == lock_table_.end())
  {
    deque<LockRequest> *lock_requests = new deque<LockRequest>();
    lock_requests->push_back(*lock_request);
    lock_table_[key] = lock_requests;
    return true;
  }
  else
  {
    bool isEmpty = lock_table_[key]->empty();
    deque<LockRequest> *lock_requests = lock_table_[key];
    lock_requests->push_back(*lock_request);

    if (isEmpty)
    {
      return true;
    }
    else
    {
      txn_waits_[txn] += 1;
      return false;
    }
  }
}

bool LockManagerA::ReadLock(Txn *txn, const Key &key)
{
  // Since Part 1A implements ONLY exclusive locks, calls to ReadLock can
  // simply use the same logic as 'WriteLock'.
  return WriteLock(txn, key);
}

void LockManagerA::Release(Txn *txn, const Key &key)
{
  // Hapus dia dari lock table
  for (auto it = lock_table_[key]->begin(); it < lock_table_[key]->end();)
  {
    if (it->txn_ == txn)
    {
      it = lock_table_[key]->erase(it);
    }
    else
    {
      it++;
    }
  }
  txn_waits_[txn] = 0;
  if (!lock_table_[key]->empty())
  {
    Txn *front = lock_table_[key]->front().txn_;
    // Terdepan udah dapet resource, dia nunggu -1 resource
    txn_waits_[front] -= 1;
    // Kalau dia udah 0, dia bisa jalan
    if (txn_waits_[front] == 0)
    {
      ready_txns_->push_back(front);
    }
  }
}

LockMode LockManagerA::Status(const Key &key, vector<Txn *> *owners)
{
  owners->clear();
  owners->push_back(lock_table_[key]->front().txn_);
  if (owners->size() > 0)
    return EXCLUSIVE;
  else
  {
    return UNLOCKED;
  }
}

LockManagerB::LockManagerB(deque<Txn *> *ready_txns)
{
  ready_txns_ = ready_txns;
}

bool LockManagerB::WriteLock(Txn *txn, const Key &key)
{
  // Intinya masukin ke queue, kalau queuenya kosong dia jadi ownernya.
  // Tapi kalau queuenya ga kosong, dia harus ngantri.
  LockRequest *lock_request = new LockRequest(EXCLUSIVE, txn);

  if (lock_table_.find(key) == lock_table_.end())
  {
    deque<LockRequest> *lock_requests = new deque<LockRequest>();
    lock_requests->push_back(*lock_request);
    lock_table_[key] = lock_requests;
    return true;
  }
  else
  {
    bool isEmpty = lock_table_[key]->empty();
    deque<LockRequest> *lock_requests = lock_table_[key];
    lock_requests->push_back(*lock_request);

    if (isEmpty)
    {
      return true;
    }
    else
    {
      txn_waits_[txn] += 1;
      return false;
    }
  }
  return true;
}

bool LockManagerB::ReadLock(Txn *txn, const Key &key)
{
  // Intinya masukin ke queue, kalau queuenya kosong dia jadi ownernya.
  // Tapi kalau queuenya ga kosong, dia harus ngantri.
  LockRequest *lock_request = new LockRequest(SHARED, txn);

  if (lock_table_.find(key) == lock_table_.end())
  {
    deque<LockRequest> *lock_requests = new deque<LockRequest>();
    lock_requests->push_back(*lock_request);
    lock_table_[key] = lock_requests;
    return true;
  }
  else
  {
    bool isEmpty = lock_table_[key]->empty();

    if (isEmpty)
    {
      deque<LockRequest> *lock_requests = lock_table_[key];
      lock_requests->push_back(*lock_request);
      return true;
    }
    else
    {
      // Cek apakah paling belakang shared ato bukan
      bool flag = false;
      for (auto it = lock_table_[key]->begin(); it < lock_table_[key]->end(); it++)
      {
        if (it->mode_ == EXCLUSIVE)
        {
          flag = true;
        }
        else
        {
          if (flag)
          {
            txn_waits_[txn] += 1;
          }
        }
      }
      if (lock_table_[key]->back().mode_ == EXCLUSIVE)
      {
        deque<LockRequest> *lock_requests = lock_table_[key];
        lock_requests->push_back(*lock_request);
        txn_waits_[txn] += 1;
        return false;
      }

      deque<LockRequest> *lock_requests = lock_table_[key];
      lock_requests->push_back(*lock_request);
      return true;
    }
  }
  return true;
}

void LockManagerB::Release(Txn *txn, const Key &key)
{
  for (auto it = lock_table_[key]->begin(); it < lock_table_[key]->end();)
  {
    if (it->txn_ == txn)
    {
      it = lock_table_[key]->erase(it);
    }
    else
    {
      it++;
    }
  }
  txn_waits_[txn] = 0;
  if (!lock_table_[key]->empty())
  {
    for (auto it = lock_table_[key]->begin(); it < lock_table_[key]->end(); it++)
    {
      if (it->mode_ == EXCLUSIVE)
      {
        txn_waits_[it->txn_] -= 1;
        break;
      }
      else
      {
        txn_waits_[it->txn_] -= 1;
      }
    }
    for (auto it = lock_table_[key]->begin(); it < lock_table_[key]->end(); it++)
    {
      if (txn_waits_[it->txn_] == 0)
      {
        ready_txns_->push_back(it->txn_);
      }
    }
  }
}

LockMode LockManagerB::Status(const Key &key, vector<Txn *> *owners)
{
  owners->clear();
  if (lock_table_[key]->size() > 0)
  {
    if (lock_table_[key]->front().mode_ == EXCLUSIVE)
    {
      owners->push_back(lock_table_[key]->front().txn_);
      return EXCLUSIVE;
    }
    else
    {
      for (auto it = lock_table_[key]->begin(); it < lock_table_[key]->end(); it++)
      {
        if (it->mode_ == EXCLUSIVE)
        {
          break;
        }
        owners->push_back(it->txn_);
      }
      return SHARED;
    }
  }
  else
  {
    return UNLOCKED;
  }
}
