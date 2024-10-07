//
// Created by fengshi on 10/2/24.
//

#ifndef CACHEMONITOR_H
#define CACHEMONITOR_H


#include "View.h"
#include <mutex>
#include <atomic>
#include <functional>


class CacheMonitor {
public:
    struct PlotEntry {
        float iter;
        float ce;
        float depth;
        float samples;
    };

    struct Probe {
        bool active;
        pbrt::Point2i pixel;
        int idx;  // order of the probe created
        std::vector<PlotEntry> data;
    };

    enum PlotType {
        PlotType_CE,
        PlotType_Depth,
        PlotType_Samples,
    };

    /// A window that plots a data field of the probes
    class Plot: public View {
    public:
        void Draw() override;

        uint64_t ID() const { return (uint64_t) this; }

    private:
        Plot(bool isMain, const std::string &title, int yOffset, CacheMonitor &monitor)
            : m_isMain(isMain), m_title(title), m_yOffset(yOffset), m_monitor(monitor) {}

        bool m_isMain;
        std::string m_title;
        int m_yOffset;
        CacheMonitor &m_monitor;
        std::atomic_bool m_shouldFitAxes = true;

        friend class CacheMonitor;
    };

    CacheMonitor() = default;

    ~CacheMonitor();

    Plot &AddPlot(const std::string &title, PlotType type, bool isMain);

    // Add/activate a probe at the given pixel. Return true if the probe is added, false if it already exists.
    // Corrects the pixel to the nearest existing probe if it is close enough.
    bool AddProbe(pbrt::Point2i &pixel);

    void UpdateProbe(pbrt::Point2i pixel, std::function<void(Probe &)> f);

    void ForEachProbe(std::function<void(Probe &)> f);

    void RequestFitAxes();

    void Clear();  // Clear the data but keep the probes

    void Reset();

    void SetProbeRadius(float radius) { m_probeRadius = radius; }

    float GetProbeRadius() const { return m_probeRadius; }

    bool DisplayProbeID() const { return m_displayProbeID; }

private:
    std::vector<Probe> m_probes;
    std::mutex m_mutex;  // protect probe data
    std::vector<Plot*> m_plots;
    struct {
        uint64_t hoveringID = 0;  // ID of the plot being hovered
        double x = 0, y = 0;
    } m_mouse;

    float m_probeRadius = 10;
    float m_markerSize = 2;
    bool m_displayProbeID = false;
    bool m_autoFitAxes = true;
};



#endif //CACHEMONITOR_H
