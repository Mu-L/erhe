§MBEL:5.0

[FOCUS]
@style-library::doc/style-library.md{R1-R6,D1-D5}✓2026-09-04{9132674f2+a88dd405c+persistence+docs}
  D1::style-source=any-Dependency_object{local-values=style;m_style_users-registry;deliver()->propagate_to_style_users;Property_style=Dependency_object-subclass}
  D2::editor::Style-item{content_library/style.{hpp,cpp};secondary-owner=target-class;Item_type::style=bit47;Styles-category-folder;icon}
  D3::Item_base::style_property{bridged-object-ref;validate-target-vs-owner-chain-or-secondary;flags-partition+variant+serialize}
  D4::ERHE_scene.styles{name,target,properties}+ERHE_material.style+library_folders.style;import order:styles→...→material-styles→folders
  D5::add_default_materials→"Brushed metal"-Style-item;make_style_from_values{paste-as-style+MCP-set_item_style}
  verify✓headless{scratchpad-verify_styles.py:live-edit,clear/reassign-by-name,folder-style-inherit(after-clearing-local),paste-as-style+undo,save/open,close-clean}|?user-interactive
@node-holds-attachment-values::D30-generalized✓2026-09-04{ff0f28de6+62b80d7fd+8d1b12a57;user-report:"cannot add light properties to empty node / to style"}
  D30::secondary-covers-descendant-types{is_secondary_property:secondary|ancestor|descendant;¬bridged+¬computed;deliver-skips-property_changed-metadata-callback-on-holder{callback-casts-to-registering-class→was-UB}}
  Node::secondary=Node_attachment{offers-Light.*/Camera.*/...;Light-props-all-inherits}|Item_base::style_applies{target-on-chain||target-descends-from-object-secondary}|candidates-filtered
  style-any-class::©User-chose-2026-09-04{"still cannot add light or camera properties to style"}→Style-secondary=root_owner_type{holds-every-class;applies-to-every-item;no-target;ERHE_scene.styles={name,properties};Create-Style-plain-item;MCP-create_style{name}}|listing-rule=has_own_value{style-provided-secondary-listed;x-clears-local-only}
  camera-migrated✓::©User-asked{"all camera properties placed into empty node and/or style"}→every-Camera-prop-entry-store+inherits;projection()=const-mirror{refresh_projection_mirror-in-on_property_changed};writers=setters/set_projection;ERHE_camera.properties=complete-local-set;content-fit-widens-LOCAL-z_far/shadow_range-only{style-assigned-after-fit}
  trap-fixed::for_each_local_value-bridged-list-unsorted{root-first-visit;style_property-on-Item_base-exposed-it;Node_properties-test-failed-since-a88dd405c}
  persistence::ERHE_node.style{Item_style_by_name_operation}+ERHE_light.properties=complete-local-set{loader-clears-unlisted-KHR-baked-values→light-keeps-inheriting-after-reload}
  trap-hit::Mesh.world_bounds_*-computed-listed-as-secondary-on-node→compute(node)-cast-to-Mesh→crash{fixed-by-¬computed-rule}|find_scene("")¬first-scene{create_library_folder-schema-claims-default}
  verify✓headless{scratchpad-verify_light_inherit.py;doc/style-library.md-step-6}|?user-interactive{Create-Style>Light,Add-Property-on-empty-node}
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
@branch::main{#28-commits-unpushed;user-pushes-themselves}
prompt_queue.txt::items-0-4-unchanged{folders-task-was-a-direct-request}

[OPEN]
?user-interactive-check{folders+category-props+texture-slots+styles+node-attachment-values+camera}→expect-fixes;then-migrations{Light-derived-rows}
?material-reload-limitation-still-open::ERHE_material-bakes-effective→local{lights-fixed-via-ERHE_light.properties-rule;same-rule-for-materials=candidate}
?inherits-registration-check::third-form-added{secondary-owner-type}→still-unimplemented
?startup-log-error::"property 'lightmapped': object is sealed"{pre-existing,at-startup,unrelated}
?inherits-registration-check{doc/property-system.md-section-6}
?Light-derived-rows→Rendertarget_mesh→Animation{doc/property-inventory.md}

[BLOCKERS]
none
