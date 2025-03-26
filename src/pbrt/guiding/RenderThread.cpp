//
// Created by fengshi on 9/30/24.
//

#include <pbrt/cameras.h>
#include <pbrt/samplers.h>
#include <pbrt/film.h>
#include <pbrt/interaction.h>
#include <pbrt/shapes.h>
#include <pbrt/scene.h>
#include <pbrt/util/progressreporter.h>
#include "RenderThread.h"
#include "Application.h"

using namespace pbrt;

const std::vector<std::pair<const char *, const char *>> commandNames = {
    {"AutoPlay", "Automatically resume the rendering"},
    {"Pause", "Pause the rendering"},
    {"Forward", "Render the next wave of samples"},
    {"Save", "Save the current rendering"},
    {"Restart", "Restart rendering (resetting the cache)"},
    {"Terminate", "Terminate rendering"},
    {"None", "No command"},
};

RenderThread::RenderThread(Application *parent, const std::function<void(int waveStart)> &renderStep, const std::function<void(int waveEnd)> &saveImage)
    : m_parent(parent), m_mainThreadID(std::this_thread::get_id()), m_cmdCompleteCallbacks(CmdCount), m_renderStep(renderStep), m_saveImage(saveImage) {
    // Start the thread
    auto oldId = m_renderThreadID;
    m_thread = std::thread(&RenderThread::Run, this);
    std::unique_lock lock(m_mtxInitialized);
    m_cvInitialized.wait(lock, [&] { return m_renderThreadID != oldId; });
    std::cout << "RenderThread initialized with id: " << m_renderThreadID << std::endl;
}

void RenderThread::SetCmdCallback(Command cmd, const std::function<bool()> &callback) {
    m_cmdCompleteCallbacks[cmd] = callback;
}

void RenderThread::SendCommand(Command cmd) {
    if (std::this_thread::get_id() != m_mainThreadID) {
        ErrorExit("RenderThread::SendCommand() should be called from the main thread");
    }
    std::cout << "SendCommand: " << commandNames[cmd].first << std::endl;
    std::lock_guard lock(m_mtxCmd);
    m_pendingCmd = cmd;
    m_cv.notify_one();
}

void RenderThread::Join() {
    m_thread.join();
}

void RenderThread::SetInitial() {
    CHECK(m_state != Rendering);
    m_state = Initial;
    m_waveStart = 0;
}

void RenderThread::Run() {
    // This function runs in a separate thread than the GUI.
    // It listens for pending render commands from the GUI thread and calls the renderWave function until the rendering is completed.
    // Only this thread modifies the waveStart and renderState variable.
    if (std::this_thread::get_id() == m_mainThreadID) {
        Error("RenderThread::Run() should not be called from the main thread");
    }

    m_state = Initial;
    m_renderThreadID = std::this_thread::get_id();
    {
        std::lock_guard lock(m_mtxInitialized);
        m_cvInitialized.notify_one();
    }

    while (true) {
        if (!m_autoPlayed || m_waveStart >= m_parent->GetSPP()) {
            // Listen for commands from the GUI
            std::unique_lock lock(m_mtxCmd);
            m_cv.wait(lock, [this] { return m_pendingCmd != None; });
        }

        Command cmd = m_pendingCmd;
        m_pendingCmd = None;
        // Process commands from the GUI
        switch (cmd) {
            case AutoPlay:
                std::cout << "Resuming rendering" << std::endl;
                m_autoPlayed = true;
                break;
            case Pause:
                std::cout << "Pausing rendering" << std::endl;
                m_autoPlayed = false;
                break;
            case Forward:
                std::cout << "Rendering next waves" << std::endl;
                m_autoPlayed = false;
                break;
            case Save:
                std::cout << "Saving rendering" << std::endl;
                m_saveImage(m_waveStart);
                break;
            case Restart:
                std::cout << "Restarting rendering" << std::endl;
                m_waveStart = 0;
                m_state = Initial;
                break;
            case Terminate:
                std::cout << "Terminating rendering" << std::endl;
                m_state = Completed;
                return;
            case None:
                break;
            default:
                Error("Unexpected command \"%s\" in RenderThread", commandNames[cmd].first);
        }
        auto &callback = m_cmdCompleteCallbacks[cmd];
        if (callback && callback())
            continue;

        // Process AutoPlay, Pause, and Forward
        bool renderedSomething = false;
        if (m_autoPlayed) {
            if (m_waveStart < m_parent->GetSPP()) {
                m_state = Rendering;
                m_renderStep(m_waveStart++);
                renderedSomething = true;
            }
        } else if (cmd == Forward) {
            m_forwarding = true;
            m_state = Rendering;
            int wavesLeft = m_forwardWaves;
            while (m_waveStart < m_parent->GetSPP() && wavesLeft-- > 0) {
                // Listen for Pause command
                if (m_pendingCmd == Pause) {
                    std::cout << "Pausing rendering" << std::endl;
                    m_autoPlayed = false;
                    m_pendingCmd = None;
                    auto &callback = m_cmdCompleteCallbacks[Pause];
                    if (callback)
                        callback();
                    break;
                }
                m_renderStep(m_waveStart++);
                renderedSomething = true;
            }
            m_forwarding = false;
        }
        if (renderedSomething && ((Options->writePartialImages && IsPowerOf2(m_waveStart)) || m_waveStart == m_parent->GetSPP())) {
            m_saveImage(m_waveStart);
        }
        m_state = WaveEnd;
    }
}
