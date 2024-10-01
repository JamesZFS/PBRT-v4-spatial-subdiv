//
// Created by fengshi on 9/30/24.
//

#ifndef RENDERTHREAD_H
#define RENDERTHREAD_H

#include <functional>
#include <thread>

class RenderThread {
public:
    RenderThread(int spp, const std::function<void(int waveStart)> &renderStep,
                 const std::function<void(int waveEnd)> &saveImage);

    enum RendererState {
        Initial = 0,
        Rendering,
        WaveEnd,
        Completed
    };

    enum Command {
        // Control buttons begin
        AutoPlay = 0,
        Pause,
        Forward,
        Save,
        Restart,
        // Control buttons end
        Terminate,
        None,
        CmdCount,
    };

    // Called by the GUI thread
    void SetCmdCallback(Command cmd, const std::function<bool()> &callback);

    void SendCommand(Command cmd);

    void Join();

    std::thread::id GetMainThreadID() const { return m_mainThreadID; }

    std::thread::id GetRenderThreadID() const { return m_renderThreadID; }

    RendererState GetState() const { return m_state; }

    int GetWaveStart() const { return m_waveStart; }

    bool IsAutoPlayed() const { return m_autoPlayed; }

    int m_forwardWaves = 1;

private:
    void Run();

    Command m_pendingCmd = None;
    RendererState m_state = Initial;
    int m_waveStart = 0;
    const int m_spp;
    const std::thread::id m_mainThreadID;
    std::thread::id m_renderThreadID;
    std::thread m_thread;
    std::mutex m_mtxInitialized;
    std::mutex m_mtxCmd;
    std::condition_variable m_cvInitialized; // for waiting for initialization
    std::condition_variable m_cv; // for waiting for commands

    std::vector<std::function<bool()> > m_cmdCompleteCallbacks;
    std::function<void(int waveStart)> m_renderStep;
    std::function<void(int waveEnd)> m_saveImage;

    bool m_autoPlayed = false;
};

extern const std::vector<std::pair<const char *, const char *>> commandNames;

#endif //RENDERTHREAD_H
