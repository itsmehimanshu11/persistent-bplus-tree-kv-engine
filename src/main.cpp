#include "kv_engine.h"
#include "cli/cli.h"
#include <iostream>

int main() {
    kv_engine::KVEngineOptions options;
    options.enable_wal = true;
    options.wal_options.wal_dir = "./wal_data";
    options.wal_options.sync_on_write = false;

    kv_engine::KVEngine engine(options);
    std::cout << "KV Engine v1.0.0\n";
    std::cout << "Persistent B+ Tree Key-Value Store\n";
    std::cout << "Type 'help' for commands.\n\n";

    kv_engine::CLI cli(engine);
    cli.Run();
    return 0;
}
