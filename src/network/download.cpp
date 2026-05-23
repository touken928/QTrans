#include "network/download.h"

#include <curl/curl.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char k_hf_scheme[] = "hf://";
constexpr const char k_ms_scheme[] = "ms://";
constexpr const char k_auto_scheme[] = "auto://";

struct DownloadContext {
    FILE * out = nullptr;
    curl_off_t last_reported = -1;
    curl_off_t last_dlnow = 0;
    std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
};

bool g_quiet = false;
std::mutex g_progress_mutex;
DownloadProgressCallback g_progress_callback;

void log_message(const char * fmt, ...) {
    if (g_quiet) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
}

void report_progress(DownloadContext * ctx, curl_off_t dltotal, curl_off_t dlnow) {
    DownloadProgress progress{};
    progress.total_bytes = static_cast<std::int64_t>(dltotal);
    progress.downloaded_bytes = static_cast<std::int64_t>(dlnow);

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - ctx->start_time).count();
    if (elapsed > 0.0) {
        progress.speed_bytes_per_sec = static_cast<double>(dlnow) / elapsed;
        if (progress.speed_bytes_per_sec > 0.0 && dltotal > 0) {
            const double remaining = static_cast<double>(dltotal - dlnow);
            progress.eta_seconds = remaining / progress.speed_bytes_per_sec;
        }
    }

    std::lock_guard<std::mutex> lock(g_progress_mutex);
    if (g_progress_callback) {
        g_progress_callback(progress);
    }
}

void ensure_curl_initialized() {
    static bool initialized = false;
    if (!initialized) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
            throw std::runtime_error("curl_global_init failed");
        }
        initialized = true;
    }
}

void ensure_parent_dir(const std::string & path) {
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

std::string url_encode_component(const std::string & value) {
    CURL * curl = curl_easy_init();
    if (curl == nullptr) {
        return value;
    }

    char * encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
    std::string result = encoded != nullptr ? encoded : value;
    curl_free(encoded);
    curl_easy_cleanup(curl);
    return result;
}

const char * hub_token_env(ModelHub hub) {
    return hub == ModelHub::ModelScope ? "MODELSCOPE_API_TOKEN" : "HF_TOKEN";
}

DownloadSpec spec_for_hub(const DownloadSpec & spec, ModelHub hub) {
    DownloadSpec resolved = spec;
    resolved.hub = hub;
    return resolved;
}

size_t write_callback(char * ptr, size_t size, size_t nmemb, void * userdata) {
    auto * ctx = static_cast<DownloadContext *>(userdata);
    return std::fwrite(ptr, size, nmemb, ctx->out);
}

int progress_callback(
    void * clientp,
    curl_off_t dltotal,
    curl_off_t dlnow,
    curl_off_t /*ultotal*/,
    curl_off_t /*ulnow*/) {
    auto * ctx = static_cast<DownloadContext *>(clientp);

    if (dltotal > 0) {
        const curl_off_t pct = dlnow * 100 / dltotal;
        if (pct != ctx->last_reported) {
            ctx->last_reported = pct;
            if (!g_quiet) {
                std::fprintf(stderr, "\rdownloading: %3lld%%", static_cast<long long>(pct));
                std::fflush(stderr);
            }
        }
    }

    const auto now = std::chrono::steady_clock::now();
    const double since_last = std::chrono::duration<double>(now - ctx->last_time).count();
    if (since_last >= 0.2 || dlnow == dltotal) {
        ctx->last_time = now;
        report_progress(ctx, dltotal, dlnow);
    }

    return 0;
}

void apply_auth_header(CURL * curl, struct curl_slist ** headers, ModelHub hub) {
    if (const char * token = std::getenv(hub_token_env(hub)); token != nullptr && token[0] != '\0') {
        const std::string auth = std::string("Authorization: Bearer ") + token;
        *headers = curl_slist_append(*headers, auth.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, *headers);
    }
}

void configure_curl_common(CURL * curl, bool is_probe) {
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "QTrans/0.1");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

    if (is_probe) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);
    } else {
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    }
}

