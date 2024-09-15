for sf in 20; #40 60 80 100 120 140 160 180 200;
do
    make clean;
    make setup;
    make bin/gpudb/main.bin SF=${sf} -j;
    mv bin/gpudb/main.bin main${sf}.bin;
done

# for sf in 20 40 60 80 100 120 140 160 180 200;
# do
#     make clean;
#     make setup;
#     make bin/gpudb/minmax SF=${sf} -j;
#     make bin/gpudb/minmaxsort SF=${sf} -j;
#     mv bin/gpudb/minmax minmax_${sf}.bin;
#     mv bin/gpudb/minmaxsort minmaxsort_${sf}.bin;
# done