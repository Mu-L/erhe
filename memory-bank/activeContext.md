§MBEL:5.0

[FOCUS]
@property-system::erhe::property{WPF-dependency-property-port;doc/property-system.md=design-record{D-numbers};doc/property-inventory.md=per-field-status;src/erhe/property/notes.md=library-reference}
>2026-09-03::object-refs{D28/D29}+migrations{Node_physics+Grid+Brush_placement+Physics_material+Layout}+Owner_type-registry{D27}+attached-properties{D3;qualified-"<Owner>.<name>";Layout.align_x..grid_span-on-child-Node}✓
>2026-09-04::Add/Remove-Property-UI{940aeccbb..3a2318199}✓
  windows/attached_property_listing.{hpp,cpp}::D12-listing-rule+addable-candidates{rows+MCP-share-it}
  Properties-section-ends-with-"Add Property"-row{filterable-popup;candidates=attached-registrations-not-listed;add-writes-effective-value-local→row-appears,undo-removes}
  attached-rows::"Remove Property"-context-menu+inline-"x"-when-listed-only-by-local-value
  MCP::get_addable_item_properties;add=set_item_property-qualified-name;remove=value-null
  trap-fixed{4e496b984}::Dependency_property_rows-kept-m_items-across-frames=scene-close-leak-holder→snapshot-bound-only-in-add_rows+each-row-lambda
  verify✓headless{node+cube-mesh-attachment;floor-mesh-is-sealed;close-scene-clean-132-released}|?user-interactive{filter-typing,Ctrl+Z,multi-select,locked-item-disabled}

[STATE]
@branch::main{#10-commits-unpushed;user-pushes-themselves}
prompt_queue.txt::item0=next-migration{Light-derived-rows}|items1-4-unchanged
memory-bank-files-of-July{asset-manager-R1/R2}→history{superseded-by-this-focus}

[OPEN]
?inherits-registration-check{inherits⇒attached||root-owner;doc-section-6;when-first-inheritable-attached-property-appears}
?Light-derived-rows→Rendertarget_mesh→Animation{doc/property-inventory.md-"Not yet migrated"-order}

[BLOCKERS]
none
