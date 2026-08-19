#pragma once

#include "kv_engine.h"
#include <string>
#include <vector>
#include <memory>

namespace kv_engine {

class CLI {
public:
    explicit CLI(KVEngine& engine);
    ~CLI() = default;

    // Run the interactive CLI loop
    void Run();

    // Execute a single command (for testing/scripting)
    Status ExecuteCommand(const std::string& command_line);

private:
    KVEngine& engine_;

    // Command handlers
    Status HandlePut(const std::vector<std::string>& args);
    Status HandleGet(const std::vector<std::string>& args);
    Status HandleDelete(const std::vector<std::string>& args);
    Status HandleScan(const std::vector<std::string>& args);
    Status HandleSize();
    Status HandlePrint();
    Status HandleStats();
    Status HandleHelp();
    Status HandleVerify();

    // Parsing utilities
    std::vector<std::string> ParseCommand(const std::string& command_line);
    void PrintPrompt() const;
    void PrintHelp() const;
    void PrintError(const Status& status) const;
    void PrintSuccess(const std::string& message) const;
};

} // namespace kv_engine