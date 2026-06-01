/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "device.hpp"
#include "instance.hpp"
#include "layer.hpp"
#include "lsfg-vk-common/helpers/errors.hpp"
#include "lsfg-vk-common/helpers/pointers.hpp"
#include "lsfg-vk-common/vulkan/command_buffer.hpp"
#include "lsfg-vk-common/vulkan/fence.hpp"
#include "lsfg-vk-common/vulkan/semaphore.hpp"
#include "lsfg-vk-common/vulkan/timeline_semaphore.hpp"
#include "lsfg-vk-common/vulkan/vulkan.hpp"
#include "swapchain.hpp"

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <time.h>
#include <unistd.h>
#include <bits/time.h>
#include <vulkan/vulkan_core.h>

using namespace lsfgvk;
using namespace lsfgvk::layer;

namespace {
    /// helper to get swapchain images
    std::vector<VkImage> getSwapchainImages(const vk::Vulkan& vk, VkSwapchainKHR swapchain) {
        uint32_t imageCount = 0;
        auto res = vk.df().GetSwapchainImagesKHR(vk.dev(), swapchain, &imageCount, nullptr);
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkGetSwapchainImagesKHR() failed to get image count");

        std::vector<VkImage> images(imageCount);
        res = vk.df().GetSwapchainImagesKHR(vk.dev(), swapchain, &imageCount, images.data());
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkGetSwapchainImagesKHR() failed to get images");

        return images;
    }

    /// current time in microseconds (CLOCK_MONOTONIC)
    uint64_t nowInUs() noexcept {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);

        return static_cast<uint64_t>(ts.tv_sec) * 1'000'000
            + static_cast<uint64_t>(ts.tv_nsec) / 1'000;
    }

    /// sleep until an absolute CLOCK_MONOTONIC time (microseconds)
    void sleepUntilUs(uint64_t targetUs) noexcept {
        const timespec ts{
            .tv_sec = static_cast<time_t>(targetUs / 1'000'000),
            .tv_nsec = static_cast<long>((targetUs % 1'000'000) * 1'000)
        };
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
    }
}

MyVkSwapchain::MyVkSwapchain(MyVkLayer& layer, MyVkInstance& instance, MyVkDevice& device,
            VkSwapchainCreateInfoKHR info,
            const std::function<VkSwapchainKHR(VkSwapchainCreateInfoKHR*)>& createFunc) :
        layer(std::ref(layer)), instance(std::ref(instance)), device(std::ref(device)),
        presentSemaphore(device.vkd(), 0), presentIndex(1) {
    const auto& vk = device.vkd();

    // ensure underlying swapchain supports transfer operations
    info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    // check for VK_KHR_swapchain_mutable_format
    VkImageCreateFlags flags{};
    if (info.flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR)
        flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    const auto* chain = ls::find_structure<VkImageFormatListCreateInfo>(
        VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        info.pNext
    );

    // when the profile selects present-timing pacing and the device enabled the
    // extension, create the real swapchain with the timing bit so the offload thread
    // can target per-frame present times instead of CPU-sleeping
    const bool wantPresentTiming =
        layer.profile().pacing == ls::Pacing::PresentTiming
        && vk.df().SetSwapchainPresentTimingQueueSizeEXT != nullptr;
    if (wantPresentTiming)
        info.flags |= VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT
            | VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR;
    // experiment: force the real swapchain to MAILBOX so timed targets aren't fighting
    // FIFO's vsync ordering (present-timing only adds value on uncapped present modes)
    if (wantPresentTiming && std::getenv("LSFGVK_PT_MAILBOX") != nullptr)
        info.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;

    // create underlying swapchain
    this->handle = createFunc(&info);
    this->swapchainImages = getSwapchainImages(vk, this->handle);
    if (wantPresentTiming)
        this->setupPresentTiming();

    // create virtual swapchain images
    this->images.reserve(this->swapchainImages.size());
    this->availableImages = std::vector<bool>(this->swapchainImages.size(), true);
    for (size_t i = 0; i < this->swapchainImages.size(); i++) {
        this->images.emplace_back(vk,
            info.imageExtent,
            info.imageFormat,
            info.imageUsage,
            std::nullopt, std::nullopt,
            flags,
            reinterpret_cast<const void*>(chain)
        );
    }

    // create the frame generation driver, then the offload thread that drives it
    this->generator.emplace(layer, device, info.imageExtent, info.imageFormat);

    this->doneSemaphore.emplace(vk, 0);
    this->thread = std::thread(&MyVkSwapchain::thread_main, this);

    if (std::getenv("LSFGVK_DEBUG") != nullptr)
        std::cerr << "lsfg-vk: virtual swapchain created " << info.imageExtent.width << "x"
            << info.imageExtent.height << " (multiplier " << layer.profile().multiplier
            << ", " << this->images.size() << " images)\n";

    // this->reinitialize();
}