void check_http_status(CURL * curl) {
    long http_code = 0;
    if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code) != CURLE_OK) {
        throw std::runtime_error("failed to read HTTP status");
    }
    if (http_code >= 400) {
        throw std::runtime_error("HTTP error " + std::to_string(http_code));
    }
}

size_t discard_callback(char * /*ptr*/, size_t size, size_t nmemb, void * /*userdata*/) {
    return size * nmemb;
}

bool remote_file_available(const std::string & url, ModelHub hub) {
    ensure_curl_initialized();

    CURL * curl = curl_easy_init();
    if (curl == nullptr) {
        return false;
    }

    struct curl_slist * headers = nullptr;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_RANGE, "0-0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_callback);
    configure_curl_common(curl, true);
    apply_auth_header(curl, &headers, hub);

    const CURLcode result = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return result == CURLE_OK && http_code >= 200 && http_code < 400;
}

void download_from_hub(const std::string & local_path, const DownloadSpec & spec, ModelHub hub) {
    ensure_curl_initialized();

    const DownloadSpec resolved = spec_for_hub(spec, hub);
    const std::string url = download_resolve_url(resolved, hub);
    const std::string temp_path = local_path + ".download";

    ensure_parent_dir(local_path);
    std::filesystem::remove(temp_path);

    DownloadContext ctx{};
    ctx.start_time = std::chrono::steady_clock::now();
    ctx.last_time = ctx.start_time;
    ctx.out = std::fopen(temp_path.c_str(), "wb");
    if (ctx.out == nullptr) {
        throw std::runtime_error("failed to open temporary file: " + temp_path);
    }

    CURL * curl = curl_easy_init();
    if (curl == nullptr) {
        std::fclose(ctx.out);
        std::filesystem::remove(temp_path);
        throw std::runtime_error("curl_easy_init failed");
    }

    struct curl_slist * headers = nullptr;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    configure_curl_common(curl, false);
    apply_auth_header(curl, &headers, hub);

    const CURLcode result = curl_easy_perform(curl);
    std::fclose(ctx.out);

    if (result != CURLE_OK) {
        std::filesystem::remove(temp_path);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        throw std::runtime_error(std::string("download failed: ") + curl_easy_strerror(result));
    }

    try {
        check_http_status(curl);
    } catch (...) {
        std::filesystem::remove(temp_path);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        throw;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    std::error_code ec;
    std::filesystem::rename(temp_path, local_path, ec);
    if (ec) {
        std::filesystem::remove(temp_path);
        throw std::runtime_error("failed to finalize download: " + ec.message());
    }

    if (!g_quiet) {
        std::fprintf(stderr, "\n");
    }
}

bool parse_scheme_uri(
    const std::string & uri,
    const char * scheme,
    ModelHub hub,
    DownloadSpec & out,
    std::string & local_name) {
    if (uri.rfind(scheme, 0) != 0) {
        return false;
    }

    out.hub = hub;
    if (!download_parse_spec(uri.substr(std::strlen(scheme)), out)) {
        throw std::runtime_error("invalid model URI: " + uri);
    }

    local_name = std::filesystem::path(out.filename).filename().string();
    return true;
}

std::vector<ModelHub> auto_hub_candidates() {
    return { ModelHub::ModelScope, ModelHub::HuggingFace };
}

} // namespace

bool download_parse_spec(const std::string & spec, DownloadSpec & out) {
    const std::size_t repo_sep = spec.find('/');
    if (repo_sep == std::string::npos) {
        return false;
    }

    const std::size_t file_sep = spec.find('/', repo_sep + 1);
    if (file_sep == std::string::npos) {
        return false;
    }

    out.repo = spec.substr(0, file_sep);
    out.filename = spec.substr(file_sep + 1);
    return !out.repo.empty() && !out.filename.empty();
}

