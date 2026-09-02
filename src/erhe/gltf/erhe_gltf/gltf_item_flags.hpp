#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace erhe { class Item_base; }

namespace erhe::gltf {

// The persistent (authored) Item flags serialized by NAME in ERHE_*
// extensions (doc/gltf-scene-roundtrip-plan.md phase 3). Names, never raw
// bit values: bit positions are not stable across erhe versions; unknown
// names are ignored on load so the set can grow. Transient presentation
// state (selected, hovered_*, negative_determinant, affects_shadow) and the
// structurally handled import_root are deliberately absent from the set.
// Shared by the exporter-internal ERHE_node / ERHE_camera / ERHE_light
// writers and the editor-domain extension builders (ERHE_layout).

// JSON array of persistent flag names for the set bits, e.g.
// ["visible","content"].
[[nodiscard]] auto persistent_item_flags_to_json(uint64_t flag_bits) -> std::string;

// The flag bit for a serialized name; 0 when the name is unknown.
[[nodiscard]] auto persistent_item_flag_from_name(std::string_view name) -> uint64_t;

// Applies an accumulated set of listed flag bits exactly: every persistent
// flag is enabled when listed and disabled when not. Bits outside the
// persistent set and the derived bits (Item_flags::derived, see below) are
// left untouched.
void apply_persistent_item_flags(erhe::Item_base& item, uint64_t listed_bits);

// Local property values (doc/property-system.md D23 / D14): the
// "properties" object next to "flags" holds every local value of the item's
// registered properties that is stored (not bridged, not an expression)
// and flagged serialize, as name -> D16 text, e.g. {"visible":"false"}.
// The object is written even when empty: its presence marks a file whose
// visible / shadow_cast / lightmapped come from properties, not flags.
[[nodiscard]] auto item_local_properties_to_json(const erhe::Item_base& item) -> std::string;

// Applies one serialized local value; false (logged) for an unknown name
// or a value that does not parse as the property's type.
auto apply_item_local_property(erhe::Item_base& item, std::string_view name, std::string_view value) -> bool;

// Older files list visible / shadow_cast / lightmapped in the flags
// arrays: a listed shadow_cast / lightmapped becomes a local true, an
// unlisted visible a local false. Call only when the payload has no
// "properties" object.
void apply_legacy_derived_item_flags(erhe::Item_base& item, uint64_t listed_bits);

}
