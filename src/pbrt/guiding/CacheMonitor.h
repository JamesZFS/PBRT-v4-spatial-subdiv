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

    // Add/activate a probe at the given pixel. Return true if the probe is added, false if it already exists.
    // Corrects the pixel to the nearest existing probe if it is close enough.
    bool AddProbe(pbrt::Point2i &pixel);

    void UpdateProbe(pbrt::Point2i pixel, std::function<void(Probe &)> f);

    void ForEachProbe(std::function<void(Probe &)> f);

    void RequestFitAxes() { m_shouldFitAxes = true; }

    void Clear();  // Clear the data but keep the probes

    void Reset();

    void SetProbeRadius(float radius) { m_probeRadius = radius; }

    float GetProbeRadius() const { return m_probeRadius; }

    bool DisplayProbeID() const { return m_displayProbeID; }

private:
    std::vector<Probe> m_probes;
    std::mutex m_mutex;  // protect probe data

    float m_probeRadius = 10;
    float m_markerSize = 2;
    bool m_displayProbeID = false;
    std::atomic_bool m_shouldFitAxes = true;
};



#endif //CACHEMONITOR_H
