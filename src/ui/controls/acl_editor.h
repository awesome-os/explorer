#pragma once

#include "operations/permission_service.h"
#include "ui/controls/flex.h"

class Label;
class AclEditor final : public Flex {
public:
  explicit AclEditor(std::function<void()> changed);
  void sync(const luciole::PermissionSnapshot& snapshot);
  bool canApply() const;
  luciole::AclEdit edit() const;
  const Node* messageNode() const;

private:
  void doLayout(Renderer& renderer) override;
  LayoutSize doMeasure(Renderer& renderer, const LayoutConstraints& constraints) override;
  void doArrange(Renderer& renderer, const LayoutRect& rect) override;
  void prepareRows();
  void rebuild();
  void changed(bool layout);
  void recalculate(bool defaults);
  void updateEffective();
  std::function<void()> m_changed;
  std::shared_ptr<const luciole::AclInfo> m_actual;
  std::vector<luciole::AclEntry> m_access, m_defaults;
  std::vector<std::pair<Label*, std::pair<bool, std::size_t>>> m_effective;
  Flex* m_rows = nullptr;
  Label *m_status = nullptr, *m_reason = nullptr;
  bool m_editable = false, m_rebuild = true, m_draft = false;
};
