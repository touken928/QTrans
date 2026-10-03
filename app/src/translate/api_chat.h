#pragma once

#include "qtrans/runtime.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class InferenceService;

struct ApiChatRequest {
    std::string model_id;
    std::vector<qtrans::core::Message> messages;
    std::optional<float> temperature;
    std::optional<float> top_p;
    std::optional<std::uint32_t> seed;
    std::optional<std::uint32_t> max_output_tokens;
};

struct ApiChatReply {
    bool accepted = false;
    qtrans::core::Failure failure;
    qtrans::core::InvocationResult result;
};

using ApiChatCallback = std::function<void(const ApiChatReply &)>;

// Concrete adapter between the local OpenAI-compatible HTTP server and the
// InferenceService. Lives on the service (worker) thread; its methods are
// thread-safe and hop to that thread exactly like the former InferenceService
// API-chat methods.
class ApiChatBridge {
public:
    bool modelSnapshot(std::string *loaded_model_id = nullptr,
                       bool *supports_conversation = nullptr) const;
    std::uint64_t submit(const ApiChatRequest &request, ApiChatCallback callback);
    bool cancel(std::uint64_t request_id);

private:
    friend class InferenceService;
    explicit ApiChatBridge(InferenceService *service);
    InferenceService *service_ = nullptr;
};
