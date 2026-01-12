#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

#include "Labs/WordCloud/WordEntity.h"
#include "Labs/WordCloud/PhysicsSimulator.h"

namespace VCX::Labs::labf {

    /**
     * 物理模拟线程（双缓冲 + 命令队列模式）
     *
     * 渲染线程通过 GetReadBuffer() 零拷贝获取当前状态。
     * 交互通过命令队列异步通知物理线程。
     */
    class PhysicsThread {
    public:
        // 命令类型
        struct Command {
            enum Type {
                UpdatePositionDelta,// 更新单个词位置（增量）
                UpdateHighlight,    // 更新高亮状态
                UpdateOrientation,  // 更新单个词角度（绝对值）
                UpdateOrientationDelta, // 更新单个词角度（增量）
                UpdateWordOBB,      // 更新词的 OBB 信息
                UpdateColor,        // 更新颜色
                AddWord,            // 添加词
                RemoveWord,         // 删除词
                SetParams,          // 设置物理参数
                Reset               // 重置模拟器
            };
            Type type;
            size_t index = 0;
            glm::vec2 position { 0, 0 };
            float orientation = 0.0f;
            bool highlighted = false;
            glm::vec4 color { 1, 1, 1, 1 };
            WordEntity word;
            PhysicsParams params;

            // OBB 更新信息
            float fontSize = 0.0f;
            glm::vec2 boxHalfSize { 0, 0 };
            bool useTwoLevelBox = false;
            float xHeight = 0.0f;
            float xHeightCenterY = 0.0f;  // x-height 区域中心
            std::vector<LetterOBB> letterOBBs;  // 字符级 OBB
        };

        PhysicsThread() = default;
        ~PhysicsThread() { Stop(); }

        // 禁止拷贝
        PhysicsThread(PhysicsThread const&) = delete;
        PhysicsThread& operator=(PhysicsThread const&) = delete;

        // 启动物理线程
        void Start(std::vector<WordEntity> const& initialWords, PhysicsParams const& params) {
            if (_running.load()) return;

            // 初始化双缓冲
            _bufferA = initialWords;
            _bufferB = initialWords;
            _readBuffer.store(&_bufferA, std::memory_order_release);
            _writeBuffer = &_bufferB;
            _params = params;
            _simulator.Reset();

            // 清空命令队列
            {
                std::lock_guard<std::mutex> lock(_commandMutex);
                _commandQueue.clear();
            }

            _running.store(true);
            _thread = std::thread(&PhysicsThread::ThreadLoop, this);
        }

        // 停止物理线程
        void Stop() {
            if (!_running.load()) return;

            _running.store(false);

            if (_thread.joinable()) {
                _thread.join();
            }
        }

        // 检查是否正在运行
        bool IsRunning() const { return _running.load(); }

        // 获取当前可读缓冲区（零拷贝）
        // 注意：返回的引用在下一次 SwapBuffers 之前有效
        // 由于渲染帧率通常低于物理帧率，这是安全的
        std::vector<WordEntity> const& GetReadBuffer() const {
            return *_readBuffer.load(std::memory_order_acquire);
        }

        // === 命令接口（异步，不阻塞） ===

        // 使用增量更新单个词的位置（用于拖拽，避免竞态条件）
        void UpdateWordPositionDelta(size_t index, glm::vec2 delta) {
            Command cmd;
            cmd.type = Command::UpdatePositionDelta;
            cmd.index = index;
            cmd.position = delta;  // 存储增量
            EnqueueCommand(std::move(cmd));
        }

        // 更新词的高亮状态
        void UpdateWordHighlight(size_t index, bool highlighted) {
            Command cmd;
            cmd.type = Command::UpdateHighlight;
            cmd.index = index;
            cmd.highlighted = highlighted;
            EnqueueCommand(std::move(cmd));
        }

        // 更新词的角度（绝对值）
        void UpdateWordOrientation(size_t index, float orientation) {
            Command cmd;
            cmd.type = Command::UpdateOrientation;
            cmd.index = index;
            cmd.orientation = orientation;
            EnqueueCommand(std::move(cmd));
        }

        // 更新词的角度（增量，用于拖拽旋转，避免竞态条件）
        void UpdateWordOrientationDelta(size_t index, float delta) {
            Command cmd;
            cmd.type = Command::UpdateOrientationDelta;
            cmd.index = index;
            cmd.orientation = delta;  // 存储增量
            EnqueueCommand(std::move(cmd));
        }

        // 更新词的 OBB 信息（统一接口）
        void UpdateWordOBB(size_t index, WordEntity const& w) {
            Command cmd;
            cmd.type = Command::UpdateWordOBB;
            cmd.index = index;
            cmd.fontSize = w.fontSize;
            cmd.boxHalfSize = w.boxHalfSize;
            cmd.useTwoLevelBox = w.useTwoLevelBox;
            cmd.xHeight = w.xHeight;
            cmd.xHeightCenterY = w.xHeightCenterY;
            cmd.letterOBBs = w.letterOBBs;  // 传递字符级 OBB
            EnqueueCommand(std::move(cmd));
        }

        // 更新词的颜色
        void UpdateWordColor(size_t index, glm::vec4 color) {
            Command cmd;
            cmd.type = Command::UpdateColor;
            cmd.index = index;
            cmd.color = color;
            EnqueueCommand(std::move(cmd));
        }

        // 添加词
        void AddWord(WordEntity const& word) {
            Command cmd;
            cmd.type = Command::AddWord;
            cmd.word = word;
            EnqueueCommand(std::move(cmd));
        }

