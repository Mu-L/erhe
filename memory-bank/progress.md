§MBEL:5.0

[TASK::style-library]{DONE-2026-09-04}
✓style-source-generalization{9132674f2}+Style-item+style-property{a88dd405c}+persistence+docs
✓node-holds-attachment-values+Light-inherits+Create-Style/create_style+node-style-persistence{ff0f28de6..8d1b12a57}
✓style-holds-any-class{no-target}
✓Camera-props-entry-store+inherits{node/style-hold-Camera.*}
✓physics-material-only-friction-carrier+names-persist{ERHE_scene}
?user-interactive-verify

[TASK::content-library-folders]{DONE-2026-09-04}
✓drop-fix{adc3b676f}+category-properties-D30{5ff73f41b..34d66432b}+texture-slots-inherit{db69e84fc..9431513f4}
✓D1-inheritance-link{48518c02f}+UI{ac5126944}+ERHE_scene.library_folders{e0704b0f0}+subtree-dedup-fix{9e768b4e0}+MCP{7993068f6}+docs
?user-interactive-verify

[TASK::Add/Remove-Property-UI]{DONE-3a2318199-2026-09-04}
✓shared-listing-rule{940aeccbb}+Add-Property-row{93153dcf9}+Remove-Property{5ea6655e8}+rows-m_items-leak-fix{4e496b984}+MCP-tool{75ca1498c}+docs-D12/D13{3a2318199;plan-doc-deleted}
?user-interactive-verify

[TASK::property-migrations]{ONGOING}
✓Material+Node+Light+Camera{entry-store-since-2026-09-04}+graph-nodes+Mesh_primitive+Node_physics+Grid+Brush_placement+Physics_material+Layout+Layout-hints{attached}
?next::Light-derived-rows{flux+blackbody}→Rendertarget_mesh→Animation{see-doc/property-inventory.md}

[NOTES]
!headless-recipe::build_vs2026_vulkan_headless-editor→ERHE_AI_DRIVER=1-launch-hidden→mcp_call.py-b64-args{get_item_properties/set_item_property/get_addable_item_properties/undo;ids-reshuffle-per-launch;scene_name-required-for-create_node/select_items/get_node_details}
!default-scene::floor-mesh-sealed{lock_edit}→use-cube-attachment-for-attachment-tests
!scene-close-check::close_scene→wait≈6s→grep-"scene-close"{clean="all N released"}
!clangd-db::re-run-configure_ninja_win_clang.bat-after-adding-source-files{done-2026-09-04}