// void MyVkSwapchain::reinitialize() {
//     // ...
// }

void MyVkSwapchain::setupPresentTiming() noexcept {
    const auto& vk = this->device.get().vkd();
    const auto& df = vk.df();

    // a results queue is required to read achieved present times back for the pacing loop
    df.SetSwapchainPresentTimingQueueSizeEXT(vk.dev(), this->handle, 16);

    // RADV exposes the present-stage-local domain for targeting. We can't read "now" in
    // that opaque domain directly, so we run a closed loop: request feedback, read the
    // achieved present times back in this same domain, and anchor future targets to them.
    uint64_t counter{0};
    VkSwapchainTimeDomainPropertiesEXT props{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_TIME_DOMAIN_PROPERTIES_EXT
    };
    df.GetSwapchainTimeDomainPropertiesEXT(vk.dev(), this->handle, &props, &counter);

    const uint32_t count = props.timeDomainCount;
    std::vector<VkTimeDomainKHR> domains(count);
    std::vector<uint64_t> ids(count);
    props.pTimeDomains = domains.data();
    props.pTimeDomainIds = ids.data();
    df.GetSwapchainTimeDomainPropertiesEXT(vk.dev(), this->handle, &props, &counter);

    bool found = false;
    for (uint32_t i = 0; i < count; i++)
        if (domains[i] == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT) {
            this->timeDomainId = ids[i];
            found = true;
            break;
        }
    if (!found && count > 0) { // fall back to whatever single domain the driver offers
        this->timeDomainId = ids[0];
        found = true;
    }

    // pace to first-pixel-out (scanout); this is what Wayland presentation feedback
    // reports, whereas first-pixel-visible is typically unknown (reported as zero)
    this->timingStage = VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT;
    this->presentTimingActive = found;

    // Driving presents via absolute targets currently destabilises FIFO swapchains on
    // RADV's young present-timing path (rapid out-of-date recreation), and FIFO is
    // already vsync-paced, so by default we only *measure* via the feedback loop and let
    // CPU pacing place the frames. The targeting path is kept behind an opt-in env var.
    this->ptTargeting = std::getenv("LSFGVK_PT_EXPERIMENTAL_TARGET") != nullptr;

    if (std::getenv("LSFGVK_DEBUG") != nullptr)
        std::cerr << "lsfg-vk: present timing " << (found ? "active" : "inactive")
            << (found ? (this->ptTargeting ? ", experimental targeting ON" : ", measuring (CPU-paced)") : "")
            << " (" << count << " domains, id " << this->timeDomainId << ")\n";
}

