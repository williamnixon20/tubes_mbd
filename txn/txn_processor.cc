
#include "txn/txn_processor.h"
#include <stdio.h>
#include <set>

#include "txn/lock_manager.h"

// Thread & queue counts for StaticThreadPool initialization.
#define THREAD_COUNT 8

TxnProcessor::TxnProcessor(CCMode mode)
    : mode_(mode), tp_(THREAD_COUNT), next_unique_id_(1)
{
  if (mode_ == LOCKING_EXCLUSIVE_ONLY)
    lm_ = new LockManagerA(&ready_txns_);
  else if (mode_ == LOCKING)
    lm_ = new LockManagerB(&ready_txns_);

  // Create the storage
  if (mode_ == MVCC)
  {
    storage_ = new MVCCStorage();
  }
  else
  {
    storage_ = new Storage();
  }

  storage_->InitStorage();

  // Start 'RunScheduler()' running.
  cpu_set_t cpuset;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  CPU_ZERO(&cpuset);
  for (int i = 0; i < 7; i++)
  {
    CPU_SET(i, &cpuset);
  }
  pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
  pthread_t scheduler_;
  pthread_create(&scheduler_, &attr, StartScheduler, reinterpret_cast<void *>(this));
}

void *TxnProcessor::StartScheduler(void *arg)
{
  reinterpret_cast<TxnProcessor *>(arg)->RunScheduler();
  return NULL;
}

TxnProcessor::~TxnProcessor()
{
  if (mode_ == LOCKING_EXCLUSIVE_ONLY || mode_ == LOCKING)
    delete lm_;

  delete storage_;
}

void TxnProcessor::NewTxnRequest(Txn *txn)
{
  // Atomically assign the txn a new number and add it to the incoming txn
  // requests queue.
  mutex_.Lock();
  txn->unique_id_ = next_unique_id_;
  next_unique_id_++;
  txn_requests_.Push(txn);
  mutex_.Unlock();
}

Txn *TxnProcessor::GetTxnResult()
{
  Txn *txn;
  while (!txn_results_.Pop(&txn))
  {
    // No result yet. Wait a bit before trying again (to reduce contention on
    // atomic queues).
    sleep(0.000001);
  }
  return txn;
}

void TxnProcessor::RunScheduler()
{
  switch (mode_)
  {
  case SERIAL:
    RunSerialScheduler();
    break;
  case LOCKING:
    RunLockingScheduler();
    break;
  case LOCKING_EXCLUSIVE_ONLY:
    RunLockingScheduler();
    break;
  case OCC:
    RunOCCScheduler();
    break;
  // case P_OCC:
  //   RunOCCParallelScheduler();
  //   break;
  case MVCC:
    RunMVCCScheduler();
  }
}

void TxnProcessor::RunSerialScheduler()
{
  Txn *txn;
  while (tp_.Active())
  {
    // Get next txn request.
    if (txn_requests_.Pop(&txn))
    {
      // Execute txn.
      ExecuteTxn(txn);

      // Commit/abort txn according to program logic's commit/abort decision.
      if (txn->Status() == COMPLETED_C)
      {
        ApplyWrites(txn);
        txn->status_ = COMMITTED;
      }
      else if (txn->Status() == COMPLETED_A)
      {
        txn->status_ = ABORTED;
      }
      else
      {
        // Invalid TxnStatus!
        DIE("Completed Txn has invalid TxnStatus: " << txn->Status());
      }

      // Return result to client.
      txn_results_.Push(txn);
    }
  }
}

