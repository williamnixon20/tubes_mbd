# Tugas besar MBD IF3140
Tugas ini menggunakan CMSC828N: Assignment 2 sebagai referensi.
https://github.com/kunrenyale/CMSC828N_assignment2/tree/master

# Tentang
Pada repository ini, dilakukan beberapa simulasi, yaitu:
1. Skema locking dengan ekslusif lock (Lock A)
2. Skema locking dengan shared + ekslusif lock (Lock B)
3. Skema OCC dengan serial validation
4. Skema MVCC dengan timestamp ordering

# Cara menjalankan
1. Gunakan platform linux. Tidak diuji pada platform lain. Apabila menggunakan Windows, gunakan WSL (Windows Subsystem for Linux)
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
## Hasil untuk Implementasi Concurrency Control Protocol
Berikut merupakan hasil saat menjalankan program kami:
```bash
== bin/txn/lock_manager_test ==
[ LockManagerA_SimpleLocking ] BEGIN
[ LockManagerA_SimpleLocking ] PASS
[ LockManagerA_LocksReleasedOutOfOrder ] BEGIN
[ LockManagerA_LocksReleasedOutOfOrder ] PASS
[ LockManagerB_SimpleLocking ] BEGIN
[ LockManagerB_SimpleLocking ] PASS
[ LockManagerB_LocksReleasedOutOfOrder ] BEGIN
[ LockManagerB_LocksReleasedOutOfOrder ] PASS
== bin/txn/txn_processor_test ==
                            Average Transaction Duration
                0.1ms           1ms             10ms
'Low contention' Read only (5 records)
 Serial         9188.06         985.436         99.6883
 Locking A      43086.7         5220.43         477.038
 Locking B      38697           5251.2          496.983
 OCC            45336.1         5167.01         479.541
 MVCC           45120.6         4824.12         496.008
'Low contention' Read only (30 records) 
 Serial         6154.72         945.48          98.4695
 Locking A      9778.43         4849.31         485.863
 Locking B      13633.4         4958.22         517.23
 OCC            26883           4991.09         443.269
 MVCC           24428           4240.4          483.664
'High contention' Read only (5 records)
 Serial         9363.58         989.528         99.4351
 Locking A      30337.8         4267.42         453.62
 Locking B      46097.8         5019.87         490.453
 OCC            42374.6         4779.58         460.717
 MVCC           44229.3         4462.91         443.583
'High contention' Read only (30 records)
 Serial         6508.52         948.485         99.4382
 Locking A      3909.75         861.85          98.077
 Locking B      15888.2         4625.73         428.322
 OCC            32613.8         4827.53         474.479
 MVCC           30844           4816.5          504.654
Low contention read-write (5 records)
 Serial         8741.27         983.806         99.7174
 Locking A      35546.4         5301.16         500.447
 Locking B      40061.7         5111.55         461.854
 OCC            43477.6         4843.28         534.276
 MVCC           38823.2         4462.3          480.767
Low contention read-write (10 records)
 Serial         7348.25         947.931         99.5401
 Locking A      27769.5         5243.53         482.999
 Locking B      29597.1         3504.61         537.946
 OCC            27188.6         5033.59         459.137
 MVCC           17437.6         4448.83         484.19
High contention read-write (5 records)
 Serial         8337.52         973.355         99.4676
 Locking A      17861.6         4051.94         455.34
 Locking B      19409.3         4002.81         450.899
 OCC            21023.9         2436.47         247.589
 MVCC           5911.97         1205.61         132.054
High contention read-write (10 records)
 Serial         6864.54         936.067         98.9354
 Locking A      7472.84         2315.41         289.306
 Locking B      7428.64         2356.97         300.594
 OCC            9864.5          1322.73         123.987
 MVCC           1900.83         656.261         77.2327
High contention mixed read only/read-write 
 Serial         6847.91         1186.39         125.277
 Locking A      3752.59         1020.72         119.049
 Locking B      10824.4         4012.77         630.821
 OCC            19105.5         2797.9          275.347
 MVCC           4673.29         4053.47         589.732
```
----------
# Oleh Kelompok 2 K1
----------

| NO | NAMA | NIM |
--- | --- | --- |
| 1 | Louis Caesa Kesuma | 13521069 |
| 2 | William Nixon | 13521123 |
| 3 | Jeremya Dharmawan Raharjo | 13521131 |
| 4 | Nathania Calista Djunaedi | 13521139 |

