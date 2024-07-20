#include "frame_controller.hpp"
#include "example_log.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/simulation_variable.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace example {

using glm::mat4;
using glm::vec3;
using glm::vec4;

Frame_controller::Frame_controller()
    : Node_attachment{"frame controller"}
{
    reset();
    rotate_x      .set_damp     (0.700f);
    rotate_y      .set_damp     (0.700f);
    rotate_z      .set_damp     (0.700f);
    rotate_x      .set_max_delta(0.02f);
    rotate_y      .set_max_delta(0.02f);
    rotate_z      .set_max_delta(0.02f);
    translate_x   .set_damp     (0.92f);
    translate_y   .set_damp     (0.92f);
    translate_z   .set_damp     (0.92f);
    translate_x   .set_max_delta(0.004f);
    translate_y   .set_max_delta(0.004f);
    translate_z   .set_max_delta(0.004f);
    speed_modifier.set_max_value(3.0f);
    speed_modifier.set_damp     (0.92f);
    speed_modifier.set_max_delta(0.5f);

    update();
}

auto Frame_controller::get_controller(const Control control) -> erhe::math::Simulation_variable&
{
    switch (control) {
        case Control::translate_x: return translate_x;
        case Control::translate_y: return translate_y;
        case Control::translate_z: return translate_z;
        case Control::rotate_x   : return rotate_x;
        case Control::rotate_y   : return rotate_y;
        case Control::rotate_z   : return rotate_z;
        default: {
            ERHE_FATAL("bad control %04x", static_cast<unsigned int>(control));
        }
    }
}

void Frame_controller::set_position(const vec3 position)
{
    m_position = position;
    update();
}

void Frame_controller::set_elevation(const float value)
{
    m_elevation = value;
    update();
}

void Frame_controller::set_heading(const float value)
{
    m_heading = value;
    m_heading_matrix = erhe::math::create_rotation(m_heading, erhe::math::vector_types<float>::vec3_unit_y());
    update();
}

auto Frame_controller::get_position() const -> vec3
{
    return m_position;
}

auto Frame_controller::get_elevation() const -> float
{
    return m_elevation;
}

auto Frame_controller::get_heading() const -> float
{
    return m_heading;
}

auto Frame_controller::get_static_type() -> uint64_t
{
    return erhe::Item_type::node_attachment | erhe::Item_type::frame_controller;
}

auto Frame_controller::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Frame_controller::get_type_name() const -> std::string_view
{
    return static_type_name;
}

void Frame_controller::get_transform_from_node(erhe::scene::Node* node)
{
    if (node == nullptr) {
        return;
    }
    const vec4 position  = node->position_in_world();
    const vec4 direction = node->direction_in_world();

    m_position = position;
    float heading  {0.0f};
    float elevation{0.0f};
    erhe::math::cartesian_to_heading_elevation(direction, elevation, heading);
    m_elevation = elevation;
    m_heading   = heading;

    m_heading_matrix = erhe::math::create_rotation(m_heading, erhe::math::vector_types<float>::vec3_unit_y());
}

void Frame_controller::handle_node_update(erhe::scene::Node* old_node, erhe::scene::Node* new_node)
{
    static_cast<void>(old_node);
    if (new_node == nullptr) {
        return;
    }
    get_transform_from_node(new_node);
}

void Frame_controller::handle_node_transform_update()
{
    if (m_transform_update) {
        return;
    }

    auto* node = get_node();
    if (node == nullptr) {
        return;
    }
    get_transform_from_node(node);
    update();
}

void Frame_controller::reset()
{
    translate_x.reset();
    translate_y.reset();
    translate_z.reset();
    rotate_x.reset();
    rotate_y.reset();
    rotate_z.reset();
}

void Frame_controller::update()
{
    auto* node = get_node();
    if (node == nullptr) {
        return;
    }

    const mat4 elevation_matrix = erhe::math::create_rotation(m_elevation, erhe::math::vector_types<float>::vec3_unit_x());
    m_rotation_matrix = m_heading_matrix * elevation_matrix;

    mat4 parent_from_local = m_rotation_matrix;

    // Put translation to column 3
    parent_from_local[3] = vec4{m_position, 1.0f};

    m_transform_update = true;
    node->set_parent_from_node(parent_from_local);
    m_transform_update = false;
}

auto Frame_controller::get_axis_x() const -> vec3
{
    return vec3{m_rotation_matrix[0]};
    //return vec3{m_heading_matrix[0]};
}

auto Frame_controller::get_axis_y() const -> vec3
{
    return vec3{m_rotation_matrix[1]};
    //return vec3{m_heading_matrix[1]};
}

auto Frame_controller::get_axis_z() const -> vec3
{
    return vec3{m_rotation_matrix[2]};
    //return vec3{m_heading_matrix[2]};
}

void Frame_controller::update_fixed_step()
{
    translate_x.update();
    translate_y.update();
    translate_z.update();
    rotate_x.update();
    rotate_y.update();
    rotate_z.update();
    speed_modifier.update();

    const float speed = 0.8f + speed_modifier.current_value();

    if (translate_x.current_value() != 0.0f) {
        m_position += get_axis_x() * translate_x.current_value() * speed;
    }

    if (translate_y.current_value() != 0.0f) {
        m_position += get_axis_y() * translate_y.current_value() * speed;
    }

    if (translate_z.current_value() != 0.0f) {
        m_position += get_axis_z() * translate_z.current_value() * speed;
    }

    if (
        (rotate_x.current_value() != 0.0f) ||
        (rotate_y.current_value() != 0.0f)
    ) {
        m_heading += rotate_y.current_value();
        m_elevation += rotate_x.current_value();
        const mat4 elevation_matrix = erhe::math::create_rotation(
            m_elevation,
            erhe::math::vector_types<float>::vec3_unit_x()
        );
        m_heading_matrix = erhe::math::create_rotation(
            m_heading,
            erhe::math::vector_types<float>::vec3_unit_y()
        );
        m_rotation_matrix = m_heading_matrix * elevation_matrix;
    }

    update();
}

auto is_frame_controller(const erhe::Item_base* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    using namespace erhe::bit;
    return test_all_rhs_bits_set(item->get_type(), erhe::Item_type::frame_controller);
}

auto is_frame_controller(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    return is_frame_controller(item.get());
}

auto get_frame_controller(const erhe::scene::Node* node) -> std::shared_ptr<Frame_controller>
{
    for (const auto& attachment : node->get_attachments()) {
        const auto frame_controller = std::dynamic_pointer_cast<Frame_controller>(attachment);
        if (frame_controller) {
            return frame_controller;
        }
    }
    return {};
}

} // namespace example
