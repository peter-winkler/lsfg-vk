/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "device.hpp"
#include "instance.hpp"
#include "../generator.hpp"
#include "../overlay.hpp"
#include "lsfg-vk-common/helpers/pointers.hpp"
#include "lsfg-vk-common/vulkan/image.hpp"
#include "lsfg-vk-common/vulkan/semaphore.hpp"
#include "lsfg-vk-common/vulkan/timeline_semaphore.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace lsfgvk::layer {

    /// helper struct for partial present information
    struct MyVkPresentInfo {
        uint32_t idx{};
        // VK_KHR_swapchain_maintenance1
        VkFence fence{};
        std::optional<VkPresentModeKHR> present_mode;
        // VK_KHR_present_id(2)
        std::optional<uint64_t> id;
        // CPU pacing: game-thread timestamp (us) when this present was submitted
        uint64_t arrivalUs{};
    };

    /// swapchain wrapper (and virtual) class
    class MyVkSwapchain {
    public:
        /// create a vulkan swapchain wrapper
        /// @param layer layer reference
        /// @param instance parent vulkan instance
        /// @param device parent vulkan device
        /// @param info base swapchain info
        /// @param createFunc function to create the swapchain
        /// @throws ls::vulkan_error on vulkan errors
        MyVkSwapchain(MyVkLayer& layer, MyVkInstance& instance, MyVkDevice& device,
            VkSwapchainCreateInfoKHR info,
            const std::function<VkSwapchainKHR(VkSwapchainCreateInfoKHR*)>& createFunc);
        /// reinitialize the contained swapchain resources
        /// @throws ls::vulkan_error on vulkan errors
        void reinitialize();

        /// get the synchronization pair for presentation
        /// @return semaphore and value pair
        [[nodiscard]] std::pair<VkSemaphore, uint64_t> sync();

        /// reimplement vkGetSwapchainImagesKHR
        VkResult GetSwapchainImagesKHR(uint32_t* count, VkImage* images) noexcept;
        /// reimplement vkAcquireNextImageKHR
        VkResult AcquireNextImageKHR(uint64_t timeout,
            VkSemaphore semaphore, VkFence fence, uint32_t* idx) noexcept;
        /// reimplement vkAcquireNextImage2KHR
        VkResult AcquireNextImage2KHR(const VkAcquireNextImageInfoKHR* info,
            uint32_t* idx) noexcept;
        /// reimplement vkQueuePresentKHR
        VkResult partial_QueuePresentKHR(const MyVkPresentInfo& info) noexcept;
        /// reimplement vkReleaseSwapchainImagesKHR
        VkResult ReleaseSwapchainImagesKHR(const VkReleaseSwapchainImagesInfoKHR* info) noexcept;
        /// reimplement vkWaitForPresentKHR
        VkResult WaitForPresentKHR(uint64_t id, uint64_t timeout) noexcept;
        /// reimplement vkWaitForPresent2KHR
        VkResult WaitForPresent2KHR(const VkPresentWait2InfoKHR* info) noexcept;

        /// get the underlying real swapchain handle
        /// @return real swapchain handle
        [[nodiscard]] auto swapchain() const noexcept { return this->handle; }

        // non-moveable, non-copyable
        MyVkSwapchain(const MyVkSwapchain&) = delete;
        MyVkSwapchain& operator=(const MyVkSwapchain&) = delete;
        MyVkSwapchain(MyVkSwapchain&&) = delete;
        MyVkSwapchain& operator=(MyVkSwapchain&&) = delete;
        ~MyVkSwapchain() noexcept;
    protected: // for use exclusively inside the offload thread
        /// wait for a present from the underlying swapchain, incrementing the counter on success
        /// @param timeout timeout in nanoseconds
        /// @return the optional present information
        /// @throws ls::error on critical failure
        std::optional<MyVkPresentInfo> virtual_FetchUPresent(uint64_t timeout, uint64_t& counter);

        /// acquire a swapchain image from the virtual swapchain
        /// @param semaphore semaphore to signal when the image is available
        /// @return the index of the acquired image
        /// @throws ls::error on critical failure
        uint32_t virtual_AcquireNext(const vk::Semaphore& semaphore);

        /// present an image to the virtual swapchain linked to the original present call
        /// @param original_info original present information
        /// @param semaphore semaphore to wait on before presenting
        /// @param idx index of the image to present
        /// @throws ls::error on critical failure
        void virtual_PresentLinked(const MyVkPresentInfo& original_info,
            const vk::Semaphore& semaphore, uint32_t idx, uint64_t presentTimeNs);

        /// mark a present from the underlying swapchain as complete
        /// @param info present information
        void virtual_CompleteUPresent(const MyVkPresentInfo& info);
    private:
        ls::R<MyVkLayer> layer;
        ls::R<MyVkInstance> instance;
        ls::R<MyVkDevice> device;

        ls::lazy<Generator> generator; // frame generation driver, runs on the offload thread

        ls::lazy<Overlay> overlay;     // debug statistics overlay (LSFGVK_OVERLAY)
        bool overlayActive{false};

        vk::TimelineSemaphore presentSemaphore;
        uint64_t presentIndex;

        std::mutex swapchainMutex;
        VkSwapchainKHR handle; // from real swapchain
        std::vector<VkImage> swapchainImages;

        // VK_EXT_present_timing state (set when the profile selects present-timing pacing)
        bool presentTimingActive{false};
        uint64_t timeDomainId{0};
        VkPresentStageFlagsEXT timingStage{0}; // present stage used for targets + feedback
        uint64_t nextPsl{0};           // next present-stage-local target (ns)
        uint64_t lastAchievedPsl{0};   // latest achieved present time, present-stage-local (ns)
        uint64_t lastAchievedId{0};    // present id of the latest achieved present (for the lead)
        uint64_t prevAchievedPsl{0};   // previous achieved present time, for jitter stats
        double pacingSum{0.0};         // achieved-interval accumulators, reported every 2s
        double pacingSumSq{0.0};
        uint64_t pacingCount{0};
        uint64_t ptPresentId{0};       // incrementing id to key the timing results queue
        bool ptTargeting{false};       // experimental: drive presents via absolute targets
        uint64_t baseCapNextUs{0};     // test-only: LSFGVK_BASE_CAP game-rate throttle state

        std::vector<vk::Image> images; // virtual swapchain images
        std::mutex availabilityMutex;
        std::vector<bool> availableImages;
        //std::mutex presentationMutex; -- should not be necessary!
        std::queue<MyVkPresentInfo> presents;
        ls::lazy<vk::TimelineSemaphore> doneSemaphore;

        std::atomic_bool running{true};
        std::atomic<VkResult> status{VK_SUCCESS};
        std::thread thread;
        void thread_main() noexcept;

        /// configure VK_EXT_present_timing on the real swapchain (queue size + time domain)
        void setupPresentTiming() noexcept;
        /// drain achieved present timings and advance the pacing anchor + jitter stats
        void drainPresentTiming() noexcept;
    };

}
