build/frontend/tpcc --target_gib=2 --ssd_path=/home/ubuntu/data/test.txt --worker_threads=120 --pp_threads=4 --dram_gib=2 --csv_path=./log --free_pct=1 --contention_split --xmerge --print_tx_console --run_for_seconds=60 --isolation_level=si 