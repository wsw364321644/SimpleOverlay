#include "game_capture.h"
#include <INIReader.h>
#include <chrono>
#include <HOOK/hook_info.h>

uint64_t get_default_frame_interval()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>( std::chrono::seconds(1)).count()/60;
}
bool load_offsets_from_string(graphics_offsets_t* offsets, const char* str)
{
    auto reader = INIReader(str, strlen(str));
    offsets->d3d8.present = reader.GetInteger("d3d8", "present", 0);

    offsets->d3d9.present = reader.GetInteger("d3d9", "present", 0);
    offsets->d3d9.present_ex = reader.GetInteger("d3d9", "present_ex", 0);
    offsets->d3d9.present_swap = reader.GetInteger("d3d9", "present_swap", 0);
    offsets->d3d9.d3d9_clsoff = reader.GetInteger("d3d9", "d3d9_clsoff", 0);
    offsets->d3d9.is_d3d9ex_clsoff = reader.GetInteger("d3d9", "is_d3d9ex_clsoff", 0);

    offsets->dxgi.present = reader.GetInteger("dxgi", "present", 0);
    offsets->dxgi.present1 = reader.GetInteger("dxgi", "present1", 0);
    offsets->dxgi.resize = reader.GetInteger("dxgi", "resize", 0);
    offsets->dxgi2.release = reader.GetInteger("dxgi", "release", 0);

    return true;
}
