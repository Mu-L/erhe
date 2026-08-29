// GL-specific worker-context tests: worker-side texture create + upload,
// worker-side blit-encoder writes, and context-index introspection. See
// doc/gl-worker-thread-contexts.md and doc/gl-worker-context-tests-plan.md.
//
// This file is added to the target only on the OpenGL backend (CMake); it
// reaches GL internals through the public Device::get_impl() and the
// erhe_graphics/gl headers. The uploads run on worker share contexts, so
// these are the only exercisers of the texture-storage, texture-upload,
// mipmap and fill publication points.

#include "gpu_test_fixture.hpp"
#include "gpu_test_environment.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/scoped_worker_context.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/gl/gl_context_index.hpp"
#include "erhe_gl/wrapper_functions.hpp"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace erhe::graphics::test {

namespace {

constexpr int texture_width  = 8;
constexpr int texture_height = 8;
constexpr std::size_t bytes_per_row  = static_cast<std::size_t>(texture_width) * 4u;
constexpr std::size_t texture_bytes  = bytes_per_row * static_cast<std::size_t>(texture_height);

void fill_pixel_pattern(std::vector<std::uint8_t>& bytes)
{
    for (std::size_t i = 0, end = bytes.size(); i < end; ++i) {
        bytes[i] = static_cast<std::uint8_t>((i * 7u) + 3u);
    }
}

} // anonymous namespace

class Worker_context_gl_test : public Gpu_test
{
protected:
    void require_worker_contexts()
    {
        if (!device().supports_worker_contexts()) {
            GTEST_SKIP() << "Device has no worker-context support in this configuration";
        }
    }

    // Staging buffer created with initial pixel bytes - worker-legal
    // (host-visible, non-persistent, init_data at storage time; workers may
    // not map).
    auto make_staging_buffer(const std::vector<std::uint8_t>& bytes, const char* debug_label)
        -> std::shared_ptr<erhe::graphics::Buffer>
    {
        const erhe::graphics::Buffer_create_info create_info{
            .capacity_byte_count               = bytes.size(),
            .usage                             = erhe::graphics::Buffer_usage::transfer_src,
            .required_memory_property_bit_mask =
                erhe::graphics::Memory_property_flag_bit_mask::host_read |
                erhe::graphics::Memory_property_flag_bit_mask::host_write,
            .init_data   = bytes.data(),
            .debug_label = erhe::utility::Debug_label{debug_label}
        };
        return std::make_shared<erhe::graphics::Buffer>(device(), create_info);
    }

    auto make_worker_texture(const int level_count, const char* debug_label)
        -> std::shared_ptr<erhe::graphics::Texture>
    {
        const erhe::graphics::Texture_create_info create_info{
            .device      = device(),
            .usage_mask  =
                erhe::graphics::Image_usage_flag_bit_mask::sampled      |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_src |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = erhe::dataformat::Format::format_8_vec4_unorm,
            .width       = texture_width,
            .height      = texture_height,
            .level_count = level_count,
            .debug_label = erhe::utility::Debug_label{debug_label}
        };
        return std::make_shared<erhe::graphics::Texture>(device(), create_info);
    }
};

// The thread-local context index is the permission model: -1 off-scope on a
// worker thread, 0 on main, 1..pool inside a worker scope, and stable
// across a nested scope.
TEST_F(Worker_context_gl_test, context_index_tracks_scopes)
{
    require_worker_contexts();

    ASSERT_EQ(get_gl_context_index(), 0) << "main thread must be context 0";

    int index_before        = -2;
    int index_outer         = -2;
    int index_inner         = -2;
    int index_after_inner   = -2;
    int index_after_release = -2;
    std::thread worker{
        [&]() {
            index_before = get_gl_context_index();
            {
                erhe::graphics::Scoped_worker_context outer{device()};
                index_outer = get_gl_context_index();
                {
                    erhe::graphics::Scoped_worker_context inner{device()};
                    index_inner = get_gl_context_index();
                }
                index_after_inner = get_gl_context_index();
            }
            index_after_release = get_gl_context_index();
        }
    };
    worker.join();

    ASSERT_EQ(index_before, -1);
    ASSERT_GE(index_outer, 1);
    ASSERT_LE(index_outer, gl_worker_context_pool_size);
    ASSERT_EQ(index_inner, index_outer) << "nested scope must keep the same context";
    ASSERT_EQ(index_after_inner, index_outer) << "inner release must not drop the outer context";
    ASSERT_EQ(index_after_release, -1);
}

// T4: a worker creates a texture AND uploads its pixels (staging buffer
// with init_data, blit-encoder copy_from_buffer), all under one scope; the
// main thread reads the texture back and checks every texel. Exercises the
// texture-storage and texture-upload publication points and the
// main-consumer wait in the readback path.
TEST_F(Worker_context_gl_test, worker_texture_create_and_upload)
{
    require_worker_contexts();

    std::vector<std::uint8_t> expected(texture_bytes);
    fill_pixel_pattern(expected);

    std::shared_ptr<erhe::graphics::Texture> texture{};
    std::thread worker{
        [&]() {
            erhe::graphics::Scoped_worker_context worker_context{device()};
            const std::shared_ptr<erhe::graphics::Buffer> staging = make_staging_buffer(expected, "T4 staging");
            texture = make_worker_texture(1, "T4 worker texture");
            erhe::graphics::Command_buffer worker_command_buffer{device(), erhe::utility::Debug_label{"T4 worker cb"}};
            worker_command_buffer.begin();
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(worker_command_buffer);
            blit.copy_from_buffer(
                staging.get(),
                0,                                                   // source_offset
                bytes_per_row,                                       // source_bytes_per_row
                texture_bytes,                                       // source_bytes_per_image
                glm::ivec3{texture_width, texture_height, 1},        // source_size
                texture.get(),
                0,                                                   // destination_slice
                0,                                                   // destination_level
                glm::ivec3{0, 0, 0}                                  // destination_origin
            );
            worker_command_buffer.end();
        }
    };
    worker.join();
    ASSERT_TRUE(texture);

    const std::vector<std::uint8_t> actual = read_texture_rgba8(*texture);
    ASSERT_EQ(actual, expected);
}

