#include "deferred.h"
#include "log/Log.h"
#include <mutex>
#include <vector>

namespace deferred
{
static std::mutex mtx;
static std::vector<std::function<void()>> queue;
static bool draining = false;

void post(std::function<void()> action)
{
	if (!action)
		return;
	const std::lock_guard<std::mutex> lock(mtx);
	queue.push_back(std::move(action));
}

void drain()
{
	// SWAP AND RELEASE THE LOCK BEFORE RUNNING ANYTHING. An action is allowed
	// to post another one (and to stop the emulator, which is the whole point),
	// so holding the lock across the call would deadlock on the first re-entrant
	// post. The newly posted action runs on the NEXT drain, which is also what
	// keeps one action from starving the frame loop.
	std::vector<std::function<void()>> run;
	{
		const std::lock_guard<std::mutex> lock(mtx);
		if (queue.empty())
			return;
		run.swap(queue);
	}
	draining = true;
	for (auto& action : run)
	{
		try {
			action();
		} catch (const std::exception& e) {
			// CONTAINED, NOT PROPAGATED. One bad action must not take down the
			// frame loop, and must not silently eat the ones queued behind it.
			ERROR_LOG(COMMON, "deferred action threw: %s", e.what());
		} catch (...) {
			ERROR_LOG(COMMON, "deferred action threw a non-std exception");
		}
	}
	draining = false;
}

bool inDrain() { return draining; }

void clear()
{
	const std::lock_guard<std::mutex> lock(mtx);
	queue.clear();
}
}
