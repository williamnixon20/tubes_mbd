# Tugas besar MBD
Tugas ini menggunakan CMSC828N: Assignment 2 sebagai referensi.
https://github.com/kunrenyale/CMSC828N_assignment2/tree/master

# Tentang
Pada repository ini, dilakukan beberapa simulasi, yaitu:
1. Skema locking dengan ekslusif lock (Lock A)
2. Skema locking dengan shared + ekslusif lock (Lock B)
3. Skema OCC dengan serial validation
4. Skema MVCC dengan timestamp ordering

# Cara menjalankan
1. Gunakan platform linux. Tidak diuji pada platform lain.
2. Jalankan `make test`.
3. Sistem akan dicompile. Setelah itu, akan muncul pengujian pada beberapa skenario.

# Kode yang diubah
  txn/lock_manager.cc:
    all methods (aside for the constructor and deconstructor) for classes 'LockManagerA' (Part 1A) and 'LockManagerB' (Part 1B)

  txn/txn_processor.cc:
    'TxnProcessor::RunOCCScheduler' method (Part 2)
    'TxnProcessor::RunMVCCScheduler' method (Part 4)
    
  txn/mvcc_storage.cc:
    'MVCCStorage::Read' method (Part 4)
    'MVCCStorage::Write' method (Part 4)
    'MVCCStorage::CheckWrite' method (Part 4)

# Penjelasan detail

--------------------------------------------------------------------------------
## Simplified 2 Phase Locking
--------------------------------------------------------------------------------
1. Saat masuk ke sistem, setiap transaksi meminta kunci EKSKLUSIF (skema A) atau EKSLUSIF/SHARED (skema B) pada SETIAP item yang akan dibaca atau ditulis.
2. Jika permintaan kunci ditolak:
-  Jika seluruh transaksi melibatkan hanya satu permintaan baca atau tulis, maka biarkan transaksi menunggu sampai permintaan tersebut disetujui dan kemudian lanjutkan ke langkah (3).
-  Jika tidak, segera lepaskan semua kunci yang telah disetujui sebelum penolakan ini, dan segera batalkan dan antrikan transaksi untuk diulang di waktu yang lebih kemudian.
3. Eksekusi transaksi
4. Lepaskan SEMUA kunci pada saat komit/batal.

Dapat dilihat, bahwa pada skema ini, kunci hanya dilepas pada saat transaksi komit/batal, sehingga memenuhi prinsip 2 PL.

--------------------------------------------------------------------------------
## Serial Optimistic Concurrency Control (OCC)
--------------------------------------------------------------------------------

Pseudocode yang diterapkan sebagai berikut

  while (tp_.Active()) {
    Get the next new transaction request (if one is pending) and pass it to an execution thread.
    Deal with all transactions that have finished running (see below).
  }

  In the execution thread (we are providing you this code):
    Record start time
    Perform "read phase" of transaction:
       Read all relevant data from storage
       Execute the transaction logic (i.e. call Run() on the transaction)

  Dealing with a finished transaction (you must write this code):
    // Validation phase:
    for (each record whose key appears in the txn's read and write sets) {
      if (the record was last updated AFTER this transaction's start time) {
        Validation fails!
      }
    }

    // Commit/restart
    if (validation failed) {
      Cleanup txn
      Completely restart the transaction.
    } else {
      Apply all writes
      Mark transaction as committed
    }

  cleanup txn:
    txn->reads_.clear();
    txn->writes_.clear();
    txn->status_ = INCOMPLETE;
       
  Restart txn:
    mutex_.Lock();
    txn->unique_id_ = next_unique_id_;
    next_unique_id_++;
    txn_requests_.Push(txn);
    mutex_.Unlock(); 

--------------------------------------------------------------------------------
## Multiversion Timestamp Ordering Concurrency Control.  20 points
--------------------------------------------------------------------------------

Pseudocode for the algorithm to implement (in the RunMVCCScheduler method):

  while (tp_.Active()) {
    Get the next new transaction request (if one is pending) and pass it to an execution thread.
  }

  In the execution thread:
    
    Read all necessary data for this transaction from storage (Note that unlike the version of MVCC from class, you should lock the key before each read)
    Execute the transaction logic (i.e. call Run() on the transaction)
    Acquire all locks for keys in the write_set_
    Call MVCCStorage::CheckWrite method to check all keys in the write_set_
    If (each key passed the check)
      Apply the writes
      Release all locks for keys in the write_set_
    else if (at least one key failed the check)
      Release all locks for keys in the write_set_
      Cleanup txn
      Completely restart the transaction.
  
  cleanup txn:
    txn->reads_.clear();
    txn->writes_.clear();
    txn->status_ = INCOMPLETE;

  Restart txn:
    mutex_.Lock();
    txn->unique_id_ = next_unique_id_;
    next_unique_id_++;
    txn_requests_.Push(txn);
    mutex_.Unlock(); 

------------------------------------------------------------------------

----------
# Oleh Kelompok 2 K1
----------
13521069	Louis Caesa Kesuma	2
13521123	William Nixon	2
13521131	Jeremya Dharmawan Raharjo	2
13521139	Nathania Calista Djunaedi	2

