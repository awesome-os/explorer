#include "config/toml_decoder.h"

#include <algorithm>
#include <format>
#include <utility>

std::expected<toml::table, luciole::ConfigIssues>
luciole::parseToml(std::string_view text, const std::filesystem::path& file) {
  try {
    return toml::parse(text, file.string());
  } catch (const toml::parse_error& error) {
    const auto& position = error.source().begin;
    return std::unexpected(
        ConfigIssues{
            {.file = file,
             .line = position.line,
             .column = position.column,
             .key = {},
             .message = std::string(error.description())}
        }
    );
  }
}

bool luciole::isSchemaVersion(const toml::node& node) { return node.value_exact<std::int64_t>() == kSchemaVersion; }

void luciole::TomlDecoder::issue(const toml::node& node, std::string key, std::string message) {
  const auto& position = node.source().begin;
  issues.push_back(
      {.file = m_file,
       .line = position.line,
       .column = position.column,
       .key = std::move(key),
       .message = std::move(message)}
  );
}

void luciole::TomlDecoder::unknownKeys(
    const toml::table& table, const std::string& prefix, std::initializer_list<std::string_view> allowed,
    std::string_view message
) {
  for (const auto& [key, value] : table) {
    if (!std::ranges::contains(allowed, key.str())) {
      issue(value, prefix + std::string(key.str()), std::string(message));
    }
  }
}

const toml::array* luciole::TomlDecoder::array(
    const toml::table& table, std::string_view field, const std::string& key, std::size_t max, bool required
) {
  const auto* value = table.get(field);
  if (value == nullptr && !required) {
    return nullptr;
  }
  if (value == nullptr || !value->is_array() || value->as_array()->size() > max) {
    issue(value != nullptr ? *value : table, key, std::format("expected an array with at most {} items", max));
    return nullptr;
  }
  return value->as_array();
}

std::optional<std::string>
luciole::TomlDecoder::text(const toml::table& table, std::string_view field, const std::string& key) {
  const auto* node = table.get(field);
  if (node == nullptr || !node->is_string()) {
    issue(node != nullptr ? *node : table, key, "expected a string");
    return std::nullopt;
  }
  return node->as_string()->get();
}
