#pragma once

#include "operations/permission_service.h"
#include "ui/controls/flex.h"

class VirtualGridView;
class PermissionResultsView final : public Flex {
public:
  PermissionResultsView();
  ~PermissionResultsView() override;
  void sync(std::shared_ptr<const std::vector<luciole::PermissionResult>> results);
  bool navigate(std::uint32_t key, std::uint32_t modifiers, bool preedit);

private:
  class Adapter;
  std::unique_ptr<Adapter> m_adapter;
  std::shared_ptr<const std::vector<luciole::PermissionResult>> m_results;
  VirtualGridView* m_grid = nullptr;
};
