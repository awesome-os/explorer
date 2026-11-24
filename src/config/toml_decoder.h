#pragma once

#include "config/diagnostic.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>

namespace luciole {
  // Schema version written to and required in config.toml, settings.toml and state.toml.
  inline constexpr std::int64_t kSchemaVersion = 1;

  // A syntax error becomes one issue positioned at the error.
  std::expected<toml::table, ConfigIssues> parseToml(std::string_view text, const std::filesystem::path& file);

  bool isSchemaVersion(const toml::node& node);

  // Collects issues positioned at the offending node while decoding a parsed document.
  class TomlDecoder {
  public:
    explicit TomlDecoder(std::filesystem::path file) : m_file(std::move(file)) {}
    ConfigIssues issues;

    void issue(const toml::node& node, std::string key, std::string message);
    // Reports every field of `table` missing from `allowed` under `prefix` + field.
    void unknownKeys(
        const toml::table& table, const std::string& prefix, std::initializer_list<std::string_view> allowed,
        std::string_view message
    );
    // Missing optional arrays yield nullptr without an issue.
    const toml::array*
    array(const toml::table& table, std::string_view field, const std::string& key, std::size_t max, bool required);
    std::optional<std::string> text(const toml::table& table, std::string_view field, const std::string& key);

  private:
    std::filesystem::path m_file;
  };
} // namespace luciole
