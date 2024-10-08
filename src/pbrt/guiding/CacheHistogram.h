//
// Created by fengshi on 10/7/24.
//

#ifndef CACHEHISTOGRAM_H
#define CACHEHISTOGRAM_H

#include "View.h"
#include <mutex>
#include <atomic>
#include <functional>


class CacheHistogram {
public:
    struct Data {
        std::vector<float> fluence;
        std::vector<float> ce;
        std::vector<int> depth;
        std::vector<int> samples;
    };

    enum PlotType {
        PlotType_Fluence,
        PlotType_CE,
        PlotType_Depth,
        PlotType_Samples,
    };

    /// A window that plots a data field histogram of the regions
    class Hist: public View {
    public:
        void Draw() override;

        bool isHovered = false;
        float hoveringValue = std::numeric_limits<float>::infinity();

    private:
        Hist(pbrt::Application* parent, bool isMain, const std::string &title, PlotType type, CacheHistogram &object)
            : View(parent), m_isMain(isMain), m_title(title), m_type(type), m_object(object) {}

        bool m_isMain;
        std::string m_title;
        PlotType m_type;
        CacheHistogram &m_object;
        std::atomic_bool m_shouldFitAxes = true;

        friend class CacheHistogram;
    };

    CacheHistogram(pbrt::Application* parent);

    ~CacheHistogram();

    Hist &AddPlot(const std::string &title, PlotType type, bool isMain);

    void Update(std::function<void(Data &)> f);

    void RequestFitAxes();

    void Clear();

private:
    pbrt::Application *m_parent;
    std::mutex m_mutex;  // protect hist data
    Data m_data;
    std::vector<Hist*> m_plots;

    bool m_autoFitAxes = true;
    int m_bins;
};



#endif //CACHEHISTOGRAM_H
