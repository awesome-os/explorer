#pragma once

#include "model/job_activity.h"

class OperationJob;

// Main-thread snapshots of service facts only. Never performs filesystem I/O or provider queries.
std::optional<luciole::JobActivity> jobActivity(const OperationJob& job, bool details = false);