void MyVkSwapchain::drainPresentTiming() noexcept {
    const auto& vk = this->device.get().vkd();
    const auto& df = vk.df();

    const VkPastPresentationTimingInfoEXT info{
        .sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_INFO_EXT,
        .swapchain = this->handle
    };
    VkPastPresentationTimingPropertiesEXT props{
        .sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_PROPERTIES_EXT
    };
    if (df.GetPastPresentationTimingEXT(vk.dev(), &info, &props) != VK_SUCCESS)
        return;
    const uint32_t count = props.presentationTimingCount;
    if (count == 0)
        return;

    // allocate up to four stage slots per result; the driver fills the stages it knows.
    // We pace to first-pixel-out (scanout), falling back to any other non-zero stage.
    constexpr uint32_t kMaxStages = 4;
    std::vector<VkPastPresentationTimingEXT> timings(count);
    std::vector<VkPresentStageTimeEXT> stages(static_cast<size_t>(count) * kMaxStages);
    for (uint32_t i = 0; i < count; i++)
        timings[i] = VkPastPresentationTimingEXT{
            .sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_EXT,
            .presentStageCount = kMaxStages,
            .pPresentStages = &stages[static_cast<size_t>(i) * kMaxStages]
        };
    props.presentationTimingCount = count;
    props.pPresentationTimings = timings.data();
    if (df.GetPastPresentationTimingEXT(vk.dev(), &info, &props) != VK_SUCCESS)
        return;

    for (uint32_t i = 0; i < props.presentationTimingCount; i++) {
        const auto& timing = timings[i];
        if (!timing.reportComplete)
            continue;

        uint64_t achieved = 0;
        uint64_t fallback = 0;
        for (uint32_t s = 0; s < timing.presentStageCount; s++) {
            const auto& st = timing.pPresentStages[s];
            if (st.time == 0)
                continue;
            if (st.stage == VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT)
                achieved = st.time;
            else if (st.time > fallback)
                fallback = st.time;
        }
        if (achieved == 0)
            achieved = fallback;
        if (achieved == 0 || achieved <= this->lastAchievedPsl)
            continue; // unavailable, out of order, or duplicate

        if (this->prevAchievedPsl != 0) {
            const double delta = static_cast<double>(achieved - this->prevAchievedPsl);
            this->pacingSum += delta;
            this->pacingSumSq += delta * delta;
            this->pacingCount++;
        }
        this->prevAchievedPsl = achieved;
        this->lastAchievedPsl = achieved;
        this->lastAchievedId = timing.presentId;
    }
}

MyVkSwapchain::~MyVkSwapchain() noexcept {
    this->running.store(false);
    if (this->thread.joinable())
        this->thread.join();
}

/* Virtual swapchain logic */

std::pair<VkSemaphore, uint64_t> MyVkSwapchain::sync() {
    return { this->presentSemaphore.handle(), this->presentIndex++ };
}

#define FILL_BARRIER(handle) \
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, \
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, \
    .image = (handle), \
    .subresourceRange = { \
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, \
        .levelCount = 1, \
        .layerCount = 1 \
    }

