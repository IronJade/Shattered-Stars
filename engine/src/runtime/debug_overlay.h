#pragma once

#include <deque>
#include <string>
#include <vector>

#include "core/types.h"

// The debug console's in-window overlay (port feature, only with --debug): the status
// line and the console's last output lines over the top of the picture, in a 5x7 pixel
// font, on a darkened band. Drawn into the displayed (and captured) RGBA image.
namespace ss::runtime {

// Draws 'status' and 'lines' (oldest first) into 'pixels' (width x height, RGBA as
// Gpu::display_to_rgba makes it). The text scale follows the width (1 up to 400 pixels).
void draw_debug_overlay(std::vector<u32>& pixels, int width, int height, const std::string& status,
                        const std::deque<std::string>& lines);

}  // namespace ss::runtime
