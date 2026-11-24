#pragma once

#include "thumbnails/preview_metadata.h"
#include "ui/controls/flex.h"

#include <span>

class Label;

// Literal, bounded facts shared by the compact preview and the Properties inspector.
class FileMetadataView final : public Flex {
public:
  FileMetadataView();
  void sync(std::span<const luciole::MetadataField> fields, float width, bool previewOnly = false);

private:
  struct Row {
    Flex* container;
    Label* key;
    Label* value;
  };
  std::vector<Row> m_rows;
};
