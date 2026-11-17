///----------------------------------------------------------------------------------------------------
/// Name         :  FrameStats.h
/// Description  :  Frame time history, average FPS and 1% low. Fed once per frame from the render
///----------------------------------------------------------------------------------------------------

#pragma once

#include <Windows.h>
#include <algorithm>
#include <vector>

class FrameStats
{
public:
	static constexpr int GRAPH_SIZE   = 240;  // frames shown in the graph
	static constexpr int HISTORY_SIZE = 8192; // frames kept for 1% low calculation

	FrameStats()
	{
		LARGE_INTEGER f; QueryPerformanceFrequency(&f);
		m_Freq = (double)f.QuadPart;
		m_History.assign(HISTORY_SIZE, 0.0f);
	}

	/// Call exactly once per frame no more no less
	void Tick()
	{
		LARGE_INTEGER now; QueryPerformanceCounter(&now);
		if (m_Last.QuadPart == 0) { m_Last = now; return; }

		const float ms = (float)((now.QuadPart - m_Last.QuadPart) * 1000.0 / m_Freq);
		m_Last = now;

		// Ignore huge gaps (alt-tab, loading screens) so they don't fuck with the stats
		if (ms <= 0.0f || ms > 1000.0f) return;

		m_Graph[m_GraphPos] = ms;
		m_GraphPos = (m_GraphPos + 1) % GRAPH_SIZE;

		m_History[m_HistPos] = ms;
		m_HistPos = (m_HistPos + 1) % HISTORY_SIZE;
		if (m_HistCount < HISTORY_SIZE) m_HistCount++;

		m_AccumMs += ms;
		m_AccumFrames++;
		if (m_AccumMs >= UPDATE_MS) Recompute();
	}

	float Fps()         const { return m_Fps; }
	float FrameTimeMs() const { return m_FrameTimeMs; }
	float OnePercentLow() const { return m_Low1; }
	float GraphMax()    const { return m_GraphMax; }

	const float* GraphData()   const { return m_Graph; }
	int          GraphOffset() const { return m_GraphPos; }

private:
	static constexpr float UPDATE_MS = 500.0f;  
	static constexpr float WINDOW_MS = 5000.0f; 

	void Recompute()
	{
		m_FrameTimeMs = m_AccumMs / (float)m_AccumFrames;
		m_Fps = 1000.0f / m_FrameTimeMs;
		m_AccumMs = 0.0f;
		m_AccumFrames = 0;

		m_Scratch.clear();
		float total = 0.0f;
		for (int i = 0; i < m_HistCount && total < WINDOW_MS; i++)
		{
			const float ms = m_History[(m_HistPos - 1 - i + HISTORY_SIZE) % HISTORY_SIZE];
			m_Scratch.push_back(ms);
			total += ms;
		}

		if (!m_Scratch.empty())
		{

			const size_t idx = (size_t)((m_Scratch.size() - 1) * 0.99);
			std::nth_element(m_Scratch.begin(), m_Scratch.begin() + idx, m_Scratch.end());
			const float p99 = m_Scratch[idx];
			m_Low1 = p99 > 0.0f ? 1000.0f / p99 : 0.0f;

			float gmax = 0.0f;
			for (float v : m_Graph) gmax = (std::max)(gmax, v);
			m_GraphMax = (std::max)(gmax * 1.1f, m_FrameTimeMs * 2.0f);
		}
	}

	double             m_Freq = 1.0;
	LARGE_INTEGER      m_Last{};

	float              m_Graph[GRAPH_SIZE] = {};
	int                m_GraphPos = 0;

	std::vector<float> m_History;
	int                m_HistPos = 0;
	int                m_HistCount = 0;
	std::vector<float> m_Scratch;

	float              m_AccumMs = 0.0f;
	int                m_AccumFrames = 0;

	float              m_Fps = 0.0f;
	float              m_FrameTimeMs = 0.0f;
	float              m_Low1 = 0.0f;
	float              m_GraphMax = 16.7f;
};
