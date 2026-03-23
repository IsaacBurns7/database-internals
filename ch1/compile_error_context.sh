g++ $1 > error.out 2>&1 
echo "=====\nFILE THAT GENERATED ERROR: $1, BELOW\n=====\n" >> error.out
cat $1 >> error.out
