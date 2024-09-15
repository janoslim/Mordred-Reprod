SF=$1

# Make sure encoder is using right scale factor
bin=./minmax_$1.bin
binsort=./minmaxsort_$1.bin

LO_LEN=$2
P_LEN=$3
S_LEN=$4
C_LEN=$5
D_LEN=$6


arr=("lo_custkey" "lo_partkey" "lo_suppkey" "lo_orderdate" "lo_quantity" "lo_extendedprice" "lo_discount" "lo_revenue" "lo_supplycost" "lo_orderkey" "lo_linenumber" "lo_tax" "lo_ordtotalprice" "lo_commitdate")
for val in ${arr[*]}; do
 echo $val
 $binsort $val $LO_LEN $SF
done

arr=("p_partkey" "p_mfgr" "p_category" "p_brand1")
for val in ${arr[*]}; do
 echo $val
 $bin $val $P_LEN $SF
done

arr=("c_custkey" "c_region" "c_nation" "c_city")
for val in ${arr[*]}; do
 echo $val
 $bin $val $C_LEN $SF
done

arr=("s_suppkey" "s_region" "s_nation" "s_city")
for val in ${arr[*]}; do
 echo $val
 $bin $val $S_LEN $SF
done

arr=("d_datekey" "d_year" "d_yearmonthnum")
for val in ${arr[*]}; do
 echo $val
 $bin $val $D_LEN $SF
done
