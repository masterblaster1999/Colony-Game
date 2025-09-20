// SingleInstance.h
#pragma once
namespace app::single_instance {
    // returns false if another instance is already running
    bool acquire();
}
