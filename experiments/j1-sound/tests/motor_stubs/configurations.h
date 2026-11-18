#pragma once
#include <cstdint>

// 仅替代板级持久配置，控制器和运动规划仍编译实验源文件。
struct TestBoardConfig
{
    bool enableMotorOnBoot = false;
    uint32_t defaultMode = 1;
    int32_t velocityLimit = 30 * 51200;
};
extern TestBoardConfig boardConfig;
