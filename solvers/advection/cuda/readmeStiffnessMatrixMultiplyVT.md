#!/bin/bash

rm stiffnessMatrixMultiplyVT
make stiffnessMatrixMultiplyVT

# to run a sweep
for Nq in `seq 4 2 12`
do

    #  let cubNq=$(($Nq + 2))
  let cubNq=$(($Nq + 2))
  echo $cubNq

  let Np=$Nq*$Nq*$Nq
  
  let maxE=2000000/$Np

  let tmpE=$maxE/400
  let tmpE2=(19+$tmpE)/20
  let skipE=$tmpE2*20
  
  echo $maxE
  echo $skipE

  ./stiffnessMatrixMultiplyVT $Nq $cubNq 1 
  
  for E in `seq 80 $skipE $maxE`
  do
    ./stiffnessMatrixMultiplyVT $Nq $cubNq $E
  done
done
      
