#include "conversion_provider_factory.h"

#include <filesystem>
#include <string>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "dictionary_conversion_provider.h"
#include "imm32_conversion_provider.h"
#include "mozc_conversion_provider.h"

namespace ime::conversion {

namespace {

class UnavailableConversionProvider final : public IConversionProvider {
public:
    explicit UnavailableConversionProvider(std::wstring error)
        : error_(std::move(error)) {}

    ProviderCapabilities GetCapabilities() const override {
        ProviderCapabilities caps;
        caps.supports_layer2 = false;
        caps.supports_translation = false;
        return caps;
    }

    void SetResultCallback(ProviderCallback) override {}

    CandidateList FetchLayer1(const LayerRequestContext& ctx) override {
        return MakeUnavailableList(ctx.layer ? ctx.layer : 1);
    }

    CandidateList FetchLayer2(const LayerRequestContext& ctx) override {
        return MakeUnavailableList(ctx.layer ? ctx.layer : 2);
    }

    CandidateList FetchTranslation(const LayerRequestContext& ctx) override {
        return MakeUnavailableList(ctx.layer ? ctx.layer : 3);
    }

    bool Cancel(uint64_t) override {
        return false;
    }

private:
    CandidateList MakeUnavailableList(uint32_t layer) const {
        CandidateList list;
        list.layer = layer;
        list.error = error_;
        return list;
    }

    std::wstring error_;
};

std::filesystem::path ResolveAgainstInstallRoot(
    const std::filesystem::path& path,
    const std::filesystem::path& installRoot) {
    if (path.empty()) {
        return path;
    }
    if (path.is_absolute() || installRoot.empty()) {
        return path;
    }
    return installRoot / path;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    int required = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(required), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), wide.data(),
                          required);
    return wide;
}

std::wstring NativeBackendName(
    ime::config::MozcNativeBackend backend,
    const std::string& rawValue) {
    switch (backend) {
    case ime::config::MozcNativeBackend::kMozcServerClient:
    case ime::config::MozcNativeBackend::kUnset:
        return L"mozc_server_client";
    case ime::config::MozcNativeBackend::kLinkedConverter:
        return L"linked_converter";
    case ime::config::MozcNativeBackend::kInvalid:
        return rawValue.empty() ? L"<invalid>" : Utf8ToWide(rawValue);
    }
    return L"<unknown>";
}

ime::config::ProviderSettings NormalizeProviderSettings(
    const ime::config::ProviderSettings& settings,
    const std::filesystem::path& installRoot) {
    ime::config::ProviderSettings normalized = settings;

    if (normalized.kana.dictionary) {
        normalized.kana.dictionary->morph_tsv =
            ResolveAgainstInstallRoot(normalized.kana.dictionary->morph_tsv,
                                      installRoot);
        normalized.kana.dictionary->bilingual_tsv =
            ResolveAgainstInstallRoot(normalized.kana.dictionary->bilingual_tsv,
                                      installRoot);
        normalized.kana.dictionary->learning_profile_root =
            ResolveAgainstInstallRoot(
                normalized.kana.dictionary->learning_profile_root, installRoot);
    }

    if (normalized.translation.dictionary) {
        normalized.translation.dictionary->morph_tsv =
            ResolveAgainstInstallRoot(
                normalized.translation.dictionary->morph_tsv, installRoot);
        normalized.translation.dictionary->bilingual_tsv =
            ResolveAgainstInstallRoot(
                normalized.translation.dictionary->bilingual_tsv, installRoot);
        normalized.translation.dictionary->learning_profile_root =
            ResolveAgainstInstallRoot(
                normalized.translation.dictionary->learning_profile_root,
                installRoot);
    }

    if (normalized.kana.mozc) {
        auto& mozc = *normalized.kana.mozc;
        mozc.native.root =
            ResolveAgainstInstallRoot(mozc.native.root, installRoot);
        mozc.native.mozc_build_artifact =
            ResolveAgainstInstallRoot(mozc.native.mozc_build_artifact,
                                      installRoot);
        mozc.native.wrapper_exe =
            ResolveAgainstInstallRoot(mozc.native.wrapper_exe, installRoot);
        mozc.native.server_exe =
            ResolveAgainstInstallRoot(mozc.native.server_exe, installRoot);
    }

    return normalized;
}

}  // namespace

std::unique_ptr<IConversionProvider> CreateConversionProvider(
    const ime::config::ProviderSettings& settings,
    const std::filesystem::path& installRoot,
    std::wstring* error) {
    const ime::config::ProviderSettings normalized =
        NormalizeProviderSettings(settings, installRoot);

    if (error) {
        error->clear();
    }

    if (normalized.kana.mode == ime::config::ProviderMode::kDictionary &&
        normalized.kana.dictionary && normalized.kana.dictionary->enabled) {
        auto provider = std::make_unique<DictionaryConversionProvider>(
            *normalized.kana.dictionary,
            normalized.translation.dictionary,
            normalized.translation.mode,
            normalized.translation.llm,
            installRoot);
        if (provider->Initialize(error)) {
            return provider;
        }
    }
    if (normalized.kana.mode == ime::config::ProviderMode::kMozc &&
        normalized.kana.mozc && normalized.kana.mozc->enabled) {
        if (normalized.kana.mozc->transport == ime::config::MozcTransport::kInvalid) {
            std::wstring message = L"Unsupported mozc transport: " +
                                   Utf8ToWide(normalized.kana.mozc->transport_value);
            if (error) {
                *error = message;
            }
            return std::make_unique<UnavailableConversionProvider>(std::move(message));
        }
        if (normalized.kana.mozc->transport == ime::config::MozcTransport::kNative) {
            auto provider = std::make_unique<MozcConversionProvider>(
                *normalized.kana.mozc,
                normalized.translation.llm);
            std::wstring nativeError;
            if (provider->Initialize(&nativeError)) {
                if (error) {
                    error->clear();
                }
                return provider;
            }
            if (nativeError.empty()) {
                nativeError =
                    L"'provider.kana.mozc.transport=native' selected backend '" +
                    NativeBackendName(normalized.kana.mozc->native.backend,
                                      normalized.kana.mozc->native.backend_value) +
                    L"', but the OSS Mozc native backend is unavailable in this "
                    L"spike; no fallback was used.";
            }
            if (error) {
                *error = nativeError;
            }
            return std::make_unique<UnavailableConversionProvider>(
                std::move(nativeError));
        }
        auto provider = std::make_unique<MozcConversionProvider>(
            *normalized.kana.mozc,
            normalized.translation.llm);
        if (provider->Initialize(error)) {
            return provider;
        }
    }

    return std::make_unique<Imm32ConversionProvider>(normalized);
}

}  // namespace ime::conversion
