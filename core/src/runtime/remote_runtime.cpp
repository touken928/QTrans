#include "remote_runtime.h"

#include <curl/curl.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace qtrans::core {

namespace {

// ── JSON helpers ──────────────────────────────────────────────────────────

std::string json_escape(const std::string &s) {
    std::ostringstream out;
    for (char c : s) {
        switch (c) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << c;
                break;
        }
    }
    return out.str();
}

std::string extract_json_string(const std::string &json, const std::string &key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos + search.size());
    if (pos == std::string::npos) return {};
    pos++;
    std::string result;
    while (pos < json.size()) {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            char next = json[pos + 1];
            switch (next) {
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                default:
                    result += next;
                    break;
            }
            pos += 2;
        } else if (json[pos] == '"') {
            break;
        } else {
            result += json[pos];
            pos++;
        }
    }
    return result;
}

std::string parse_openai_response(const std::string &body) {
    auto content = extract_json_string(body, "content");
    if (content.empty()) {
        auto logger = spdlog::get("inference");
        if (logger) logger->error("openai response parse failed: {}", body);
        throw std::runtime_error("failed to parse OpenAI response");
    }
    return content;
}

std::string parse_anthropic_response(const std::string &body) {
    auto text = extract_json_string(body, "text");
    if (text.empty()) {
        auto logger = spdlog::get("inference");
        if (logger) logger->error("anthropic response parse failed: {}", body);
        throw std::runtime_error("failed to parse Anthropic response");
    }
    return text;
}

std::string build_openai_body(const std::string &model,
                              const std::string &user_prompt,
                              const TranslatorOptions &config) {
    std::ostringstream ss;
    ss << "{"
       << "\"model\":\"" << json_escape(model) << "\","
       << "\"messages\":[{\"role\":\"user\",\"content\":\"" << json_escape(user_prompt) << "\"}],"
       << "\"temperature\":" << config.generation.temperature << ","
       << "\"max_tokens\":" << config.context.max_tokens
       << "}";
    return ss.str();
}

std::string build_anthropic_body(const std::string &model,
                                 const std::string &user_prompt,
                                 const TranslatorOptions &config) {
    std::ostringstream ss;
    ss << "{"
       << "\"model\":\"" << json_escape(model) << "\","
       << "\"messages\":[{\"role\":\"user\",\"content\":\"" << json_escape(user_prompt) << "\"}],"
       << "\"max_tokens\":" << config.context.max_tokens
       << "}";
    return ss.str();
}

// ── Curl helpers ───────────────────────────────────────────────────────────

void ensure_curl_initialized() {
    static std::once_flag once;
    std::call_once(once, []() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *body = static_cast<std::string *>(userdata);
    body->append(ptr, size * nmemb);
    return size * nmemb;
}

struct CurlSlistGuard {
    struct curl_slist *list = nullptr;
    ~CurlSlistGuard() {
        if (list) curl_slist_free_all(list);
    }
};

std::string curl_post(const std::string &url,
                      const std::map<std::string, std::string> &headers,
                      const std::string &body) {
    ensure_curl_initialized();

    CURL *curl = curl_easy_init();
    if (curl == nullptr)
        throw std::runtime_error("curl_easy_init failed");

    CurlSlistGuard slist;
    for (const auto &h : headers)
        slist.list = curl_slist_append(slist.list, (h.first + ": " + h.second).c_str());

    std::string response_body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist.list);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        auto logger = spdlog::get("inference");
        if (logger) logger->error("curl error: {}", curl_easy_strerror(res));
        throw std::runtime_error(std::string("curl request failed: ") + curl_easy_strerror(res));
    }

    if (http_code < 200 || http_code >= 300) {
        auto logger = spdlog::get("inference");
        if (logger) logger->error("HTTP {}: {}", http_code, response_body);
        throw std::runtime_error("remote API returned HTTP " + std::to_string(http_code));
    }

    return response_body;
}

}  // namespace

// ── RemoteRuntime::Impl ────────────────────────────────────────────────────

struct RemoteRuntime::Impl {
    RemoteModelConfig remote_config;
    TranslatorOptions config;
    bool loaded_ = false;
};

RemoteRuntime::RemoteRuntime()
    : impl_(std::make_unique<Impl>()) {
}
RemoteRuntime::~RemoteRuntime() = default;
RemoteRuntime::RemoteRuntime(RemoteRuntime &&) noexcept = default;
RemoteRuntime &RemoteRuntime::operator=(RemoteRuntime &&) noexcept = default;

void RemoteRuntime::load(const ModelLoadSpec &model, const TranslatorOptions &config) {
    const auto *remote = std::get_if<RemoteModelConfig>(&model);
    if (remote == nullptr) {
        throw std::runtime_error("remote runtime requires remote model config");
    }

    impl_->remote_config = *remote;
    impl_->config = config;
    impl_->loaded_ = true;
}

void RemoteRuntime::unload() {
    impl_->loaded_ = false;
}

bool RemoteRuntime::is_loaded() const {
    return impl_->loaded_;
}

std::string RemoteRuntime::translate(
    const std::string &prompt,
    const std::function<void(const std::string &)> &on_token,
    const std::function<bool()> &should_cancel) {
    if (!impl_->loaded_)
        throw std::runtime_error("remote model is not configured");
    if (should_cancel && should_cancel())
        throw std::runtime_error("translation cancelled");

    const auto &remote = impl_->remote_config;

    std::string api_url;
    std::map<std::string, std::string> headers;
    std::string req_body;

    if (remote.api_provider == "anthropic") {
        std::string url = remote.endpoint_url;
        if (url.back() == '/') url.pop_back();
        api_url = url + "/messages";
        headers["x-api-key"] = remote.api_key;
        headers["anthropic-version"] = "2023-06-01";
        headers["Content-Type"] = "application/json";
        req_body = build_anthropic_body(remote.model_name, prompt, impl_->config);
    } else {
        std::string url = remote.endpoint_url;
        if (url.back() == '/') url.pop_back();
        api_url = url + "/chat/completions";
        headers["Authorization"] = "Bearer " + remote.api_key;
        headers["Content-Type"] = "application/json";
        req_body = build_openai_body(remote.model_name, prompt, impl_->config);
    }

    auto logger = spdlog::get("inference");
    if (logger) logger->trace("remote request: url={}", api_url);

    const std::string resp_body = curl_post(api_url, headers, req_body);

    std::string result;
    if (remote.api_provider == "anthropic")
        result = parse_anthropic_response(resp_body);
    else
        result = parse_openai_response(resp_body);

    if (on_token)
        on_token(result);
    return result;
}

int RemoteRuntime::count_prompt_tokens(const std::string &prompt) const {
    return std::max(1, static_cast<int>(prompt.size()) / 4);
}

std::string RemoteRuntime::backend_label() const {
    return impl_->remote_config.api_provider.empty()
               ? "Remote API"
               : impl_->remote_config.api_provider;
}

RuntimeKind RemoteRuntime::kind() const {
    return RuntimeKind::Remote;
}

RuntimeTraits RemoteRuntime::traits() const {
    RuntimeTraits t;
    t.kind = RuntimeKind::Remote;
    t.context_handling = ContextHandling::RuntimeManaged;
    t.streaming = StreamingSupport::FullResultCallback;
    t.has_precise_token_counting = false;
    t.max_input_tokens = 0;
    t.max_output_tokens = impl_->config.context.max_tokens;
    return t;
}

}  // namespace qtrans::core
