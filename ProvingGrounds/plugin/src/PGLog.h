#pragma once
#include <string>
#include <Debug.h> // SDK error-formatting helpers remain available.
namespace PGLog {
    void Debug(const std::string& message);
    void Error(const std::string& message);
    // Ambient NPC bouts, their markets, and the controlled diagnostic run.
    void Combat(const std::string& fields);
    // Player-vs-NPC challenges, in their own file so a balance read of one
    // population is never diluted by the other. Both channels share the process
    // session id and the monotonic record counter, so the two files can be
    // interleaved on (session, record) if a question spans them.
    void Challenge(const std::string& fields);
    std::string Quote(const std::string& value);
    unsigned NextId();
    // The plugin module's own directory, with a trailing separator. The logs and
    // the price file are both resolved from it, so "beside ProvingGrounds.dll"
    // means exactly one thing in this codebase. Empty only if the module path
    // could not be read, which is also when nothing can be written.
    const std::wstring& Directory();
}
