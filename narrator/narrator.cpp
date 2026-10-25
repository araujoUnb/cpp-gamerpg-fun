#include "narrator/narrator.hpp"

#include <utility>

namespace rpg::narrator {

using nlohmann::json;

Narrator::Narrator(Config cfg, std::unique_ptr<Transport> transport, Warnings warnings)
    : cfg_(std::move(cfg)), transport_(std::move(transport)), warning_texts_(std::move(warnings)) {
    worker_ = std::thread([this] { run(); });
}

Narrator::~Narrator() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    transport_->cancel();
    worker_.join();
}

void Narrator::request(json situation) {
    {
        std::lock_guard lock(mutex_);
        if (!history_.empty()) situation["narracoes_anteriores"] = json(history_);
        if (!warnings_.empty()) {
            situation["avisos"] = warnings_;
            warnings_.clear();
        }
        queued_ = std::move(situation);
        ++generation_;
        ready_.reset();
    }
    cv_.notify_all();
}

std::optional<Narration> Narrator::receive(std::chrono::milliseconds wait) {
    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, wait, [this] { return ready_.has_value() || finished_ == generation_; });
    std::optional<Narration> out;
    out.swap(ready_);
    if (out) {
        // Only what the player saw is part of the story the model is told about.
        history_.push_back(out->text);
        while (history_.size() > static_cast<std::size_t>(cfg_.history)) history_.pop_front();
    }
    return out;
}

void Narrator::run() {
    for (;;) {
        json situation;
        std::uint64_t generation = 0;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || queued_.has_value(); });
            if (stop_) return;
            situation = std::move(*queued_);
            queued_.reset();
            generation = generation_;
        }

        const auto reply = transport_->post(request_body(cfg_, situation).dump(-1, ' ', false, json::error_handler_t::replace));
        auto narration = reply ? parse_reply(cfg_, situation, *reply, warning_texts_) : std::nullopt;

        {
            std::lock_guard lock(mutex_);
            if (stop_) return;
            finished_ = generation;
            if (narration) {
                warnings_.insert(warnings_.end(), narration->warnings.begin(), narration->warnings.end());
                if (generation == generation_) ready_ = std::move(narration);
            }
        }
        cv_.notify_all();
    }
}

}  // namespace rpg::narrator
