///----------------------------------------------------------------------------------------------------
/// Name         :  Sampler.cpp
/// Description  :  See Sampler.h.
///----------------------------------------------------------------------------------------------------

#include "Sampler.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <Pdh.h>
#include <PdhMsg.h>

#include <atomic>
#include <thread>
#include <unordered_map>
#include <vector>
#include <cstdio>

#pragma comment(lib, "pdh.lib")

namespace
{
	constexpr DWORD SAMPLE_INTERVAL_MS = 500;
	constexpr float SMOOTHING          = 0.5f; 
	constexpr int   STATE_CONFIRMATION = 2;    

	std::thread        s_Thread;
	HANDLE             s_StopEvent = nullptr;

	std::atomic<float> s_GpuUtil{ -1.0f };
	std::atomic<float> s_MaxThreadUtil{ 0.0f };
	std::atomic<int>   s_Bottleneck{ (int)EBottleneck::Unknown };
	std::atomic<float> s_GpuThreshold{ 90.0f };
	std::atomic<float> s_CpuThreshold{ 85.0f };
	std::atomic<bool>  s_Paused{ false };

	class GpuCounter
	{
	public:
		~GpuCounter() { Close(); }

		bool Open()
		{
			Close();
			if (PdhOpenQueryW(nullptr, 0, &m_Query) != ERROR_SUCCESS) { m_Query = nullptr; return false; }

			wchar_t path[128];
			swprintf_s(path, L"\\GPU Engine(pid_%lu_*engtype_3D)\\Utilization Percentage", GetCurrentProcessId());
			if (PdhAddEnglishCounterW(m_Query, path, 0, &m_Counter) != ERROR_SUCCESS) { Close(); return false; }

			PdhCollectQueryData(m_Query); 
			return true;
		}

		void Close()
		{
			if (m_Query) PdhCloseQuery(m_Query);
			m_Query = nullptr;
			m_Counter = nullptr;
		}

		float Read()
		{
			if (!m_Query) return -1.0f;
			if (PdhCollectQueryData(m_Query) != ERROR_SUCCESS) return -1.0f;

			DWORD size = 0, count = 0;
			PDH_STATUS st = PdhGetFormattedCounterArrayW(m_Counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, nullptr);
			if (st != PDH_MORE_DATA || size == 0) return -1.0f;

			m_Buffer.resize(size);
			auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(m_Buffer.data());
			if (PdhGetFormattedCounterArrayW(m_Counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, items) != ERROR_SUCCESS)
				return -1.0f;

			double best = -1.0;
			for (DWORD i = 0; i < count; i++)
			{
				if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
				if (items[i].FmtValue.doubleValue > best) best = items[i].FmtValue.doubleValue;
			}
			if (best < 0.0) return -1.0f;
			return best > 100.0 ? 100.0f : (float)best;
		}

	private:
		PDH_HQUERY        m_Query   = nullptr;
		PDH_HCOUNTER      m_Counter = nullptr;
		std::vector<BYTE> m_Buffer;
	};

	class ThreadCpu
	{
	public:
		~ThreadCpu()
		{
			for (auto& [tid, info] : m_Threads) CloseHandle(info.Handle);
		}

		void Reset() { m_LastQpc.QuadPart = 0; }

		float Read(DWORD aIgnoreTid)
		{
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			const double wall100ns = m_LastQpc.QuadPart
				? (double)(now.QuadPart - m_LastQpc.QuadPart) * 1e7 / (double)Freq()
				: 0.0;
			m_LastQpc = now;

			HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
			if (snap == INVALID_HANDLE_VALUE) return 0.0f;

			for (auto& [tid, info] : m_Threads) info.Seen = false;

			const DWORD pid = GetCurrentProcessId();
			double best = 0.0;

			THREADENTRY32 te{ sizeof(te) };
			for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
			{
				if (te.th32OwnerProcessID != pid || te.th32ThreadID == aIgnoreTid) continue;

				auto it = m_Threads.find(te.th32ThreadID);
				if (it == m_Threads.end())
				{
					HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
					if (!h) continue;
					it = m_Threads.emplace(te.th32ThreadID, Info{ h, ThreadTime(h), true }).first;
					continue; // no delta yet
				}

				Info& info = it->second;
				info.Seen = true;
				const ULONGLONG t = ThreadTime(info.Handle);
				if (wall100ns > 0.0)
				{
					const double pct = (double)(t - info.Last) / wall100ns * 100.0;
					if (pct > best) best = pct;
				}
				info.Last = t;
			}
			CloseHandle(snap);

			for (auto it = m_Threads.begin(); it != m_Threads.end();)
			{
				if (!it->second.Seen) { CloseHandle(it->second.Handle); it = m_Threads.erase(it); }
				else ++it;
			}

			return best > 100.0 ? 100.0f : (float)best;
		}

