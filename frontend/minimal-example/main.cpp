#include "../shared/LeanStoreAdapter.hpp"
#include "../shared/Schema.hpp"
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
// -------------------------------------------------------------------------------------
#include <iostream>
#include <set>
// -------------------------------------------------------------------------------------
DEFINE_uint32(user_run_id, 0, "A runtime flag to use in your benchmark/driver");
// -------------------------------------------------------------------------------------
using namespace leanstore;
// -------------------------------------------------------------------------------------
using Key = u64;
using Payload = BytesPayload<8>;
using KVTable = Relation<Key, Payload>;
// ------------------------------------------------------------------------------------
int main(int argc, char** argv)
{
   gflags::SetUsageMessage("LeanStore KV minimal example");
   gflags::ParseCommandLineFlags(&argc, &argv, true);
   // -------------------------------------------------------------------------------------
   // Always init with the maximum number of threads (FLAGS_worker_threads)
   // 调用默认构造函数LeanStore::LeanStore()
   LeanStore db;
   auto& crm = db.getCRManager(); // Any transaction must be scheduled on one of worker threads.
   
   LeanStoreAdapter<KVTable> table;
   // Create table
   crm.scheduleJobSync(0, [&]() { table = LeanStoreAdapter<KVTable>(db, "ExampleTable"); });
    // This will be printed in the CSV and can help you identify the run when you analyze the results
   // db.registerConfigEntry("user_run_id", FLAGS_user_run_id);
   db.registerConfigEntry("user_run_id", 1009);
   // -------------------------------------------------------------------------------------
   // Cf. --help isolation_level for the remaining isolation levels
   // leanstore::TX_ISOLATION_LEVEL isolation_level = leanstore::parseIsolationLevel(FLAGS_isolation_level);
   leanstore::TX_ISOLATION_LEVEL isolation_level = leanstore::TX_ISOLATION_LEVEL::SNAPSHOT_ISOLATION;
   // -------------------------------------------------------------------------------------
   // db.startProfilingThread();
   cout << "\n-------------------------------------------------------------------------------------" << endl;
   for(u64 t_i = 0; t_i < 1; t_i++) {
      crm.scheduleJobSync(t_i, [&, t_i]() { // Usually we want async for parallelism
         // Key key = t_i;
         Key key = 2024;
         Payload payload;
         utils::RandomGenerator::getRandString(reinterpret_cast<u8*>(&payload), sizeof(Payload));
         // 随机生成一个字符串用作value，key就是编号
         cr::Worker::my().startTX(TX_MODE::OLTP, isolation_level);
         table.insert({key}, {payload});
         cr::Worker::my().commitTX();
         cout << "Inserted: (" << key << ", " << payload << ")" << endl;
         /*-------------------插入成功----------------------------------------*/
         cr::Worker::my().startTX(TX_MODE::OLTP, isolation_level);
         table.lookup1({key}, [&](const KVTable& res) {
            cout << "Lookup(" << key << "): returned "<< res.my_payload << endl;
         });
         cr::Worker::my().commitTX();
         /*-------------------查找成功----------------------------------------*/
         // If we know that is read-only, then we can set TX_MODE::OLTP
         cout << "Scanning table..." << endl;
         cr::Worker::my().startTX(TX_MODE::OLTP, isolation_level);
         table.scan(
             {0},
             [&](const KVTable::Key& key, const KVTable& res) {
                cout << "(" << key.my_key << ", " << res.my_payload << ")" << endl;
                return true;
             }, [&]() {});
         cr::Worker::my().commitTX();
         /*-------------------扫描成功----------------------------------------*/
         // Check LeanStoreAdapter.hpp for the remaining commands (erase, update1)
      });
   }
   crm.joinAll(); // Not really needed here because of Sync.
   cout << "\n-------------------------------------------------------------------------------------" << endl;
   return 0;
}
