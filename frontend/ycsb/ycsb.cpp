#include "../shared/LeanStoreAdapter.hpp"
#include "Schema.hpp"
#include "Units.hpp"
#include "leanstore/Config.hpp"
#include "leanstore/LeanStore.hpp"
#include "leanstore/profiling/counters/WorkerCounters.hpp"
#include "leanstore/utils/FVector.hpp"
#include "leanstore/utils/Files.hpp"
#include "leanstore/utils/Parallelize.hpp"
#include "leanstore/utils/RandomGenerator.hpp"
#include "leanstore/utils/ScrambledZipfGenerator.hpp"
// -------------------------------------------------------------------------------------
#include <gflags/gflags.h>
#include <tbb/parallel_for.h>
#include <tbb/task_scheduler_init.h>
// -------------------------------------------------------------------------------------
#include <iostream>
#include <set>
// -------------------------------------------------------------------------------------
DEFINE_uint32(ycsb_read_ratio, 100, "");
DEFINE_uint64(ycsb_tuple_count, 0, "");
DEFINE_uint32(ycsb_payload_size, 100, "tuple size in bytes");
DEFINE_uint32(ycsb_warmup_rounds, 0, "");
DEFINE_uint32(ycsb_tx_rounds, 1, "");
DEFINE_uint32(ycsb_tx_count, 0, "default = tuples");
DEFINE_bool(verify, false, "");
DEFINE_bool(ycsb_scan, false, "");
DEFINE_bool(ycsb_single_statement_tx, true, "");
DEFINE_bool(ycsb_count_unique_lookup_keys, true, "");
// -------------------------------------------------------------------------------------
using namespace leanstore;
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
using YCSBKey = u64;
using YCSBPayload = BytesPayload<120>;
using tabular = Relation<YCSBKey, YCSBPayload>;
// -------------------------------------------------------------------------------------
double calculateMTPS(chrono::high_resolution_clock::time_point begin, chrono::high_resolution_clock::time_point end, u64 factor)
{
   double tps = ((factor * 1.0 / (chrono::duration_cast<chrono::microseconds>(end - begin).count() / 1000000.0)));
   return (tps / 1000000.0);
}
// -------------------------------------------------------------------------------------
int main(int argc, char** argv)
{
   gflags::SetUsageMessage("Leanstore Frontend");
   gflags::ParseCommandLineFlags(&argc, &argv, true);
   // -------------------------------------------------------------------------------------
   tbb::task_scheduler_init taskScheduler(FLAGS_worker_threads);
   // -------------------------------------------------------------------------------------
   chrono::high_resolution_clock::time_point begin, end;
   // -------------------------------------------------------------------------------------
   // LeanStore DB
   LeanStore db;
   auto& crm = db.getCRManager();
   LeanStoreAdapter<tabular> table;
   crm.scheduleJobSync(0, [&]() { table = LeanStoreAdapter<tabular>(db, "YCSB_adapter"); });
   db.registerConfigEntry("ycsb_read_ratio", FLAGS_ycsb_read_ratio);
   db.registerConfigEntry("ycsb_target_gib", FLAGS_target_gib);
   db.startProfilingThread();
   // -------------------------------------------------------------------------------------
   const u64 ycsb_tuple_count = (FLAGS_ycsb_tuple_count)
                                    ? FLAGS_ycsb_tuple_count
                                    : FLAGS_target_gib * 1024 * 1024 * 1024 * 1.0 / 2.0 / (sizeof(YCSBKey) + sizeof(YCSBPayload));
   // Insert values
   {
      const u64 n = ycsb_tuple_count;
      cout << "-------------------------------------------------------------------------------------" << endl;
      cout << "Inserting values" << endl;
      begin = chrono::high_resolution_clock::now();
      utils::Parallelize::range(FLAGS_worker_threads, n, [&](u64 t_i, u64 begin, u64 end) {
         crm.scheduleJobAsync(t_i, [&, begin, end]() {
            cr::Worker::TX_TYPE tx_type = FLAGS_ycsb_single_statement_tx ? cr::Worker::TX_TYPE::SINGLE_UPSERT : cr::Worker::TX_TYPE::LONG_TX;
            cr::Worker::my().refreshSnapshot();
            for (u64 i = begin; i < end; i++) {
               YCSBPayload payload;
               utils::RandomGenerator::getRandString(reinterpret_cast<u8*>(&payload), sizeof(YCSBPayload));
               YCSBKey& key = i;
               cr::Worker::my().startTX(tx_type);
               table.insert({key}, {payload});
               cr::Worker::my().commitTX();
            }
         });
      });
      crm.joinAll();
      end = chrono::high_resolution_clock::now();
      cout << "time elapsed = " << (chrono::duration_cast<chrono::microseconds>(end - begin).count() / 1000000.0) << endl;
      cout << calculateMTPS(begin, end, n) << " M tps" << endl;
      // -------------------------------------------------------------------------------------
      const u64 written_pages = db.getBufferManager().consumedPages();
      const u64 mib = written_pages * PAGE_SIZE / 1024 / 1024;
      cout << "Inserted volume: (pages, MiB) = (" << written_pages << ", " << mib << ")" << endl;
      cout << "-------------------------------------------------------------------------------------" << endl;
   }
   // -------------------------------------------------------------------------------------
   auto zipf_random = std::make_unique<utils::ScrambledZipfGenerator>(0, ycsb_tuple_count, FLAGS_zipf_factor);
   cout << setprecision(4);
   // -------------------------------------------------------------------------------------
   // Scan
   if (FLAGS_ycsb_scan) {
      const u64 n = ycsb_tuple_count;
      cout << "-------------------------------------------------------------------------------------" << endl;
      cout << "Scan" << endl;
      {
         begin = chrono::high_resolution_clock::now();
         utils::Parallelize::range(FLAGS_worker_threads, n, [&](u64 t_i, u64 begin, u64 end) {
            crm.scheduleJobAsync(t_i, [&, begin, end]() {
               for (u64 i = begin; i < end; i++) {
                  YCSBPayload result;
                  table.lookup1({i}, [&](const tabular& record) { result = record.my_payload; });
               }
            });
         });
         crm.joinAll();
         end = chrono::high_resolution_clock::now();
      }
      // -------------------------------------------------------------------------------------
      cout << "time elapsed = " << (chrono::duration_cast<chrono::microseconds>(end - begin).count() / 1000000.0) << endl;
      // -------------------------------------------------------------------------------------
      cout << calculateMTPS(begin, end, n) << " M tps" << endl;
      cout << "-------------------------------------------------------------------------------------" << endl;
   }
   // -------------------------------------------------------------------------------------
   cout << "-------------------------------------------------------------------------------------" << endl;
   cout << "~Transactions" << endl;
   atomic<bool> keep_running = true;
   atomic<u64> running_threads_counter = 0;
   for (u64 t_i = 0; t_i < FLAGS_worker_threads; t_i++) {
      crm.scheduleJobAsync(t_i, [&]() {
         running_threads_counter++;
         while (keep_running) {
            jumpmuTry()
            {
               YCSBKey key = zipf_random->rand();
               assert(key < ycsb_tuple_count);
               YCSBPayload result;
               if (FLAGS_ycsb_read_ratio == 100 || utils::RandomGenerator::getRandU64(0, 100) < FLAGS_ycsb_read_ratio) {
                  cr::Worker::TX_TYPE tx_type = FLAGS_ycsb_single_statement_tx ? cr::Worker::TX_TYPE::SINGLE_LOOKUP : cr::Worker::TX_TYPE::LONG_TX;
                  cr::Worker::my().startTX(tx_type);
                  table.lookup1({key}, [&](const tabular& record) { result = record.my_payload; });
                  cr::Worker::my().commitTX();
               } else {
                  cr::Worker::TX_TYPE tx_type = FLAGS_ycsb_single_statement_tx ? cr::Worker::TX_TYPE::SINGLE_UPSERT : cr::Worker::TX_TYPE::LONG_TX;
                  YCSBPayload payload;
                  UpdateDescriptorGenerator1(tabular_update_descriptor, tabular, my_payload);
                  utils::RandomGenerator::getRandString(reinterpret_cast<u8*>(&payload), sizeof(YCSBPayload));
                  // -------------------------------------------------------------------------------------
                  cr::Worker::my().startTX(tx_type);
                  table.update1(
                      {key}, [&](tabular& rec) { rec.my_payload = payload; }, tabular_update_descriptor);
                  cr::Worker::my().commitTX();
               }
               WorkerCounters::myCounters().tx++;
            }
            jumpmuCatch() { WorkerCounters::myCounters().tx_abort++; }
         }
         running_threads_counter--;
      });
   }
   {
      // Shutdown threads
      sleep(FLAGS_run_for_seconds);
      keep_running = false;
      while (running_threads_counter) {
         MYPAUSE();
      }
      crm.joinAll();
   }
   cout << "-------------------------------------------------------------------------------------" << endl;
   return 0;
}
