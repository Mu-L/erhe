§MBEL:5.0

[FOCUS]
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
@branch::main{#16-commits-unpushed;user-pushes-themselves}
prompt_queue.txt::items-0-4-unchanged{folders-task-was-a-direct-request}

[OPEN]
?startup-log-error::"property 'lightmapped': object is sealed"{pre-existing,at-startup,unrelated}
?inherits-registration-check{doc/property-system.md-section-6}
?Light-derived-rows→Rendertarget_mesh→Animation{doc/property-inventory.md}

[BLOCKERS]
none
