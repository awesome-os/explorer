#pragma once

#include "operations/create_service.h"
#include "ui/controls/flex.h"

class Input;
class Label;
class Button;
class ScrollView;
class Segmented;

class LinkDialog final : public Flex {
public:
  LinkDialog(
      float width, std::string source, std::string name, std::string destination, bool hardAllowed,
      std::function<void(std::string, std::string, luciole::CreateKind)> submit, std::function<void()> cancel
  );
  Input* nameInput() const { return m_name; }
  void setSaving(bool saving);
  void showError(const std::string& error);
  void reveal(const Node* node);

private:
  void submit();
  Input *m_name = nullptr, *m_destination = nullptr;
  Segmented* m_kind = nullptr;
  ScrollView* m_scroll = nullptr;
  Label* m_error = nullptr;
  Button *m_create = nullptr, *m_cancel = nullptr;
  bool m_saving = false;
  std::function<void(std::string, std::string, luciole::CreateKind)> m_submit;
};
