§MBEL:5.0

[TASK::Add/Remove-Property-UI]{DONE-3a2318199-2026-09-04}
✓shared-listing-rule{940aeccbb}+Add-Property-row{93153dcf9}+Remove-Property{5ea6655e8}+rows-m_items-leak-fix{4e496b984}+MCP-tool{75ca1498c}+docs-D12/D13{3a2318199;plan-doc-deleted}
?user-interactive-verify

[TASK::property-migrations]{ONGOING}
✓Material+Node+Light+Camera+graph-nodes+Mesh_primitive+Node_physics+Grid+Brush_placement+Physics_material+Layout+Layout-hints{attached}
?next::Light-derived-rows{flux+blackbody}→Rendertarget_mesh→Animation{see-doc/property-inventory.md}

[NOTES]
!headless-recipe::build_vs2026_vulkan_headless-editor→ERHE_AI_DRIVER=1-launch-hidden→mcp_call.py-b64-args{get_item_properties/set_item_property/get_addable_item_properties/undo;ids-reshuffle-per-launch;scene_name-required-for-create_node/select_items/get_node_details}
!default-scene::floor-mesh-sealed{lock_edit}→use-cube-attachment-for-attachment-tests
!scene-close-check::close_scene→wait≈6s→grep-"scene-close"{clean="all N released"}
!clangd-db::re-run-configure_ninja_win_clang.bat-after-adding-source-files{done-2026-09-04}
