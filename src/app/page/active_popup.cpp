#include "app/page/active_popup.h"

#include <utility>

void ActivePopup::open(std::unique_ptr<Flex> scene, Flex* content) {
  m_scene = std::move(scene);
  m_content = content;
  m_kind = Kind::Menu;
}

void ActivePopup::bind(Kind kind, std::function<void()> onClose) {
  m_kind = kind;
  m_onClose = std::move(onClose);
}

void ActivePopup::close() {
  ++m_generation;
  m_kind = Kind::None;
  if (auto onClose = std::exchange(m_onClose, {})) {
    onClose();
  }
}

void ActivePopup::release() {
  m_content = nullptr;
  m_scene.reset();
}
