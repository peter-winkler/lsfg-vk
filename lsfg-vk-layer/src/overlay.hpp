/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "lsfg-vk-common/vulkan/buffer.hpp"
#include "lsfg-vk-common/vulkan/command_buffer.hpp"
#include "lsfg-vk-common/vulkan/vulkan.hpp"

#include <cstdint>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace lsfgvk::layer {

    /// a small debug statistics overlay drawn onto the real swapchain corner.
    ///
    /// the offload thread renders the numbers on the CPU (7-segment digits, no font
    /// dependency) into a host-visible buffer and copies it onto each presented frame.
    /// row 1 is base fps (white) and output fps (green); row 2 is the per-cycle pipeline
    /// breakdown in microseconds: prepare (blue), interpolate (red), present (green).
    class Overlay {
    public:
        /// @param vk the device wrapper
        /// @param format the real swapchain format (selects RGBA vs BGRA byte order)
        Overlay(const vk::Vulkan& vk, VkFormat format);

        /// re-render the statistics into the host buffer (called from the report tick)
        void update(const vk::Vulkan& vk, uint32_t baseFps, uint32_t outFps,
            uint32_t prepUs, uint32_t genUs, uint32_t presUs);

        /// record the copy of the overlay onto a swapchain image (assumes PRESENT_SRC in,
        /// leaves PRESENT_SRC out)
        void draw(const vk::Vulkan& vk, const vk::CommandBuffer& cmdbuf,
            VkImage swapchainImage) const;

        static constexpr uint32_t WIDTH = 384;
        static constexpr uint32_t HEIGHT = 84;
    private:
        void clear();
        void rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
            uint8_t r, uint8_t g, uint8_t b);
        void digit(uint32_t x, uint32_t y, int d, uint8_t r, uint8_t g, uint8_t b);
        void number(uint32_t x, uint32_t y, uint32_t v, uint8_t r, uint8_t g, uint8_t b);

        bool bgra;
        std::vector<uint8_t> px;
        vk::Buffer buffer;
    };

}
