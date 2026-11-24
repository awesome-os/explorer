#pragma once

#include "operations/permission_service.h"
#include "ui/controls/flex.h"

#include <array>

class Label;
class BulkAclEditor final : public Flex {
public:
  explicit BulkAclEditor(std::function<void()> changed);
  void sync(const luciole::PermissionSnapshot& snapshot);
  bool canApply() const;
  luciole::AclPatchEdit edit() const;
  const Node* messageNode() const;

private:
  struct Rule {
    luciole::AclTag tag;
    std::string account;
    std::array<std::size_t, 3> bits{};
    bool remove = false;
  };
  struct List {
    std::array<std::array<std::size_t, 3>, 4> bits{};
    std::vector<Rule> named;
    luciole::AclMaskPolicy mask = luciole::AclMaskPolicy::Keep;
  };
  void changed(bool rebuild);
  void prepareRows();
  void rebuild();
  LayoutSize doMeasure(Renderer& renderer, const LayoutConstraints& constraints) override;
  void doArrange(Renderer& renderer, const LayoutRect& rect) override;
  void doLayout(Renderer& renderer) override;
  std::array<List, 2> m_lists;
  luciole::AclDefaultPolicy m_defaults = luciole::AclDefaultPolicy::Keep;
  std::shared_ptr<const std::vector<luciole::PermissionResult>> m_results;
  std::function<void()> m_changed;
  Flex* m_rows = nullptr;
  Label *m_summary = nullptr, *m_status = nullptr;
  bool m_ready = false, m_rebuild = true;
};
