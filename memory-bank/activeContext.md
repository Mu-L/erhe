§MBEL:5.0

[FOCUS]
@folder-category-properties::D30{secondary-owner-type}✓2026-09-04{5ff73f41b+59dd60042+34d66432b}
  Content_library_node.category_owner_type→get_secondary_property_owner_type{folders-only};Material-value-props-inherits=true;Add-Property-offers-"Material.<name>"-on-Materials-folder;listing=is_extra_property_listed+collect_addable_properties
  trap::Material-visible_when-lambdas-static_cast-to-Material→NEVER-evaluate-on-a-folder{secondary-listed-by-local-value-only}
  limitation::material-own-values-are-local{Reset-to-default-lets-folder-through};glTF-export-bakes-effective→local-on-reload{folder-value-persists}
  texture-slots::entry-store+inherits{db69e84fc;Material::on_property_changed-mirrors-effective-value-into-Material_data;set_data/create-info:default-field=unset,else-local;folder-texture-ref-resolves-via-Content_library_node::resolve_expression_object;find_scene_root_for_item-knows-library-nodes}
  verify✓headless{scratchpad-verify_folder_material.py:addable→set-folder-red→clear-Copper→inherited→undo/redo→remove→save/open→close-clean}|?user-interactive
@content-library-folders::doc/content-library-folders.md{R1-R6,D1-D7}✓2026-09-04{5-commits-48518c02f..7993068f6+follow-ups}
  D1::Item_base.m_inheritance_container{set-by-Content_library_node::handle_add_child;cleared-in-dtor;for_each_inheritance_child-visits-item;reference-entries-never}
  D2/D3::Create-Folder-menu{scene_root.cpp}+drag-onto-folder{Content_library_move_operation}
  D5/D6::ERHE_scene.library_folders{path+properties+items;brushes-folder_path-read-only}+Content_library_folders_operation{last-in-import_gltf_editor_state}
  D7::MCP-create_library_folder+move_library_item{folder_path,undoable}+find_item_in_scene-visits-library-nodes
  trap-fixed::add/remove-scanned-direct-children-only→duplicate-node-per-item-after-folder-move{find_entry-subtree;get_all-caches-cleared-up-to-root}
  verify✓headless{scratchpad-recipe:create→move-Copper→set-visible-false→inherited→undo/redo→save/open→local+inherited→close-clean;undo_reference_clearing_smoke_test-45/45}|?user-interactive{Create-Folder,rename,drag-drop,Ctrl+Z}
@property-system::erhe::property{doc/property-system.md=design-record;doc/property-inventory.md=per-field-status;src/erhe/property/notes.md=library-reference}
  >2026-09-04::Add/Remove-Property-UI{940aeccbb..3a2318199}✓|?user-interactive

[STATE]
@branch::main{#24-commits-unpushed;user-pushes-themselves}
prompt_queue.txt::items-0-4-unchanged{folders-task-was-a-direct-request}

[OPEN]
?startup-log-error::"property 'lightmapped': object is sealed"{pre-existing,at-startup,unrelated}
?inherits-registration-check{doc/property-system.md-section-6}
?Light-derived-rows→Rendertarget_mesh→Animation{doc/property-inventory.md}

[BLOCKERS]
none
