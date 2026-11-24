#pragma once

#include "render/scene/node.h"

#include <memory>
#include <optional>
#include <utility>

namespace luciole {
  class FileIconCache;
  class PreviewService;
  class ThumbnailService;
} // namespace luciole

struct KeyboardEvent;
class ApplicationChooserController;
class FilePreviewView;
class PageContext;
class RenderContext;

// File preview: the preview pane beside the file list and the quick preview dialog.
class PreviewController {
public:
  PreviewController(
      PageContext& page, luciole::PreviewService& preview, luciole::ThumbnailService& thumbnails,
      luciole::FileIconCache& icons, RenderContext& renderer, ApplicationChooserController& applications
  );

  // Asks the preview service for the selected item at the size the pane or popup shows it.
  void sync();
  void showQuickPreview();
  // Keyboard hook in BrowserPage::keyboard() while the popup is shown: the quick preview closes and steps through
  // items.
  bool quickPreviewKey(const KeyboardEvent& event);
  // Layout hook in BrowserPage::prepare(): shows and arranges the pane when `show`, hides it otherwise.
  void layoutPane(bool show);
  // BrowserPage::deactivate(): hides the pane and clears its image.
  void hidePane();
  // Dialog frame hook, before the dialog measures itself: sizes and syncs the quick preview.
  void layoutQuickPreview();

  // Pane rectangle the workspace reserves; empty while no pane fits.
  const std::optional<LayoutRect>& bounds() const { return m_bounds; }
  void setBounds(std::optional<LayoutRect> bounds) { m_bounds = bounds; }

private:
  void openPreviewed(bool with);
  std::unique_ptr<FilePreviewView> makeView(bool quick);
  // The quick preview's content size: the Medium dialog width and a height that follows the window.
  std::pair<float, float> quickPreviewSize() const;

  PageContext& m_page;
  luciole::PreviewService& m_preview;
  luciole::ThumbnailService& m_thumbnails;
  luciole::FileIconCache& m_icons;
  RenderContext& m_renderer;
  ApplicationChooserController& m_applications;
  FilePreviewView* m_view = nullptr;
  FilePreviewView* m_quickPreview = nullptr;
  std::optional<LayoutRect> m_bounds;
  std::shared_ptr<int> m_lifetime = std::make_shared<int>(0);
};
