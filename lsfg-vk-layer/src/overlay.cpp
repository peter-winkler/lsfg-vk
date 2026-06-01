/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "overlay.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan_core.h>

using namespace lsfgvk::layer;

namespace {
    // standard 7-segment masks; bit0=A(top) 1=B(top-right) 2=C(bottom-right)
    // 3=D(bottom) 4=E(bottom-left) 5=F(top-left) 6=G(middle)
    constexpr std::array<uint8_t, 10> SEG{
        0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
    };
    constexpr uint32_t DW = 18, DH = 30, T = 4; // digit cell width, height, stroke
    constexpr uint32_t ADV = DW + 4;            // horizontal advance per digit
}

Overlay::Overlay(const vk::Vulkan& vk, VkFormat format)
        : bgra(format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB),
          px(static_cast<size_t>(WIDTH) * HEIGHT * 4, 0),
          buffer(vk, px.data(), px.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT) {
    this->clear();
    this->buffer.update(vk, px.data(), px.size());
}

void Overlay::rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
        uint8_t r, uint8_t g, uint8_t b) {
    for (uint32_t j = y; j < y + h && j < HEIGHT; j++)
        for (uint32_t i = x; i < x + w && i < WIDTH; i++) {
            uint8_t* p = &px[(static_cast<size_t>(j) * WIDTH + i) * 4];
            p[0] = this->bgra ? b : r;
            p[1] = g;
            p[2] = this->bgra ? r : b;
            p[3] = 255;
        }
}

void Overlay::clear() {
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = 18; px[i + 1] = 18; px[i + 2] = 18; px[i + 3] = 255;
    }
}

void Overlay::digit(uint32_t x, uint32_t y, int d, uint8_t r, uint8_t g, uint8_t b) {
    if (d < 0 || d > 9)
        return;
    const uint8_t s = SEG.at(static_cast<size_t>(d));
    const uint32_t mid = DH / 2;
    if (s & 0x01) rect(x + T,      y,             DW - 2 * T, T,           r, g, b); // A
    if (s & 0x02) rect(x + DW - T, y + T,         T,          mid - T,     r, g, b); // B
    if (s & 0x04) rect(x + DW - T, y + mid,       T,          mid - T,     r, g, b); // C
    if (s & 0x08) rect(x + T,      y + DH - T,    DW - 2 * T, T,           r, g, b); // D
    if (s & 0x10) rect(x,          y + mid,       T,          mid - T,     r, g, b); // E
    if (s & 0x20) rect(x,          y + T,         T,          mid - T,     r, g, b); // F
    if (s & 0x40) rect(x + T,      y + mid - T / 2, DW - 2 * T, T,         r, g, b); // G
}

void Overlay::number(uint32_t x, uint32_t y, uint32_t v, uint8_t r, uint8_t g, uint8_t b) {
    std::array<int, 8> ds{};
    int n = 0;
    if (v == 0)
        ds.at(static_cast<size_t>(n++)) = 0;
    else
        for (uint32_t t = v; t != 0 && n < 8; t /= 10)
            ds.at(static_cast<size_t>(n++)) = static_cast<int>(t % 10);

    // ds is least-significant first; draw most-significant on the left
    for (int i = 0; i < n; i++)
        digit(x + static_cast<uint32_t>(i) * ADV, y,
            ds.at(static_cast<size_t>(n - 1 - i)), r, g, b);
}

void Overlay::update(const vk::Vulkan& vk, uint32_t baseFps, uint32_t outFps,
        uint32_t prepUs, uint32_t genUs, uint32_t presUs) {
    this->clear();
    // row 1: base fps (white) | output fps (green)
    this->number(6,   6, baseFps, 235, 235, 235);
    this->number(200, 6, outFps,   90, 235,  90);
    // row 2: per-cycle pipeline breakdown in us: prepare (blue) interpolate (red) present (green)
    this->number(6,   46, prepUs, 110, 150, 250);
    this->number(136, 46, genUs,  250,  90,  90);
    this->number(266, 46, presUs,  90, 240, 140);
    this->buffer.update(vk, px.data(), px.size());
}

void Overlay::draw(const vk::Vulkan& vk, const vk::CommandBuffer& cmdbuf,
        VkImage swapchainImage) const {
    const auto barrier = [swapchainImage](VkImageLayout oldL, VkImageLayout newL,
            VkAccessFlags src, VkAccessFlags dst) {
        return VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = src,
            .dstAccessMask = dst,
            .oldLayout = oldL,
            .newLayout = newL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchainImage,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1
            }
        };
    };
    cmdbuf.insertBarriers(vk, { barrier(
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_ACCESS_NONE, VK_ACCESS_TRANSFER_WRITE_BIT) });
    cmdbuf.copyBufferToImageRegion(vk, this->buffer, swapchainImage, 16, 16, WIDTH, HEIGHT);
    cmdbuf.insertBarriers(vk, { barrier(
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_NONE) });
}
