//
// Created by fengshi on 1/21/25.
//

#ifndef PLOTS_H
#define PLOTS_H


#include "View.h"
#include <mutex>
#include <atomic>


class PlotManager {
public:
    PlotManager(pbrt::Application *parent) : m_parent(parent) {}

    ~PlotManager();

    struct Plot : public View {
        Plot(pbrt::Application *parent, const std::string &xAxisName, const std::string &yAxisName, PlotManager &plotManager);

        void Draw() override;

        uint64_t ID() const { return (uint64_t) this; }

        struct Curve {
            std::string id;
            std::vector<float> xData, yData;

            explicit Curve(const std::string &id) : id(id) {}
        };

        std::string m_xAxisName, m_yAxisName;
        std::vector<Curve> m_curves;
        PlotManager &m_manager;
        std::atomic_bool m_shouldFitAxes = true;
    };

    Plot &AddPlot(const std::string &yAxisName);

    void AppendData(const std::string &yAxisName, float x, float y);

    void ClearAll();

    void ClearCurrent();

    void RequestFitAxes();

private:
    pbrt::Application* m_parent;
    std::mutex m_mutex;  // protect plot data
    std::map<std::string, Plot*> m_plots;  // yAxisName -> Plot
    struct {
        uint64_t hoveringID = 0;  // ID of the plot being hovered
        double x = 0, y = 0;
    } m_mouse;

    float m_lineWeight = 1;
    float m_markerSize = 2;
    float m_alpha = 0.5f;
    bool m_autoFitAxes = true;
    std::string m_curveId = "#0";  // value of the input box
};


#endif //PLOTS_H