void MyVkSwapchain::thread_main() noexcept {
    const auto& vk = this->device.get().vkd();
    auto& offload = this->device.get().offload();
    auto& gen = this->generator.mut();
    const uint64_t genCount = gen.count(); // max generated frames per cycle (ceil(R)-1)
    const double multiplier = this->layer.get().profile().multiplier; // configured max (cap)
    const double targetFps = this->layer.get().profile().target_fps;  // 0 = fixed multiplier
    double debt{0.0}; // fractional-frame debt accumulator

    struct Pass {
        vk::Semaphore acquireSemaphore;
        vk::CommandBuffer commandBuffer;
        vk::Fence copyFence;
        vk::Semaphore presentSemaphore;
    };

    std::vector<Pass> passes;
    const size_t passCount = this->swapchainImages.size() + genCount + 2;
    passes.reserve(passCount);
    for (size_t i = 0; i < passCount; i++) {
        passes.emplace_back(Pass {
            .acquireSemaphore = vk::Semaphore(vk),
            .commandBuffer = vk::CommandBuffer(vk),
            .copyFence = vk::Fence(vk),
            .presentSemaphore = vk::Semaphore(vk)
        });
    }

    uint64_t passIdx{0};

    // acquire a real swapchain image, record a blit onto it (recordBlit returns the
    // timeline semaphore + value the blit must wait on, or {VK_NULL_HANDLE, 0}),
    // present it linked to the original present, then block until the copy completes
    const auto present = [&](const auto& recordBlit, const MyVkPresentInfo& linked,
            uint64_t targetUs, uint64_t presentTimeNs) {
        auto& pass = passes[passIdx++ % passes.size()];
        const uint32_t real_idx = this->virtual_AcquireNext(pass.acquireSemaphore);
        auto& swapchainImage = this->swapchainImages.at(real_idx);

        auto& cmdbuf = pass.commandBuffer;
        cmdbuf.begin(vk);
        const auto [waitSem, waitVal] = recordBlit(cmdbuf, swapchainImage);
        cmdbuf.end(vk);

        {
            const std::scoped_lock<std::mutex> lock(offload.mutex);
            cmdbuf.submit(vk,
                { pass.acquireSemaphore.handle() }, waitSem, waitVal,
                { pass.presentSemaphore.handle() }, VK_NULL_HANDLE, 0,
                pass.copyFence.handle(), offload.queue
            );
        }

        // pace the present: CPU pacing sleeps until targetUs (the copy overlaps the
        // sleep); present-timing instead hands the present an absolute target (presentTimeNs)
        if (targetUs)
            sleepUntilUs(targetUs);
        this->virtual_PresentLinked(linked, pass.presentSemaphore, real_idx, presentTimeNs);

        if (!pass.copyFence.wait(vk, UINT64_MAX))
            throw ls::error("virtual swapchain copy fence wait timed out");
        pass.copyFence.reset(vk);
    };

    // blit recorder for the real (game-rendered) frame: virtual image -> real image
    const auto recordRealFrame = [&](VkImage virtualImage)
            -> std::function<std::pair<VkSemaphore, uint64_t>(vk::CommandBuffer&, VkImage)> {
        return [&vk, virtualImage, &gen](vk::CommandBuffer& cmdbuf, VkImage swapchainImage)
                -> std::pair<VkSemaphore, uint64_t> {
            cmdbuf.blitImage(vk,
                {
                    {
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                        .srcAccessMask = VK_ACCESS_NONE,
                        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                        .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        FILL_BARRIER(virtualImage)
                    },
                    {
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                        .srcAccessMask = VK_ACCESS_NONE,
                        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        FILL_BARRIER(swapchainImage)
                    },
                },
                { virtualImage, swapchainImage },
                gen.sourceExtent(),
                {
                    {
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
                        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        FILL_BARRIER(swapchainImage)
                    }
                }
            );
            return { VK_NULL_HANDLE, 0 };
        };
    };

    // present-timing measures via its feedback loop but, unless experimental targeting is
    // enabled and active, places frames with CPU pacing, so the mode is never worse than
    // CPU pacing
    const auto pacingMode = this->layer.get().profile().pacing;
    const bool useTargeting = this->presentTimingActive && this->ptTargeting;
    const bool pace = pacingMode == ls::Pacing::CPU
        || (pacingMode == ls::Pacing::PresentTiming && !useTargeting);
    double intervalUs{0};       // smoothed game-frame interval
    uint64_t lastArrivalUs{0};  // previous game present timestamp
    uint64_t nextUs{0};         // running present anchor for CPU pacing

    // LSFGVK_DEBUG enables a periodic framerate report (base game rate vs generated
    // output rate) on stderr; capture it with PROTON_LOG=1 or a launch-option wrapper.
    const bool logFps = std::getenv("LSFGVK_DEBUG") != nullptr;
    uint64_t logUs{0}, realFrames{0}, genFrames{0};
    if (logFps)
        std::cerr << "lsfg-vk: offload thread started (max multiplier " << multiplier
            << (targetFps > 0.0 ? ", adaptive)\n" : ")\n");
    bool firstPresent = true;

    try {
        uint64_t counter{1};
        while (this->running.load()) {
            // wait for the next rendered frame
            const auto ppi = this->virtual_FetchUPresent(100'1000, counter);
            if (!ppi.has_value())
                continue; // timeout after 100us

            if (logFps && firstPresent) {
                firstPresent = false;
                std::cerr << "lsfg-vk: offload thread received first present\n";
            }

            auto& virtualImage = this->images.at(ppi->idx);

            // load the freshly rendered frame as a generation source
            {
                auto& pass = passes[passIdx++ % passes.size()];
                auto& cmdbuf = pass.commandBuffer;
                cmdbuf.begin(vk);
                const auto [sem, val] = gen.prepare(cmdbuf, virtualImage.handle());
                cmdbuf.end(vk);

                {
                    const std::scoped_lock<std::mutex> lock(offload.mutex);
                    cmdbuf.submit(vk, {}, VK_NULL_HANDLE, 0, {}, sem, val,
                        pass.copyFence.handle(), offload.queue);
                }

                if (!pass.copyFence.wait(vk, UINT64_MAX))
                    throw ls::error("virtual swapchain prepare fence wait timed out");
                pass.copyFence.reset(vk);
            }

            // update the smoothed game-frame interval from the game-thread timestamp
            if (lastArrivalUs != 0 && ppi->arrivalUs > lastArrivalUs) {
                const double measured = static_cast<double>(ppi->arrivalUs - lastArrivalUs);
                intervalUs = (intervalUs == 0) ? measured : (intervalUs * 0.8 + measured * 0.2);
            }
            lastArrivalUs = ppi->arrivalUs;

            // pick this cycle's multiplier: fixed, or adaptive toward target_fps
            // (rate = target_fps / base_fps), clamped to [1, configured max]
            double rate = multiplier;
            if (targetFps > 0.0 && intervalUs > 0.0) {
                rate = targetFps * intervalUs / 1'000'000.0;
                if (rate < 1.0) rate = 1.0;
                if (rate > multiplier) rate = multiplier;
            }

            // accumulate fractional debt into an integer generated-frame count this cycle:
            // e.g. rate 1.5 yields frames = 0,1,0,1,... averaging 0.5 extra per real frame
            debt += rate - 1.0;
            const uint64_t frames = static_cast<uint64_t>(debt);
            debt -= static_cast<double>(frames);

            // interpolate between the two most recent sources
            gen.schedule(frames);

            // running present anchor, kept within [now, now + interval] to bound drift
            const double stepUs = intervalUs / static_cast<double>(frames + 1);
            const uint64_t stepNs = static_cast<uint64_t>(stepUs * 1000.0);
            const uint64_t now = nowInUs();
            if (nextUs < now || nextUs > now + static_cast<uint64_t>(intervalUs))
                nextUs = now;
            // CPU pacing anchor: absolute monotonic schedule (us), 0 until an interval is known
            const auto nextSchedule = [&]() -> uint64_t {
                if (intervalUs == 0)
                    return 0;
                const uint64_t target = nextUs;
                nextUs += static_cast<uint64_t>(stepUs);
                return target;
            };

            // present-timing closed loop: read achieved present times and keep the
            // present-stage-local target a few frames ahead of the latest one. This both
            // bootstraps the opaque domain's clock and corrects drift every cycle.
            if (this->presentTimingActive) {
                this->drainPresentTiming();
                if (this->lastAchievedPsl != 0) {
                    // project from the most recent *confirmed* present to the next one:
                    // achieved time + (frames still in flight + 1) steps. Using the present
                    // id correlation lands targets in the real future instead of the past.
                    uint64_t inFlight = this->ptPresentId - this->lastAchievedId;
                    if (inFlight > 16) inFlight = 16; // guard against a stalled feedback queue
                    this->nextPsl = this->lastAchievedPsl + (inFlight + 1) * stepNs;
                }
            }
            const auto nextPslTarget = [&]() -> uint64_t {
                if (!useTargeting || this->nextPsl == 0)
                    return 0;
                const uint64_t target = this->nextPsl;
                this->nextPsl += stepNs;
                return target;
            };

            // present each generated frame, then the real frame
            for (uint64_t j = 0; j < frames; j++)
                present([&gen, j](vk::CommandBuffer& cmdbuf, VkImage swapchainImage) {
                    return gen.obtain(cmdbuf, swapchainImage, j);
                }, MyVkPresentInfo{}, pace ? nextSchedule() : 0, nextPslTarget());

            present(recordRealFrame(virtualImage.handle()), *ppi,
                pace ? nextSchedule() : 0, nextPslTarget());

            // mark the virtual image as available again
            this->virtual_CompleteUPresent(*ppi);

            // periodic framerate report: base game rate vs generated output rate
            if (logFps) {
                realFrames++;
                genFrames += frames;
                const uint64_t t = nowInUs();
                if (logUs == 0) {
                    logUs = t;
                } else if (t - logUs >= 2'000'000) {
                    const double secs = static_cast<double>(t - logUs) / 1'000'000.0;
                    std::cerr << "lsfg-vk: base "
                        << static_cast<uint64_t>(realFrames / secs) << " fps -> output "
                        << static_cast<uint64_t>((realFrames + genFrames) / secs) << " fps\n";
                    if (this->presentTimingActive && this->pacingCount > 1) {
                        const double n = static_cast<double>(this->pacingCount);
                        const double mean = this->pacingSum / n;
                        const double var = this->pacingSumSq / n - mean * mean;
                        std::cerr << "lsfg-vk: present-timing achieved interval "
                            << static_cast<uint64_t>(mean / 1000.0) << " us (jitter "
                            << static_cast<uint64_t>(std::sqrt(var > 0.0 ? var : 0.0) / 1000.0)
                            << " us, n=" << this->pacingCount << ")\n";
                        this->pacingSum = 0.0;
                        this->pacingSumSq = 0.0;
                        this->pacingCount = 0;
                    }
                    logUs = t;
                    realFrames = 0;
                    genFrames = 0;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "lsfg-vk: virtual swapchain encountered an error:\n"
            "- " << e.what() << "\n";
    }

    if (offload.mutex.try_lock()) { // must be in vkDeviceWaitIdle if locked
        vk.df().QueueWaitIdle(offload.queue);
        offload.mutex.unlock();
    }
}

std::optional<MyVkPresentInfo> MyVkSwapchain::virtual_FetchUPresent(uint64_t timeout,
        uint64_t& counter) {
    const auto& vk = this->device.get().vkd();

    if (!this->presentSemaphore.wait(vk, counter, timeout))
        return std::nullopt;
    counter++;

    if (this->presents.empty())
        throw ls::error("virtual_FetchUPresent() encountered an impossible state");

    const auto info = this->presents.front();
    this->presents.pop();

    return info;
}

uint32_t MyVkSwapchain::virtual_AcquireNext(const vk::Semaphore& semaphore) {
    const auto& vk = this->device.get().vkd();

    uint32_t idx{};
    {
        const std::scoped_lock<std::mutex> lock(this->swapchainMutex);

        auto res = vk.df().AcquireNextImageKHR(vk.dev(),
            this->handle, UINT64_MAX,
            semaphore.handle(), VK_NULL_HANDLE,
            &idx
        );
        if (res != VK_SUCCESS) {
            this->status.store(res);

            if (res != VK_SUBOPTIMAL_KHR)
                throw ls::error("vkAcquireNextImageKHR() failed");
        }

    }

    return idx;
}

void MyVkSwapchain::virtual_PresentLinked(const MyVkPresentInfo& original_info,
        const vk::Semaphore& semaphore, uint32_t idx, uint64_t presentTimeNs) {
    const auto& vk = this->device.get().vkd();

    const uint64_t presentId = original_info.id.value_or(0);
    const VkPresentIdKHR presentIdInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR,
        .swapchainCount = 1,
        .pPresentIds = &presentId
    };
    const auto mode = original_info.present_mode.value_or(VK_PRESENT_MODE_FIFO_KHR);
    const VkSwapchainPresentModeInfoKHR presentModeInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_KHR,
        .pNext = original_info.id.has_value() ? &presentIdInfo : nullptr,
        .swapchainCount = 1,
        .pPresentModes = &mode
    };

    const void* chain{};
    if (original_info.present_mode.has_value())
        chain = &presentModeInfo;
    else if (original_info.id.has_value())
        chain = &presentIdInfo;

    // present-timing keys its results queue by present id, so tag every timed present
    // with our own incrementing VK_KHR_present_id2 value
    this->ptPresentId++;
    const VkPresentId2KHR presentId2Info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR,
        .pNext = chain,
        .swapchainCount = 1,
        .pPresentIds = &this->ptPresentId
    };
    if (this->presentTimingActive)
        chain = &presentId2Info;

    // present-timing: target an absolute time in the present-stage-local domain and
    // request feedback for the same stage. targetTime 0 (bootstrap) means "no target,
    // just feedback"; the stage fields are required for present-stage-local targeting.
    const VkPresentTimingInfoEXT timingInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT,
        .flags = 0,
        .targetTime = presentTimeNs,
        .timeDomainId = this->timeDomainId,
        .presentStageQueries = VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT
            | VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT
            | VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT
            | VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT,
        .targetTimeDomainPresentStage = this->timingStage
    };
    const VkPresentTimingsInfoEXT timingsInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT,
        .pNext = chain,
        .swapchainCount = 1,
        .pTimingInfos = &timingInfo
    };
    if (this->presentTimingActive)
        chain = &timingsInfo;

    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = chain,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &semaphore.handle(),
        .swapchainCount = 1,
        .pSwapchains = &this->handle,
        .pImageIndices = &idx,
    };
    {
        auto& offload = this->device.get().offload();

        const std::scoped_lock<std::mutex> lock(offload.mutex);
        const std::scoped_lock<std::mutex> lock2(this->swapchainMutex);

        auto res = vk.df().QueuePresentKHR(offload.queue, &presentInfo);
        if (res == VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT) {
            // the timing results queue is full; the next drain frees slots. the frame is
            // still presented, so this is not a fatal present error
        } else if (res != VK_SUCCESS) {
            this->status.store(res);

            if (res != VK_SUBOPTIMAL_KHR)
                throw ls::error("vkQueuePresentKHR() failed");
        }
    }

    if (original_info.id.has_value())
        this->doneSemaphore->signal(vk, presentId + 1);
}

