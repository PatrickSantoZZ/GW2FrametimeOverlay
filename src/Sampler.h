///----------------------------------------------------------------------------------------------------
/// Name         :  Sampler.h
/// Description  :  Background sampling of GPU utilization (Windows "GPU Engine" counters, same source
///                 as Task Manager) and per-thread CPU usage of the game process.
///----------------------------------------------------------------------------------------------------

#pragma once

enum class EBottleneck : int
{
	Unknown = 0, // not enough data yet / GPU counters unavailable
	CPU,         // one game thread is saturated, GPU has headroom
	GPU,         // GPU 3D engine is saturated
	Limit        // neither is saturated: FPS cap, V-Sync or other waits
};

namespace Sampler
{
	void Start();
	void Stop();

	/// GPU 3D engine utilization of this process in percent, or -1 if counters are unavailable.
	float GpuUtil();

	/// CPU usage of the busiest game thread in percent of one core.
	float MaxThreadUtil();

	EBottleneck Bottleneck();

	/// Thresholds in percent, editable from the options.
	void SetThresholds(float aGpu, float aCpu);

	/// While paused, no counters or thread times are read at all.
	void SetPaused(bool aPaused);
}