bool download_parse_uri(const std::string & uri, DownloadSpec & out, std::string & local_name) {
    return parse_scheme_uri(uri, k_hf_scheme, ModelHub::HuggingFace, out, local_name) ||
           parse_scheme_uri(uri, k_ms_scheme, ModelHub::ModelScope, out, local_name) ||
           parse_scheme_uri(uri, k_auto_scheme, ModelHub::Auto, out, local_name);
}

std::string download_default_revision(ModelHub hub) {
    return hub == ModelHub::ModelScope ? "master" : "main";
}

std::string download_resolve_url(const DownloadSpec & spec, ModelHub hub) {
    const std::string revision = spec.revision.empty()
        ? download_default_revision(hub)
        : spec.revision;

    if (hub == ModelHub::ModelScope) {
        return "https://www.modelscope.cn/models/" + spec.repo +
               "/resolve/" + revision + "/" + spec.filename;
    }

    return "https://huggingface.co/" + spec.repo + "/resolve/" + revision + "/" + spec.filename;
}

const char * download_hub_name(ModelHub hub) {
    switch (hub) {
    case ModelHub::HuggingFace:
        return "HuggingFace";
    case ModelHub::ModelScope:
        return "ModelScope";
    case ModelHub::Auto:
        return "Auto";
    }
    return "Unknown";
}

bool download_file_exists(const std::string & path) {
    return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
}

ModelHub download_probe_hub(const DownloadSpec & spec) {
    std::string errors;

    for (const ModelHub hub : auto_hub_candidates()) {
        const std::string url = download_resolve_url(spec, hub);
        if (remote_file_available(url, hub)) {
            log_message("auto: selected %s\n", download_hub_name(hub));
            return hub;
        }
        if (!errors.empty()) {
            errors += "; ";
        }
        errors += std::string(download_hub_name(hub)) + " unavailable at " + url;
    }

    throw std::runtime_error("auto hub selection failed: " + errors);
}

void download_to_file(const std::string & local_path, const DownloadSpec & spec, bool force) {
    if (!force && download_file_exists(local_path)) {
        return;
    }

    if (spec.hub != ModelHub::Auto) {
        const ModelHub hub = spec.hub;
        const std::string url = download_resolve_url(spec, hub);
        log_message("downloading [%s] %s\n", download_hub_name(hub), url.c_str());
        download_from_hub(local_path, spec, hub);
        return;
    }

    std::string errors;
    for (const ModelHub hub : auto_hub_candidates()) {
        const std::string url = download_resolve_url(spec, hub);
        try {
            if (!remote_file_available(url, hub)) {
                if (!errors.empty()) {
                    errors += "; ";
                }
                errors += std::string(download_hub_name(hub)) + " unavailable at " + url;
                continue;
            }

            log_message("auto: selected %s\n", download_hub_name(hub));
            log_message("downloading [%s] %s\n", download_hub_name(hub), url.c_str());
            download_from_hub(local_path, spec, hub);
            return;
        } catch (const std::exception & ex) {
            if (!errors.empty()) {
                errors += "; ";
            }
            errors += std::string(download_hub_name(hub)) + " failed: " + ex.what();
            std::filesystem::remove(local_path + ".download");
        }
    }

    throw std::runtime_error("auto download failed: " + errors);
}

void download_ensure(const std::string & local_path, const DownloadSpec & spec, bool force) {
    if (!force && download_file_exists(local_path)) {
        log_message("using cached model: %s\n", local_path.c_str());
        return;
    }

    download_to_file(local_path, spec, true);
    log_message("saved to %s\n", local_path.c_str());
}

void download_set_progress_callback(DownloadProgressCallback callback) {
    std::lock_guard<std::mutex> lock(g_progress_mutex);
    g_progress_callback = std::move(callback);
}

void download_set_quiet(bool quiet) {
    g_quiet = quiet;
}