void TxnProcessor::RunLockingScheduler()
{
  Txn *txn;
  while (tp_.Active())
  {
    // Start processing the next incoming transaction request.
    if (txn_requests_.Pop(&txn))
    {
      bool blocked = false;
      // Request read locks.
      for (set<Key>::iterator it = txn->readset_.begin();
           it != txn->readset_.end(); ++it)
      {
        if (!lm_->ReadLock(txn, *it))
        {
          blocked = true;
          // If readset_.size() + writeset_.size() > 1, and blocked, just abort
          if (txn->readset_.size() + txn->writeset_.size() > 1)
          {
            // Release all locks that already acquired
            for (set<Key>::iterator it_reads = txn->readset_.begin(); true; ++it_reads)
            {
              lm_->Release(txn, *it_reads);
              if (it_reads == it)
              {
                break;
              }
            }
            break;
          }
        }
      }

      if (blocked == false)
      {
        // Request write locks.
        for (set<Key>::iterator it = txn->writeset_.begin();
             it != txn->writeset_.end(); ++it)
        {
          if (!lm_->WriteLock(txn, *it))
          {
            blocked = true;
            // If readset_.size() + writeset_.size() > 1, and blocked, just abort
            if (txn->readset_.size() + txn->writeset_.size() > 1)
            {
              // Release all read locks that already acquired
              for (set<Key>::iterator it_reads = txn->readset_.begin(); it_reads != txn->readset_.end(); ++it_reads)
              {
                lm_->Release(txn, *it_reads);
              }
              // Release all write locks that already acquired
              for (set<Key>::iterator it_writes = txn->writeset_.begin(); true; ++it_writes)
              {
                lm_->Release(txn, *it_writes);
                if (it_writes == it)
                {
                  break;
                }
              }
              break;
            }
          }
        }
      }

      // If all read and write locks were immediately acquired, this txn is
      // ready to be executed. Else, just restart the txn
      if (blocked == false)
      {
        ready_txns_.push_back(txn);
      }
      else if (blocked == true && (txn->writeset_.size() + txn->readset_.size() > 1))
      {
        mutex_.Lock();
        txn->unique_id_ = next_unique_id_;
        next_unique_id_++;
        txn_requests_.Push(txn);
        mutex_.Unlock();
      }
    }

    // Process and commit all transactions that have finished running.
    while (completed_txns_.Pop(&txn))
    {
      // Commit/abort txn according to program logic's commit/abort decision.
      if (txn->Status() == COMPLETED_C)
      {
        ApplyWrites(txn);
        txn->status_ = COMMITTED;
      }
      else if (txn->Status() == COMPLETED_A)
      {
        txn->status_ = ABORTED;
      }
      else
      {
        // Invalid TxnStatus!
        DIE("Completed Txn has invalid TxnStatus: " << txn->Status());
      }

      // Release read locks.
      for (set<Key>::iterator it = txn->readset_.begin();
           it != txn->readset_.end(); ++it)
      {
        lm_->Release(txn, *it);
      }
      // Release write locks.
      for (set<Key>::iterator it = txn->writeset_.begin();
           it != txn->writeset_.end(); ++it)
      {
        lm_->Release(txn, *it);
      }

      // Return result to client.
      txn_results_.Push(txn);
    }

    // Start executing all transactions that have newly acquired all their
    // locks.
    while (ready_txns_.size())
    {
      // Get next ready txn from the queue.
      txn = ready_txns_.front();
      ready_txns_.pop_front();

      // Start txn running in its own thread.
      tp_.RunTask(new Method<TxnProcessor, void, Txn *>(
          this,
          &TxnProcessor::ExecuteTxn,
          txn));
    }
  }
}

void TxnProcessor::ExecuteTxn(Txn *txn)
{

  // Get the start time
  txn->occ_start_time_ = GetTime();

  // Read everything in from readset.
  for (set<Key>::iterator it = txn->readset_.begin();
       it != txn->readset_.end(); ++it)
  {
    // Save each read result iff record exists in storage.
    Value result;
    if (storage_->Read(*it, &result))
      txn->reads_[*it] = result;
  }

  // Also read everything in from writeset.
  for (set<Key>::iterator it = txn->writeset_.begin();
       it != txn->writeset_.end(); ++it)
  {
    // Save each read result iff record exists in storage.
    Value result;
    if (storage_->Read(*it, &result))
      txn->reads_[*it] = result;
  }

  // Execute txn's program logic.
  txn->Run();

  // Hand the txn back to the RunScheduler thread.
  completed_txns_.Push(txn);
}

void TxnProcessor::ApplyWrites(Txn *txn)
{
  // Write buffered writes out to storage.
  for (map<Key, Value>::iterator it = txn->writes_.begin();
       it != txn->writes_.end(); ++it)
  {
    storage_->Write(it->first, it->second, txn->unique_id_);
  }
}

void TxnProcessor::RunOCCScheduler()
{
  // Commit/restart
  // Aborted will be cleanup and restarted.
  // Referring to Notes in https://github.com/kunrenyale/CMSC828N_assignment2/tree/master
  // You should only put the committed transaction into the txn_results_ queue. Don't put the aborted transaction into the txn_results_ queue. Instead, put it into txn_requests_ queue.
  // mutex_.Lock();
  //   txn->unique_id_ = next_unique_id_;
  //   next_unique_id_++;
  //   txn_requests_.Push(txn);
  //   mutex_.Unlock();

  while (tp_.Active())
  {
    Txn *txn;
    if (txn_requests_.Pop(&txn))
    {
      this->tp_.RunTask(new Method<TxnProcessor, void, Txn *>(this, &TxnProcessor::ExecuteTxn, txn));
    }

    Txn *finished_txn;
    while (completed_txns_.Pop(&finished_txn))
    {
      // If invalid, will change the status to COMPLETED_A
      // IF VALID, will be marked COMPLETED_C
      ValidateTxn(finished_txn);

      if (finished_txn->Status() == ABORTED || finished_txn->Status() == COMPLETED_A)
      {
        CleanupTxn(finished_txn);
        RestartTxn(finished_txn);
      }
      else
      {
        ApplyWrites(finished_txn);
        finished_txn->status_ = COMMITTED;
        txn_results_.Push(finished_txn);
      }
    }
  }
}

