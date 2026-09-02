#pragma once

#include <cstdint>

namespace erhe::texgen { class Node_descriptor; }

namespace editor {

// Registers the texture graph node parameters as dependency properties
// (property-system D27 / section 6 texture-graph item): one owner subtype
// per node descriptor and one property per float / color / enum / bool /
// size parameter (plus the seed of a seeded descriptor), bridged into the
// Texture_descriptor_node's Parameter_value storage by parameter index.
// Gradient and curve parameters have no Property_value form and stay in
// the node's imgui().
//
// The descriptors are function-local statics built on first use, so this
// cannot ride C++ static initialization: call once from run_editor(),
// before any thread other than main exists (the registry's writes must end
// with single-threaded early startup, D3 / R12). Idempotent.
void register_texture_graph_properties();

// The subtype allocated for the descriptor, 0 when
// register_texture_graph_properties() has not registered it.
[[nodiscard]] auto texture_descriptor_owner_subtype(const erhe::texgen::Node_descriptor* descriptor) -> uint32_t;

} // namespace editor