void MyVkSwapchain::virtual_CompleteUPresent(const MyVkPresentInfo& info) {
    const auto& vk = this->device.get().vkd();

    {
        const std::scoped_lock<std::mutex> lock(this->availabilityMutex);
        this->availableImages.at(info.idx) = true;
    }

    if (info.fence != VK_NULL_HANDLE) {
        auto& offload = this->device.get().offload();

        const std::scoped_lock<std::mutex> lock(offload.mutex);
        auto res = vk.df().QueueSubmit(offload.queue, 0, nullptr, info.fence);
        if (res != VK_SUCCESS)
            this->status.store(res);
    }
}

/* Reimplementations of Vulkan functions */

VkResult MyVkSwapchain::GetSwapchainImagesKHR(uint32_t *count, VkImage *images) noexcept {
    if (images == nullptr) {
        *count = static_cast<uint32_t>(this->images.size());
        return VK_SUCCESS;
    }

    VkResult res = VK_SUCCESS;
    auto limit = static_cast<uint32_t>(this->images.size());

    if (*count < limit) {
        limit = *count;
        res = VK_INCOMPLETE;
    }

    for (uint32_t i = 0; i < limit; i++)
        images[i] = this->images[i].handle();

    *count = limit;
    return res;
}

namespace {
    /// find and mark an available image
    std::optional<uint32_t> mark_available(std::vector<bool>& avail, std::mutex& mutex) noexcept {
        const std::scoped_lock<std::mutex> lock(mutex);
        for (size_t i = 0; i < avail.size(); i++) {
            if (!avail[i])
                continue;

            avail[i] = false;
            return static_cast<uint32_t>(i);
        }
        return std::nullopt;
    }
}

