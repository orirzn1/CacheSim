#!/bin/bash

echo Running tests...
echo The test succeed if there are no diffs printed.
echo
all=0
fail=0
for filename in tests/test*.command; do
    test_num=`echo $filename | cut -d'.' -f1`
     bash ${filename} > ${test_num}.YoursOut
done

for filename in tests/test*.out; do
	(( all++ ))
    test_num=`echo $filename | cut -d'.' -f1`
    diff_result=$(diff ${test_num}.out ${test_num}.YoursOut)
    if [ "$diff_result" != "" ]; then
        echo The test ${test_num} didnt pass
	(( fail++ ))
    fi
done

echo
echo "faild $fail out of $all "
echo Ran all tests.
