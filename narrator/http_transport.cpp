#include "narrator/narrator.hpp"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <httplib.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <atomic>

namespace rpg::narrator {

namespace {

class HttpTransport final : public Transport {
public:
    HttpTransport(const std::string& url, std::chrono::milliseconds timeout) : client_(url) {
        client_.set_connection_timeout(std::chrono::seconds(3));
        client_.set_read_timeout(timeout);
        client_.set_write_timeout(std::chrono::seconds(10));
        client_.set_keep_alive(true);
    }

    std::optional<std::string> post(const std::string& body) override {
        if (cancelled_) return std::nullopt;
        const auto res = client_.Post("/v1/chat/completions", body, "application/json");
        if (!res || res->status != 200) return std::nullopt;
        return res->body;
    }

    void cancel() override {
        cancelled_ = true;
        client_.stop();
    }

private:
    httplib::Client client_;
    std::atomic<bool> cancelled_{false};
};

}  // namespace

std::unique_ptr<Transport> make_http_transport(const std::string& url, std::chrono::milliseconds timeout) {
    return std::make_unique<HttpTransport>(url, timeout);
}

}  // namespace rpg::narrator
