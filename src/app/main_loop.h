#pragma once

#include <functional>
#include <vector>

class PollSource;
class WaylandConnection;

class MainLoop {
public:
  MainLoop(WaylandConnection& wayland, std::vector<PollSource*> sources, std::function<bool()> running);
  void run();

private:
  WaylandConnection& m_wayland;
  std::vector<PollSource*> m_sources;
  std::function<bool()> m_running;
};
