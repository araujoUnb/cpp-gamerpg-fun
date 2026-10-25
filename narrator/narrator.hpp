#pragma once

#include "narrator/config.hpp"
#include "narrator/narration.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace rpg::narrator {

// Sends one request body and returns the reply body, or nullopt on any failure.
class Transport {
public:
    virtual ~Transport() = default;
    virtual std::optional<std::string> post(const std::string& body) = 0;
    // Called from another thread: makes a post in progress, and every later one, return nullopt soon.
    virtual void cancel() {}
};

// llama-server over plain HTTP on the local network, e.g. "http://127.0.0.1:8080".
std::unique_ptr<Transport> make_http_transport(const std::string& url, std::chrono::milliseconds timeout);

// The narrator asks the model in the background, one request at a time, so
// the game never waits for it. Only the narration of the latest situation is
// handed out: one that arrives after a newer request is dropped, and the game
// keeps its default text for that moment.
class Narrator {
public:
    Narrator(Config cfg, std::unique_ptr<Transport> transport, Warnings warnings);
    ~Narrator();
    Narrator(const Narrator&) = delete;
    Narrator& operator=(const Narrator&) = delete;

    // Asks for the narration of `situation`. The previous narrations and the
    // warnings about dropped actions are added before sending.
    void request(nlohmann::json situation);

    // The narration of the latest request, if it has arrived. Waits at most
    // `wait`, and less when that request fails (server down, bad reply).
    std::optional<Narration> receive(std::chrono::milliseconds wait = std::chrono::milliseconds(0));

    const Config& config() const { return cfg_; }

private:
    void run();

    Config cfg_;
    std::unique_ptr<Transport> transport_;
    Warnings warning_texts_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<nlohmann::json> queued_;  // waiting to be sent; a newer request replaces it
    std::uint64_t generation_ = 0;          // of the latest request
    std::uint64_t finished_ = 0;            // of the last request answered or failed
    std::optional<Narration> ready_;
    std::deque<std::string> history_;  // narrations handed out, newest last
    std::vector<std::string> warnings_;
    bool stop_ = false;
    std::thread worker_;
};

}  // namespace rpg::narrator