namespace {
    /// signal a semaphore and fence on the shared graphics queue
    void signalSemaphoreAndFence(const vk::Vulkan& vk, std::mutex& queueMutex,
            VkSemaphore semaphore, VkFence fence) noexcept {
        const VkSubmitInfo info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .signalSemaphoreCount = semaphore != VK_NULL_HANDLE,
            .pSignalSemaphores = semaphore != VK_NULL_HANDLE ? &semaphore : nullptr
        };
        const std::scoped_lock<std::mutex> lock(queueMutex);
        vk.df().QueueSubmit(vk.queue(), 1, &info, fence);
    }
}

VkResult MyVkSwapchain::AcquireNextImageKHR(uint64_t timeout,
        VkSemaphore semaphore, VkFence fence, uint32_t *idx) noexcept {
    const auto& vk = this->device.get().vkd();

    // fast path: try to get an available image without blocking
    if (auto optIdx = mark_available(this->availableImages, this->availabilityMutex)) {
        *idx = *optIdx;

        signalSemaphoreAndFence(vk, this->device.get().offload().mutex, semaphore, fence);
        return this->status.load();
    }

    // if call is non-blocking: return not ready
    if (timeout == 0)
        return VK_NOT_READY;

    // otherwise: repeat fetch and delay 100us
    const uint64_t start = nowInUs();
    timeout *= 1'000; // to microseconds

    while (nowInUs() - start < timeout) {
        auto res = this->status.load();
        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR)
            return res;

        if (auto optIdx = mark_available(this->availableImages, this->availabilityMutex)) {
            *idx = *optIdx;

            signalSemaphoreAndFence(vk, this->device.get().offload().mutex, semaphore, fence);
            return res;
        }

        usleep(100);
    }

    return VK_TIMEOUT;
}

