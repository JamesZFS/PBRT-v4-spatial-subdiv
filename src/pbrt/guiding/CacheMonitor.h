//
// Created by fengshi on 10/2/24.
//

#ifndef CACHEMONITOR_H
#define CACHEMONITOR_H


#include "View.h"
#include <mutex>
#include <atomic>
#include <functional>


class CacheMonitor : public View {
public:
    struct PlotEntry {
        float x, y;
    };

    struct Probe {
        bool active;
        pbrt::Point2i pixel;
        int idx;  // order of the probe created
        std::vector<PlotEntry> data;
    };

    void Draw() override;

    bool AddProbe(pbrt::Point2i pixel);

    bool AddProbe(pbrt::Point2i pixel, PlotEntry entry);

    void ForEachProbe(std::function<void(Probe &)> f);

    void RequestFitAxes() { m_shouldFitAxes = true; }

    void Reset();  // Clear the data but keep the probes

    void Clear();

    void SetProbeRadius(float radius) { m_probeRadius = radius; }

    float GetProbeRadius() const { return m_probeRadius; }

private:
    std::vector<Probe> m_probes;
    std::mutex m_mutex;  // protect probe data

    float m_probeRadius = 10;
    std::atomic_bool m_shouldFitAxes = true;
};



#endif //CACHEMONITOR_H
