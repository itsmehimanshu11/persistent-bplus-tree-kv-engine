#include "cli/cli.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace kv_engine {

CLI::CLI(KVEngine& engine) : engine_(engine) {}

void CLI::Run() {
    std::cout << "KV Engine Interactive CLI v1.0.0\n";
    std::cout << "Type 'help' for available commands, 'exit' to quit\n\n";

    std::string line;
    while (true) {
        PrintPrompt();
        if (!std::getline(std::cin, line)) {
            break; // EOF
        }

        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty()) continue;

        if (line == "exit" || line == "quit") {
            std::cout << "Goodbye!\n";
            break;
        }

        Status s = ExecuteCommand(line);
        if (!s.ok()) {
            PrintError(s);
        }
    }
}

Status CLI::ExecuteCommand(const std::string& command_line) {
    auto args = ParseCommand(command_line);
    if (args.empty()) return Status::OK();

    std::string cmd = args[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
    args.erase(args.begin());

    if (cmd == "PUT") return HandlePut(args);
    if (cmd == "GET") return HandleGet(args);
    if (cmd == "DELETE" || cmd == "DEL") return HandleDelete(args);
    if (cmd == "SCAN") return HandleScan(args);
    if (cmd == "SIZE") return HandleSize();
    if (cmd == "PRINT") return HandlePrint();
    if (cmd == "STATS") return HandleStats();
    if (cmd == "HELP") return HandleHelp();
    if (cmd == "VERIFY") return HandleVerify();
    if (cmd == "WALSTATS") { return HandleStats(); }
    if (cmd == "FLUSH") { Status s = engine_.Flush(); if (s.ok()) PrintSuccess("OK"); return s; }
    if (cmd == "SYNC") { Status s = engine_.Sync(); if (s.ok()) PrintSuccess("OK"); return s; }

    return Status::InvalidArgument("Unknown command: " + cmd + ". Type 'help' for available commands.");
}

std::vector<std::string> CLI::ParseCommand(const std::string& command_line) {
    std::vector<std::string> args;
    std::string current;
    bool in_quotes = false;
    char quote_char = 0;

    for (size_t i = 0; i < command_line.size(); ++i) {
        char c = command_line[i];

        if ((c == '"' || c == '\'') && (i == 0 || command_line[i - 1] != '\\')) {
            if (!in_quotes) {
                in_quotes = true;
                quote_char = c;
            } else if (c == quote_char) {
                in_quotes = false;
                quote_char = 0;
            } else {
                current += c;
            }
        } else if (std::isspace(c) && !in_quotes) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }

    if (!current.empty()) {
        args.push_back(current);
    }

    return args;
}

Status CLI::HandlePut(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return Status::InvalidArgument("Usage: PUT <key> <value>");
    }

    std::string key = args[0];
    std::string value = args[1];

    // Join remaining args as value (in case value has spaces but wasn't quoted)
    for (size_t i = 2; i < args.size(); ++i) {
        value += " " + args[i];
    }

    Status s = engine_.Put(key, value);
    if (s.ok()) {
        PrintSuccess("OK");
    }
    return s;
}

Status CLI::HandleGet(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return Status::InvalidArgument("Usage: GET <key>");
    }

    std::string value;
    Status s = engine_.Get(args[0], &value);
    if (s.ok()) {
        std::cout << value << "\n";
    }
    return s;
}

Status CLI::HandleDelete(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return Status::InvalidArgument("Usage: DELETE <key>");
    }

    Status s = engine_.Delete(args[0]);
    if (s.ok()) {
        PrintSuccess("OK");
    }
    return s;
}

Status CLI::HandleScan(const std::vector<std::string>& args) {
    Slice start, limit;
    bool has_start = false, has_limit = false;

    if (args.size() >= 1) {
        start = Slice(args[0]);
        has_start = true;
    }
    if (args.size() >= 2) {
        limit = Slice(args[1]);
        has_limit = true;
    }

    auto* iter = has_limit ? engine_.NewIterator(start, limit) :
                         (has_start ? engine_.NewIterator(start) : engine_.NewIterator());

    size_t count = 0;
    iter->SeekToFirst();
    while (iter->Valid()) {
        std::cout << iter->key().ToString() << " = " << iter->value().ToString() << "\n";
        iter->Next();
        count++;
    }

    if (count == 0) {
        std::cout << "(empty)\n";
    } else {
        std::cout << "--- " << count << " key(s) ---\n";
    }

    delete iter;
    return Status::OK();
}

Status CLI::HandleSize() {
    std::cout << "Keys: " << engine_.Size() << "\n";
    return Status::OK();
}

Status CLI::HandlePrint() {
    engine_.Print();
    return Status::OK();
}

Status CLI::HandleStats() {
    auto wal_stats = engine_.GetWalStats();
    std::cout << "=== Engine Stats ===\n";
    std::cout << "Keys: " << engine_.Size() << "\n";
    std::cout << "Nodes: " << engine_.NodeCount() << "\n";
    std::cout << "Height: " << engine_.Height() << "\n";
    std::cout << "Memory: " << engine_.MemoryUsage() << " bytes\n";
    std::cout << "=== WAL Stats ===\n";
    std::cout << "Files: " << wal_stats.current_file_number << "\n";
    std::cout << "Current file size: " << wal_stats.current_file_size << " bytes\n";
    std::cout << "Total writes: " << wal_stats.total_writes << "\n";
    std::cout << "Total bytes: " << wal_stats.total_bytes_written << "\n";
    std::cout << "Total syncs: " << wal_stats.total_syncs << "\n";
    return Status::OK();
}

Status CLI::HandleVerify() {
    bool ok = engine_.Verify();
    if (ok) {
        PrintSuccess("Tree verification passed");
    } else {
        std::cout << "Tree verification FAILED\n";
    }
    return Status::OK();
}

Status CLI::HandleHelp() {
    PrintHelp();
    return Status::OK();
}

void CLI::PrintPrompt() const {
    std::cout << "kv> ";
    std::cout.flush();
}

void CLI::PrintHelp() const {
    std::cout << "Available commands:\n";
    std::cout << "  PUT <key> <value>           - Insert or update a key-value pair\n";
    std::cout << "  GET <key>                   - Retrieve value for key\n";
    std::cout << "  DELETE <key>                - Delete a key\n";
    std::cout << "  SCAN [start] [limit]        - Range scan (optional start/limit keys)\n";
    std::cout << "  SIZE                        - Show number of keys\n";
    std::cout << "  PRINT                       - Print tree structure\n";
    std::cout << "  STATS                       - Show engine and WAL statistics\n";
    std::cout << "  VERIFY                      - Verify tree integrity\n";
    std::cout << "  WALSTATS                    - Show WAL statistics\n";
    std::cout << "  FLUSH                       - Flush buffered WAL data\n";
    std::cout << "  SYNC                        - Flush and synchronize WAL\n";
    std::cout << "  HELP                        - Show this help\n";
    std::cout << "  EXIT / QUIT                 - Exit the CLI\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  PUT name \"John Doe\"\n";
    std::cout << "  GET name\n";
    std::cout << "  SCAN a z\n";
    std::cout << "  DELETE name\n";
}

void CLI::PrintError(const Status& status) const {
    std::cout << "Error: " << status.ToString() << "\n";
}

void CLI::PrintSuccess(const std::string& message) const {
    std::cout << message << "\n";
}

} // namespace kv_engine