// T5a: worker-side generate_mipmaps is a publication producer; the main
// thread reads a downsampled level back. With a constant-color level 0
// every level averages to the same color, so the check is exact.
TEST_F(Worker_context_gl_test, worker_generate_mipmaps)
{
    require_worker_contexts();

    std::vector<std::uint8_t> solid(texture_bytes);
    for (std::size_t i = 0; i < texture_bytes; i += 4) {
        solid[i + 0] = 10;
        solid[i + 1] = 20;
        solid[i + 2] = 30;
        solid[i + 3] = 255;
    }

    std::shared_ptr<erhe::graphics::Texture> texture{};
    std::thread worker{
        [&]() {
            erhe::graphics::Scoped_worker_context worker_context{device()};
            const std::shared_ptr<erhe::graphics::Buffer> staging = make_staging_buffer(solid, "T5 staging");
            texture = make_worker_texture(
                erhe::graphics::get_texture_level_count(texture_width, texture_height),
                "T5 mipmapped texture"
            );
            erhe::graphics::Command_buffer worker_command_buffer{device(), erhe::utility::Debug_label{"T5 worker cb"}};
            worker_command_buffer.begin();
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(worker_command_buffer);
            blit.copy_from_buffer(
                staging.get(), 0, bytes_per_row, texture_bytes,
                glm::ivec3{texture_width, texture_height, 1},
                texture.get(), 0, 0, glm::ivec3{0, 0, 0}
            );
            blit.generate_mipmaps(texture.get());
            worker_command_buffer.end();
        }
    };
    worker.join();
    ASSERT_TRUE(texture);
    ASSERT_GT(texture->get_level_count(), 1);

    const std::vector<std::byte> level1 = read_texture_level_bytes(*texture, 1, 4);
    ASSERT_EQ(level1.size(), (texture_bytes / 4u));
    for (std::size_t i = 0; i < level1.size(); i += 4) {
        EXPECT_EQ(static_cast<std::uint8_t>(level1[i + 0]), 10u) << "texel " << (i / 4);
        EXPECT_EQ(static_cast<std::uint8_t>(level1[i + 1]), 20u);
        EXPECT_EQ(static_cast<std::uint8_t>(level1[i + 2]), 30u);
        EXPECT_EQ(static_cast<std::uint8_t>(level1[i + 3]), 255u);
    }
}

// T5b: worker-side fill_buffer is a publication producer; main reads the
// fill back through the blit copy path.
TEST_F(Worker_context_gl_test, worker_fill_buffer)
{
    require_worker_contexts();

    constexpr std::size_t byte_count = 512;
    constexpr std::uint8_t fill_value = 0xA5;

    std::shared_ptr<erhe::graphics::Buffer> buffer{};
    std::thread worker{
        [&]() {
            erhe::graphics::Scoped_worker_context worker_context{device()};
            const std::vector<std::uint8_t> zeroes(byte_count, 0u);
            buffer = make_staging_buffer(zeroes, "T5b filled buffer");
            erhe::graphics::Command_buffer worker_command_buffer{device(), erhe::utility::Debug_label{"T5b worker cb"}};
            worker_command_buffer.begin();
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(worker_command_buffer);
            blit.fill_buffer(buffer.get(), 0, byte_count, fill_value);
            worker_command_buffer.end();
        }
    };
    worker.join();
    ASSERT_TRUE(buffer);

    const std::shared_ptr<erhe::graphics::Buffer> readback = make_readback_buffer(byte_count, "T5b readback");
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
            blit.copy_from_buffer(buffer.get(), 0, readback.get(), 0, byte_count);
        }
    );
    const std::vector<std::byte> actual = read_buffer(*readback, byte_count);
    for (std::size_t i = 0; i < byte_count; ++i) {
        ASSERT_EQ(static_cast<std::uint8_t>(actual[i]), fill_value) << "byte " << i;
    }
}

// T13: worker GL errors are observable - the per-context debug callback
// must route a deliberately provoked worker-side GL error into the
// environment's message list. This is what makes the other worker tests'
// silence meaningful. The messages are consumed here so TearDown does not
// fail the case.
TEST_F(Worker_context_gl_test, worker_gl_errors_are_observable)
{
    require_worker_contexts();
    if (!device().get_info().use_debug_output) {
        GTEST_SKIP() << "GL debug output is not enabled in this configuration";
    }

    Gpu_test_environment::get().clear_messages();
    std::thread worker{
        [&]() {
            erhe::graphics::Scoped_worker_context worker_context{device()};
            // Deliberate GL_INVALID_VALUE: buffer name that was never
            // created by glCreateBuffers.
            gl::named_buffer_sub_data(0xDEADBEEFu, 0, 0, nullptr);
        }
    };
    worker.join();

    const std::vector<Gpu_test_environment::Message> messages = Gpu_test_environment::get().take_messages();
    ASSERT_FALSE(messages.empty()) << "worker GL error was silently discarded - the per-context debug callback is not installed or not routed";
}

} // namespace erhe::graphics::test
