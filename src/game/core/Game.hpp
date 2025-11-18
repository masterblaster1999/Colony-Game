#pragma once

#include <memory>
#include <taskflow/taskflow.hpp>   // make tf::Executor / tf::Taskflow complete

class Game {
public:
    Game();
    ~Game();   // can be defaulted inline if you want

    // ...

private:
    std::unique_ptr<tf::Executor> m_executor;
    std::unique_ptr<tf::Taskflow> m_taskflow;
};