        // 删除词
        void RemoveWord(size_t index) {
            Command cmd;
            cmd.type = Command::RemoveWord;
            cmd.index = index;
            EnqueueCommand(std::move(cmd));
        }

        // 更新参数
        void SetParams(PhysicsParams const& params) {
            Command cmd;
            cmd.type = Command::SetParams;
            cmd.params = params;
            EnqueueCommand(std::move(cmd));
        }

        // 重置模拟器
        void ResetSimulator() {
            Command cmd;
            cmd.type = Command::Reset;
            EnqueueCommand(std::move(cmd));
        }

    private:
        void EnqueueCommand(Command&& cmd) {
            std::lock_guard<std::mutex> lock(_commandMutex);
            _commandQueue.push_back(std::move(cmd));
        }

        void ProcessCommands() {
            // 取出所有命令
            std::vector<Command> commands;
            {
                std::lock_guard<std::mutex> lock(_commandMutex);
                commands = std::move(_commandQueue);
                _commandQueue.clear();
            }

            // 在写入缓冲区上执行命令
            for (auto& cmd : commands) {
                switch (cmd.type) {
                case Command::UpdatePositionDelta:
                    if (cmd.index < _writeBuffer->size()) {
                        (*_writeBuffer)[cmd.index].position += cmd.position;  // cmd.position 存储的是增量
                    }
                    break;

                case Command::UpdateHighlight:
                    if (cmd.index < _writeBuffer->size()) {
                        auto& w = (*_writeBuffer)[cmd.index];
                        w.isHighlighted = cmd.highlighted;
                        if (!cmd.highlighted) {
                            w.velocity = glm::vec2(0.0f);
                        }
                    }
                    break;

                case Command::UpdateOrientation:
                    if (cmd.index < _writeBuffer->size()) {
                        (*_writeBuffer)[cmd.index].orientation = cmd.orientation;
                    }
                    break;

                case Command::UpdateOrientationDelta:
                    if (cmd.index < _writeBuffer->size()) {
                        auto& w = (*_writeBuffer)[cmd.index];
                        w.orientation += cmd.orientation;  // cmd.orientation 存储的是增量
                        // 规范化到 [-180, 180]
                        while (w.orientation > 180.0f) w.orientation -= 360.0f;
                        while (w.orientation < -180.0f) w.orientation += 360.0f;
                    }
                    break;

                case Command::UpdateWordOBB:
                    if (cmd.index < _writeBuffer->size()) {
                        auto& w = (*_writeBuffer)[cmd.index];
                        w.fontSize = cmd.fontSize;
                        w.boxHalfSize = cmd.boxHalfSize;
                        w.useTwoLevelBox = cmd.useTwoLevelBox;
                        w.xHeight = cmd.xHeight;
                        w.xHeightCenterY = cmd.xHeightCenterY;
                        w.letterOBBs = std::move(cmd.letterOBBs);  // 传递字符级 OBB
                        // 更新 wordLevelOBB 的 halfSize（center 和 rotation 在碰撞检测时动态计算）
                        if (w.useTwoLevelBox) {
                            w.wordLevelOBB.halfSize = glm::vec2(w.boxHalfSize.x, w.xHeight * 0.5f);
                        }
                        w.updateMassFromArea();
                    }
                    break;

                case Command::UpdateColor:
                    if (cmd.index < _writeBuffer->size()) {
                        (*_writeBuffer)[cmd.index].color = cmd.color;
                    }
                    break;

                case Command::AddWord:
                    _writeBuffer->push_back(cmd.word);
                    break;

                case Command::RemoveWord:
                    if (cmd.index < _writeBuffer->size()) {
                        _writeBuffer->erase(_writeBuffer->begin() + cmd.index);
                    }
                    break;

                case Command::SetParams:
                    _params = cmd.params;
                    break;

                case Command::Reset:
                    _simulator.Reset();
                    for (auto& w : *_writeBuffer) {
                        w.velocity = glm::vec2(0.0f);
                        w.clearAccumulators();
                    }
                    break;
                }
            }
        }

        void SwapBuffers() {
            // 将当前写入缓冲区设为可读
            _readBuffer.store(_writeBuffer, std::memory_order_release);

            // 交换写入目标
            _writeBuffer = (_writeBuffer == &_bufferA) ? &_bufferB : &_bufferA;

            // 从当前可读缓冲区拷贝状态到新的写入缓冲区
            // 这保证物理模拟的连续性
            *_writeBuffer = *_readBuffer.load(std::memory_order_acquire);
        }

        void ThreadLoop() {
            while (_running.load()) {
                // 1. 处理命令队列
                ProcessCommands();

                // 2. 执行物理模拟（使用固定时间步）
                _simulator.Update(*_writeBuffer, _params.fixedDt, _params);

                // 3. 交换缓冲区
                SwapBuffers();

                // 4. 按固定时间步休眠
                auto sleepTime = std::chrono::duration<float>(_params.fixedDt);
                std::this_thread::sleep_for(std::chrono::duration_cast<std::chrono::microseconds>(sleepTime));
            }
        }

        // 线程
        std::thread _thread;
        std::atomic<bool> _running { false };

        // 双缓冲
        std::vector<WordEntity> _bufferA;
        std::vector<WordEntity> _bufferB;
        std::atomic<std::vector<WordEntity>*> _readBuffer { &_bufferA };
        std::vector<WordEntity>* _writeBuffer { &_bufferB };

        // 命令队列
        std::mutex _commandMutex;
        std::vector<Command> _commandQueue;

        // 物理模拟
        PhysicsParams _params;
        PhysicsSimulator _simulator;
    };

} // namespace VCX::Labs::labf