void TxnProcessor::CleanupTxn(Txn *txn)
{
  txn->reads_.clear();
  txn->writes_.clear();
  txn->status_ = INCOMPLETE;
}

void TxnProcessor::RestartTxn(Txn *txn)
{
  mutex_.Lock();
  txn->unique_id_ = next_unique_id_;
  next_unique_id_++;
  txn_requests_.Push(txn);
  mutex_.Unlock();
}

void TxnProcessor::ValidateTxn(Txn *txn)
{
  // Check if it has been aborted from the start.
  if (txn->status_ == COMPLETED_A)
  {
    return;
  }
  // Check if the readset is still valid, if not abort
  for (auto it = txn->readset_.begin(); it != txn->readset_.end(); ++it)
  {
    if (txn->occ_start_time_ < storage_->Timestamp(*it))
    {
      txn->status_ = COMPLETED_A;
      return;
    }
  }

  // Check if the writeset is still valid, if not abort
  for (auto it = txn->writeset_.begin(); it != txn->writeset_.end(); ++it)
  {
    if (txn->occ_start_time_ < storage_->Timestamp(*it))
    {
      txn->status_ = COMPLETED_A;
      return;
    }
  }
  // Complete, you can commit.
  txn->status_ = COMPLETED_C;
}

void TxnProcessor::RunOCCParallelScheduler()
{
  // NOT IMPLEMENTED FOR THIS TUBES
  RunSerialScheduler();
}

void TxnProcessor::RunMVCCScheduler()
{
  //
  // Implement this method!

  // Hint:Pop a txn from txn_requests_, and pass it to a thread to execute.
  // Note that you may need to create another execute method, like TxnProcessor::MVCCExecuteTxn.
  while (tp_.Active())
  {
    Txn *txn;
    if (txn_requests_.Pop(&txn))
    {
      this->tp_.RunTask(new Method<TxnProcessor, void, Txn *>(this, &TxnProcessor::MVCCExecuteTxn, txn));
    }
  }
}

void TxnProcessor::MVCCExecuteTxn(Txn *txn)
{
  //  In the execution thread:
  // Read all necessary data for this transaction from storage (Note that unlike the version of MVCC from class, you should lock the key before each read)
  // Execute the transaction logic (i.e. call Run() on the transaction)
  // Acquire all locks for keys in the write_set_
  // Call MVCCStorage::CheckWrite method to check all keys in the write_set_
  // If (each key passed the check)
  //   Apply the writes
  //   Release all locks for keys in the write_set_
  // else if (at least one key failed the check)
  //   Release all locks for keys in the write_set_
  //   Cleanup txn
  //   Completely restart the transaction.

  MVCCReadKeys(txn);
  txn->Run();
  MVCCLockWriteKeys(txn);
  if (MVCCCheckWrites(txn))
  {
    ApplyWrites(txn);
    MVCCUnlockWriteKeys(txn);
    txn->status_ = COMMITTED;
    txn_results_.Push(txn);
  }
  else
  {
    MVCCUnlockWriteKeys(txn);
    CleanupTxn(txn);
    RestartTxn(txn);
  }
}

void TxnProcessor::MVCCReadKeys(Txn *txn)
{
  // Inside your Execution method of MVCC:  when you call read() method to read values from database,
  // please don't forget to provide the third parameter(txn->unique_id_), otherwise the default value is 0 and you always read the oldest version.
  for (auto &e : txn->readset_)
  {
    storage_->Lock(e);
    Value result;
    if (storage_->Read(e, &result, txn->unique_id_))
    {
      txn->reads_[e] = result;
    }
    storage_->Unlock(e);
  }

  for (auto &e : txn->writeset_)
  {
    storage_->Lock(e);
    Value result;
    if (storage_->Read(e, &result, txn->unique_id_))
    {
      txn->reads_[e] = result;
    }
    storage_->Unlock(e);
  }
}
bool TxnProcessor::MVCCCheckWrites(Txn *txn)
{
  for (auto &e : txn->writeset_)
  {
    if (!(storage_->CheckWrite(e, txn->unique_id_)))
    {
      return false;
    }
  }
  return true;
}

void TxnProcessor::MVCCLockWriteKeys(Txn *txn)
{
  for (auto &e : txn->writeset_)
  {
    storage_->Lock(e);
  }
}

void TxnProcessor::MVCCUnlockWriteKeys(Txn *txn)
{
  for (auto &e : txn->writeset_)
  {
    storage_->Unlock(e);
  }
}
