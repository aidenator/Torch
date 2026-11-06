#pragma once

#include "ResourceType.h"
#include "n64/Cartridge.h"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include <iostream>
#include <any>
#include <memory>
#include <vector>
#include <string>
#include <variant>
#include <optional>
#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <strhash64/StrHash64.h>
#include "lib/binarytools/BinaryWriter.h"
#include "lib/binarytools/BinaryReader.h"

#ifdef BUILD_UI
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#endif

namespace fs = std::filesystem;

struct ParseResultData;

#define REGISTER(type, c) { ExportType::type, std::make_shared<c>() },

#define SEGMENT_OFFSET(a) ((uint32_t)(a) & 0x00FFFFFF)
#define SEGMENT_NUMBER(x) (((uint32_t)(x) >> 24) & 0xFF)
// I would love to use 0x01000000, but the stupid compiler takes it as 0x01
#define IS_SEGMENTED(x) ((SEGMENT_NUMBER(x) > 0) && (SEGMENT_NUMBER(x) < 0x20))
#define IS_VIRTUAL_SEGMENT(x) (SEGMENT_NUMBER(x) >= 0x80)
#define ASSET_PTR(x) ((IS_SEGMENTED(x) || IS_VIRTUAL_SEGMENT(x)) ? SEGMENT_OFFSET(x) : (x))

#define tab_t "\t"
#define fourSpaceTab "    "

struct OffsetEntry {
    uint32_t start;
    uint32_t end;
};

typedef std::optional<std::variant<size_t, OffsetEntry>> ExportResult;

enum class ExportType {
    Header,
    Code,
    Binary,
    Modding,
    XML
};

// node.as<T>(), without the std::stringstream yaml-cpp builds for every number. That stream and
// its locale were about a seventh of making bk.o2r on a 3DS. Only a whole-string integer that fits
// takes the quick path; anything else goes to yaml-cpp, so the results are the same.
template<typename T>
T NodeAs(const YAML::Node& node) {
    if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool> && sizeof(T) > 1) {
        if (node.IsScalar()) {
            const std::string& text = node.Scalar();
            // yaml-cpp reads with no base set, as strtol's base 0 does: 0x is hex, a leading 0 octal
            const bool negative = !text.empty() && text[0] == '-';
            const size_t first = negative ? 1 : 0;
            // Newer C libraries also read 0b as binary, which yaml-cpp doesn't
            const bool binary = first + 1 < text.size() && text[first] == '0' &&
                                (text[first + 1] == 'b' || text[first + 1] == 'B');
            if (first < text.size() && text[first] >= '0' && text[first] <= '9' && !binary &&
                (std::is_signed_v<T> || !negative)) {
                char* end = nullptr;
                errno = 0;
                if constexpr (std::is_signed_v<T>) {
                    const long long value = std::strtoll(text.c_str(), &end, 0);
                    if (errno == 0 && end == text.c_str() + text.size() &&
                        value >= (std::numeric_limits<T>::min)() && value <= (std::numeric_limits<T>::max)()) {
                        return static_cast<T>(value);
                    }
                } else {
                    const unsigned long long value = std::strtoull(text.c_str(), &end, 0);
                    if (errno == 0 && end == text.c_str() + text.size() && value <= (std::numeric_limits<T>::max)()) {
                        return static_cast<T>(value);
                    }
                }
            }
        }
    }
    return node.as<T>();
}

template<typename T>
std::optional<T> GetNode(YAML::Node& node, const std::string& key) {
    const auto value = node[key];
    if(!value) {
        return std::nullopt;
    }

    return std::optional<T>(NodeAs<T>(value));
}

// Each node[key] searches the map and allocates a node, so it's looked up once
template<typename T>
T GetSafeNode(YAML::Node& node, const std::string& key) {
    const auto value = node[key];
    if(!value) {
        auto dump = YAML::Dump(node);

        if (node["symbol"]) {
            throw std::runtime_error("Yaml asset missing the '" + key + "' node for '" + node["symbol"].as<std::string>() + "'\nProblematic YAML:\n" + dump);
        } else {
            throw std::runtime_error("Yaml asset missing the '" + key + "' node\nProblematic YAML:\n" + dump);
        }
    }

    return NodeAs<T>(value);
}

template<typename T>
T GetSafeNode(YAML::Node& node, const std::string& key, const T& def) {
    const auto value = node[key];
    if(!value) {
        return def;
    }

    return NodeAs<T>(value);
}

class IParsedData{};

template<typename T>
class GenericData : public IParsedData {
public:
    T mData;
};

class BaseExporter {
public:
    virtual ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) = 0;
    static void WriteHeader(LUS::BinaryWriter& write, Torch::ResourceType resType, int32_t version);
};

class BaseFactory {
public:
    virtual std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) = 0;
    virtual std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& data) {
        return std::nullopt;
    }
    std::optional<std::shared_ptr<BaseExporter>> GetExporter(ExportType type) {
        auto exporters = this->GetExporters();
        if (exporters.find(type) != exporters.end()) {
            return exporters[type];
        }
        return std::nullopt;
    }
    virtual bool SupportModdedAssets() {
        return false;
    }
    virtual bool HasModdedDependencies() {
        return false;
    }
    virtual uint32_t GetAlignment() {
        return 4;
    }
    virtual bool IsDialogPackRoot() const {
        return false;
    }
    virtual void PreprocessConfig(YAML::Node& cfg, N64::Cartridge* cart) {}
    virtual std::optional<std::shared_ptr<IParsedData>> CreateDataPointer() {
        return std::nullopt;
    }
    // When true, the viewer's default preview renders this factory's Code
    // exporter output as read-only text. Only enable for exporters that just
    // format to the stream (no file writes or other side effects).
    virtual bool CanPreviewCode() {
        return false;
    }
private:
    virtual std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() {
        return {};
    }
};

#ifdef BUILD_UI
// Base preview renderer. The default implementation (see BaseFactory.cpp) shows
// the asset's Code-exporter text when the factory opts in via CanPreviewCode(),
// otherwise its YAML config. Subclasses override DrawUI for richer visuals.
class BaseFactoryUI {
public:
    virtual ~BaseFactoryUI() = default;

    virtual float GetItemHeight(const ParseResultData& data);
    virtual void DrawUI(const ParseResultData& data);
};
#endif