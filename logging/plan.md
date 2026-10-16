# Friday, October 16, 6pm 

LogManager, and the accompanying LogRecordHeader are both completed. 
LogRecordHeader currently has two enums over the LogType and LogOperation. LogOperation is only relevant to UPDATE logs, which update records. Otherwise it has standard ARIES information.
    The before image of a checkpoint end log is the ATT, whereas the after image is the DPT. 
LogManager currently batches flushes with a leader that syncs the entire append buffer to the fill buffer, with all other threads waiting on the lock being dropped. Upon wakeup, they check if their targetLSN has been satisfied before they try to acquire the lock. 

The next 2 things that must happen are 
1. The B+ Tree, which is the only class capable of modifying logical rows, must log all of its operations correctly. 
2. The Buffer Pool Manager, which is the class responsible for moving pages out of system memory, to ensure that if a dirty page is to move out of system memory into secondary storage, its most recently updated LSN must also be moved out of system memory into secondary storage beforehand. ARIES relies on this 1 singular invariant. 

Other things that will come up later: 
1. TransactionManager, which must log transactions properly. I believe it sits between the B+ Tree and any layer that requires atomicity, which I believe will probably be the query exeuctor... 
2. RecoveryManager, which initially must perform Analysis, Redo, and Undo for all non-SMO operations. This will be quite difficult and have to be tested thoroughly. 
3. Extend RecoveryManager to SMOs. 

I'm sure I've missed a few things, but for now, this will suffice. I will re inspect my assumptions after I complete the specified modifications to the BPM and B+ Tree. 