VkResult MyVkSwapchain::AcquireNextImage2KHR(const VkAcquireNextImageInfoKHR* info,
        uint32_t* idx) noexcept {
    return this->AcquireNextImageKHR(
        info->timeout,
        info->semaphore,
        info->fence,
        idx
    );
}

VkResult MyVkSwapchain::partial_QueuePresentKHR(const MyVkPresentInfo& info) noexcept {
    // test-only: throttle the game's present rate to LSFGVK_BASE_CAP fps, so a fast game
    // can be held below refresh (where frame generation is actually useful and the
    // achieved-interval metric is not corrupted by MAILBOX discarding frames)
    static const uint64_t capUs = [] {
        const char* cap = std::getenv("LSFGVK_BASE_CAP");
        const unsigned long long fps = cap ? std::strtoull(cap, nullptr, 10) : 0;
        return fps != 0 ? 1'000'000ULL / fps : 0ULL;
    }();
    if (capUs != 0) {
        const uint64_t now = nowInUs();
        if (now < this->baseCapNextUs)
            sleepUntilUs(this->baseCapNextUs);
        this->baseCapNextUs = (now > this->baseCapNextUs ? now : this->baseCapNextUs) + capUs;
    }

    MyVkPresentInfo stamped = info;
    stamped.arrivalUs = nowInUs();
    this->presents.emplace(stamped);
    return this->status.load();
}