	private:
		struct Info { HANDLE Handle; ULONGLONG Last; bool Seen; };

		static ULONGLONG ThreadTime(HANDLE h)
		{
			FILETIME c, e, k, u;
			if (!GetThreadTimes(h, &c, &e, &k, &u)) return 0;
			auto toU64 = [](const FILETIME& ft) { return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime; };
			return toU64(k) + toU64(u);
		}

		static LONGLONG Freq()
		{
			static LONGLONG f = [] { LARGE_INTEGER li; QueryPerformanceFrequency(&li); return li.QuadPart; }();
			return f;
		}

		std::unordered_map<DWORD, Info> m_Threads;
		LARGE_INTEGER                   m_LastQpc{};
	};

	EBottleneck Classify(float aGpu, float aCpu)
	{
		const bool gpuKnown = aGpu >= 0.0f;
		if (gpuKnown && aGpu >= s_GpuThreshold.load()) return EBottleneck::GPU;
		if (aCpu >= s_CpuThreshold.load())             return EBottleneck::CPU;
		return gpuKnown ? EBottleneck::Limit : EBottleneck::Unknown;
	}

	void Worker()
	{
		GpuCounter gpu;
		bool gpuOk = gpu.Open();
		int  gpuRetry = 0;

		ThreadCpu cpu;
		const DWORD selfTid = GetCurrentThreadId();

		float gpuAvg = -1.0f, cpuAvg = 0.0f;
		EBottleneck shown = EBottleneck::Unknown, pending = EBottleneck::Unknown;
		int pendingCount = 0;

		bool wasPaused = false;

		while (WaitForSingleObject(s_StopEvent, SAMPLE_INTERVAL_MS) == WAIT_TIMEOUT)
		{
			if (s_Paused.load())
			{
				if (!wasPaused)
				{
					gpu.Close();
					gpuOk = false;
					s_GpuUtil.store(-1.0f);
					s_MaxThreadUtil.store(0.0f);
					s_Bottleneck.store((int)EBottleneck::Unknown);
					wasPaused = true;
				}
				continue;
			}
			if (wasPaused)
			{
				gpuOk = gpu.Open();
				gpuRetry = 0;
				cpu.Reset();
				gpuAvg = -1.0f; cpuAvg = 0.0f;
				shown = pending = EBottleneck::Unknown; pendingCount = 0;
				wasPaused = false;
				continue; 
			}

			float g = gpuOk ? gpu.Read() : -1.0f;
			if (g < 0.0f && ++gpuRetry >= 20) { gpuOk = gpu.Open(); gpuRetry = 0; }
			if (g >= 0.0f) gpuRetry = 0;

			gpuAvg = (g < 0.0f) ? -1.0f : (gpuAvg < 0.0f ? g : gpuAvg + SMOOTHING * (g - gpuAvg));

			const float c = cpu.Read(selfTid);
			cpuAvg += SMOOTHING * (c - cpuAvg);

			s_GpuUtil.store(gpuAvg);
			s_MaxThreadUtil.store(cpuAvg);

			const EBottleneck now = Classify(gpuAvg, cpuAvg);
			if (now == shown) { pendingCount = 0; }
			else if (now == pending) { if (++pendingCount >= STATE_CONFIRMATION) { shown = now; pendingCount = 0; } }
			else { pending = now; pendingCount = 1; }

			s_Bottleneck.store((int)shown);
		}
	}
}

namespace Sampler
{
	void Start()
	{
		if (s_Thread.joinable()) return;
		s_StopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!s_StopEvent) return;
		s_Thread = std::thread(Worker);
	}

	void Stop()
	{
		if (!s_Thread.joinable()) return;
		SetEvent(s_StopEvent);
		s_Thread.join();
		CloseHandle(s_StopEvent);
		s_StopEvent = nullptr;
	}

	float       GpuUtil()       { return s_GpuUtil.load(); }
	float       MaxThreadUtil() { return s_MaxThreadUtil.load(); }
	EBottleneck Bottleneck()    { return (EBottleneck)s_Bottleneck.load(); }

	void SetThresholds(float aGpu, float aCpu)
	{
		s_GpuThreshold.store(aGpu);
		s_CpuThreshold.store(aCpu);
	}

	void SetPaused(bool aPaused) { s_Paused.store(aPaused); }
}