VkResult MyVkSwapchain::ReleaseSwapchainImagesKHR(
        const VkReleaseSwapchainImagesInfoKHR* info) noexcept {
    for (uint32_t i = 0; i < info->imageIndexCount; i++) {
        const uint32_t idx = info->pImageIndices[i];

        const std::scoped_lock<std::mutex> lock(this->availabilityMutex);
        this->availableImages.at(idx) = true;
    }
    return VK_SUCCESS;
}

VkResult MyVkSwapchain::WaitForPresentKHR(uint64_t id, uint64_t timeout) noexcept {
    const auto& vk = this->device.get().vkd();

    if (!this->doneSemaphore->wait(vk, id + 1, timeout))
        return VK_TIMEOUT;

    const std::scoped_lock<std::mutex> lock(this->swapchainMutex);
    const auto& df = vk.df();

    if (df.WaitForPresentKHR) {
        return vk.df().WaitForPresentKHR(vk.dev(), this->handle, id, timeout);
    }

    if (df.WaitForPresent2KHR) {
        const VkPresentWait2InfoKHR info{
            .sType = VK_STRUCTURE_TYPE_PRESENT_WAIT_2_INFO_KHR,
            .presentId = id,
            .timeout = timeout
        };
        return vk.df().WaitForPresent2KHR(vk.dev(), this->handle, &info);
    }

    return VK_ERROR_EXTENSION_NOT_PRESENT; // should never happen
}

VkResult MyVkSwapchain::WaitForPresent2KHR(
        const VkPresentWait2InfoKHR* info) noexcept {
    return this->WaitForPresentKHR(
        info->presentId,
        info->timeout
    );
}
