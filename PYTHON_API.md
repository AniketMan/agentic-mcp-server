# Complete Python API Reference for AgenticMCP

This document lists ALL available Python capabilities through the `unreal` module. This is the authoritative reference - no guessing, no "and much more".

---

## 1. EditorAssetLibrary

Asset management and file operations in the Content Browser.

| Method | Description |
|--------|-------------|
| `consolidate_assets(asset_to_consolidate_to, assets_to_consolidate)` | Consolidate assets by replacing references |
| `delete_asset(asset_path_to_delete)` | Delete an asset |
| `delete_directory(directory_path_to_delete)` | Delete a directory and all contents |
| `delete_loaded_asset(asset_to_delete)` | Delete an already-loaded asset |
| `delete_loaded_assets(assets_to_delete)` | Batch delete loaded assets |
| `do_assets_exist(asset_paths)` | Check if multiple assets exist |
| `does_asset_exist(asset_path)` | Check if a single asset exists |
| `does_directory_exist(directory_path)` | Check if directory exists |
| `does_directory_have_assets(directory_path, recursive)` | Check if directory contains assets |
| `duplicate_asset(source_asset_path, destination_asset_path)` | Duplicate an asset |
| `duplicate_directory(source_directory_path, destination_directory_path)` | Duplicate an entire directory |
| `duplicate_loaded_asset(source_asset, destination_asset_path)` | Duplicate a loaded asset |
| `find_asset_data(asset_path)` | Get asset metadata |
| `find_package_referencers_for_asset(asset_path, load_assets_to_confirm)` | Find what references an asset |
| `get_assets_by_class(class_name, search_path, recursive)` | Find assets by class type |
| `get_metadata_tag(object, tag)` | Get metadata tag value |
| `get_metadata_tag_values(object, tag)` | Get all values for a metadata tag |
| `get_path_name_for_loaded_asset(loaded_asset)` | Get path for loaded asset |
| `get_tag_values(asset_path, tag_name)` | Get tag values for an asset |
| `list_asset_by_class(class_name, search_path, recursive)` | List asset paths by class |
| `list_assets(directory_path, recursive, include_folder)` | List all assets in directory |
| `load_asset(asset_path)` | Load an asset into memory |
| `load_blueprint_class(asset_path)` | Load a Blueprint class |
| `make_directory(directory_path)` | Create a new directory |
| `rename_asset(source_asset_path, destination_asset_path)` | Rename/move an asset |
| `rename_directory(source_directory_path, destination_directory_path)` | Rename/move a directory |
| `rename_loaded_asset(source_asset, destination_asset_path)` | Rename a loaded asset |
| `save_asset(asset_to_save, only_if_is_dirty)` | Save an asset |
| `save_directory(directory_path, only_if_is_dirty, recursive)` | Save all assets in directory |
| `save_loaded_asset(asset_to_save, only_if_is_dirty)` | Save a loaded asset |
| `save_loaded_assets(assets_to_save, only_if_is_dirty)` | Batch save loaded assets |
| `set_metadata_tag(object, tag, value)` | Set metadata tag on object |
| `sync_browser_to_objects(asset_paths)` | Sync Content Browser to paths |

---

## 2. EditorLevelLibrary

Level and world manipulation in the editor.

| Method | Description |
|--------|-------------|
| `clear_actor_selection_set()` | Deselect all actors |
| `convert_actors(actors, actor_class, static_mesh_package_path)` | Convert actors to different class |
| `create_proxy_mesh_actor(actors_to_merge, merge_options)` | Create proxy/LOD mesh |
| `destroy_actor(actor_to_destroy)` | Remove actor from level |
| `editor_end_play()` | Stop PIE session |
| `editor_invalidate_viewports()` | Refresh viewport rendering |
| `editor_play_simulate()` | Start Simulate in Editor |
| `editor_set_game_view(game_view)` | Toggle game view mode |
| `eject_pilot_level_actor()` | Stop piloting an actor |
| `get_actor_reference(path_to_actor)` | Get actor by path |
| `get_all_level_actors()` | Get all actors in level |
| `get_all_level_actors_components()` | Get all components in level |
| `get_editor_world()` | Get the editor world context |
| `get_game_world()` | Get the game world (PIE) |
| `get_level_viewport_camera_info()` | Get viewport camera transform |
| `get_pie_worlds(include_dedicated_server)` | Get all PIE worlds |
| `get_selected_level_actors()` | Get selected actors |
| `join_static_mesh_actors(actors_to_join, join_options)` | Combine static meshes |
| `load_level(asset_path)` | Load a level |
| `merge_static_mesh_actors(actors_to_merge, merge_options)` | Merge meshes into one |
| `new_level(asset_path)` | Create new empty level |
| `new_level_from_template(asset_path, template_asset_path)` | Create level from template |
| `pilot_level_actor(actor_to_pilot)` | Pilot camera to actor |
| `replace_mesh_components_materials(mesh_components, material_to_replace, new_material)` | Replace materials |
| `replace_mesh_components_materials_on_actors(actors, material_to_replace, new_material)` | Replace materials on actors |
| `replace_mesh_components_meshes(mesh_components, mesh_to_replace, new_mesh)` | Replace meshes |
| `replace_mesh_components_meshes_on_actors(actors, mesh_to_replace, new_mesh)` | Replace meshes on actors |
| `replace_selected_actors(asset_path)` | Replace selected with new actor |
| `save_all_dirty_levels()` | Save all modified levels |
| `save_current_level()` | Save current level |
| `select_nothing()` | Clear selection |
| `set_actor_selection_state(actor, should_be_selected)` | Set actor selection |
| `set_current_level_by_name(level_name)` | Change current level |
| `set_level_viewport_camera_info(camera_location, camera_rotation)` | Set viewport camera |
| `set_selected_level_actors(actors_to_select)` | Select specific actors |
| `spawn_actor_from_class(actor_class, location, rotation, transient)` | Spawn actor from class |
| `spawn_actor_from_object(object_to_use, location, rotation, transient)` | Spawn actor from asset |

---

## 3. EditorActorSubsystem

Modern replacement for EditorLevelLibrary actor operations (UE5+).

| Method | Description |
|--------|-------------|
| `clear_actor_selection_set()` | Deselect all |
| `convert_actors(actors, actor_class, static_mesh_package_path)` | Convert actor types |
| `destroy_actor(actor_to_destroy)` | Delete actor |
| `destroy_actors(actors_to_destroy)` | Batch delete actors |
| `duplicate_actor(actor_to_duplicate, to_world, offset)` | Duplicate with offset |
| `duplicate_actors(actors_to_duplicate, to_world, offset)` | Batch duplicate |
| `duplicate_selected_actors(world)` | Duplicate selection |
| `get_actor_reference(path_to_actor)` | Get actor by path |
| `get_all_level_actors()` | All actors in level |
| `get_all_level_actors_components()` | All components |
| `get_selected_level_actors()` | Selected actors |
| `invert_selection(world)` | Invert selection |
| `select_all(world)` | Select all actors |
| `select_all_children(parent_actor, recursive)` | Select children |
| `select_nothing(world)` | Clear selection |
| `set_actor_selection_state(actor, should_be_selected)` | Toggle selection |
| `set_component_selection_state(component, should_be_selected)` | Select component |
| `set_selected_level_actors(actors_to_select)` | Set selection |
| `spawn_actor_from_class(actor_class, location, rotation, transient)` | Spawn from class |
| `spawn_actor_from_object(object_to_use, location, rotation, transient)` | Spawn from asset |

---

## 4. EditorUtilityLibrary

General editor utility functions.

| Method | Description |
|--------|-------------|
| `get_active_asset_editor_target_classes()` | Get classes of open asset editors |
| `get_current_content_browser_path()` | Get Content Browser location |
| `get_selection_bounds()` | Get bounding box of selection |
| `get_selection_set()` | Get selected objects |
| `get_selected_asset_data()` | Get selected asset metadata |
| `get_selected_assets()` | Get selected assets |
| `get_selected_blueprint_classes()` | Get selected Blueprint classes |
| `get_selected_folder_paths()` | Get selected folders |
| `rename_asset(asset, new_name)` | Rename asset |
| `sync_browser_to_folders(folder_list)` | Navigate to folders |
| `sync_browser_to_objects(objects)` | Navigate to objects |

---

## 5. LevelEditorSubsystem

Level editor operations (UE5 modern API).

| Method | Description |
|--------|-------------|
| `build_light_maps()` | Build lighting |
| `build_reflections()` | Build reflection captures |
| `editor_end_play()` | Stop PIE |
| `editor_invalidate_viewports()` | Refresh viewports |
| `editor_play_simulate()` | Simulate in Editor |
| `editor_request_end_play()` | Request PIE stop |
| `editor_set_game_view(game_view)` | Toggle game view |
| `eject_pilot_level_actor()` | Stop piloting |
| `get_current_level()` | Get current sublevel |
| `get_level_viewport_camera_info()` | Get camera info |
| `get_pilot_level_actor()` | Get piloted actor |
| `get_viewport_config_key(viewport_index)` | Get viewport config |
| `load_level(asset_path)` | Load level |
| `new_level(asset_path)` | New empty level |
| `new_level_from_template(asset_path, template_path)` | New from template |
| `pilot_level_actor(actor_to_pilot)` | Pilot to actor |
| `save_all_dirty_levels()` | Save all dirty levels |
| `save_current_level()` | Save current level |
| `set_allow_light_maps_streaming(allow)` | Toggle lightmap streaming |
| `set_current_level_by_name(level_name)` | Change current level |
| `set_level_viewport_camera_info(camera_location, camera_rotation)` | Set camera |

---

## 6. UnrealEditorSubsystem

Editor-wide operations.

| Method | Description |
|--------|-------------|
| `get_editor_world()` | Get editor world |
| `get_game_world()` | Get PIE world |
| `get_level_viewport_camera_info()` | Get viewport camera |
| `set_level_viewport_camera_info(camera_location, camera_rotation)` | Set viewport camera |

---

## 7. AssetEditorSubsystem

Asset editor window management.

| Method | Description |
|--------|-------------|
| `close_all_editors_for_asset(asset)` | Close editors for asset |
| `open_editor_for_assets(assets)` | Open asset editors |

---

## 8. LevelSequenceEditorBlueprintLibrary

Level Sequence (Sequencer) operations.

| Method | Description |
|--------|-------------|
| `close_level_sequence()` | Close Sequencer |
| `focus_level_sequence()` | Focus Sequencer window |
| `get_bound_objects(binding)` | Get objects for binding |
| `get_current_level_sequence()` | Get open sequence |
| `get_current_local_time()` | Get playhead frame |
| `get_current_time()` | Get playhead time |
| `get_focused_level_sequence()` | Get focused sequence |
| `get_selected_bindings()` | Get selected bindings |
| `get_selected_channels()` | Get selected channels |
| `get_selected_folders()` | Get selected folders |
| `get_selected_keys()` | Get selected keyframes |
| `get_selected_sections()` | Get selected sections |
| `get_selected_tracks()` | Get selected tracks |
| `is_camera_cut_locked_to_viewport()` | Check camera lock |
| `is_level_sequence_locked()` | Check sequence lock |
| `is_playing()` | Check if playing |
| `open_level_sequence(level_sequence)` | Open sequence |
| `pause()` | Pause playback |
| `play()` | Start playback |
| `play_to(time)` | Play to specific time |
| `refresh_current_level_sequence()` | Refresh Sequencer UI |
| `select_bindings(bindings)` | Select bindings |
| `select_channels(channels)` | Select channels |
| `select_folders(folders)` | Select folders |
| `select_keys(keys)` | Select keyframes |
| `select_sections(sections)` | Select sections |
| `select_tracks(tracks)` | Select tracks |
| `set_camera_cut_locked_to_viewport(lock)` | Lock camera to viewport |
| `set_current_local_time(new_frame)` | Set playhead frame |
| `set_current_time(new_time)` | Set playhead time |
| `set_custom_playback_range_end(new_end_frame)` | Set playback end |
| `set_custom_playback_range_start(new_start_frame)` | Set playback start |
| `set_lock_level_sequence(lock)` | Lock/unlock sequence |
| `set_playback_position(position)` | Set playback position |
| `set_random_color_for_channels(channels)` | Randomize channel colors |
| `set_selection_range_end(new_end_frame)` | Set selection end |
| `set_selection_range_start(new_start_frame)` | Set selection start |
| `stop()` | Stop playback |

---

## 9. MoviePipelineBlueprintLibrary

Movie Render Queue operations.

| Method | Description |
|--------|-------------|
| `duplicate_sequence(outer, source_sequence)` | Duplicate sequence |
| `find_or_get_default_executor_for_queue(queue)` | Get queue executor |
| `get_active_movie_pipeline_executor()` | Get active executor |
| `get_completion_percentage(in_pipeline)` | Get render progress |
| `get_current_frame_number(in_pipeline)` | Get current frame |
| `get_current_segment_name(in_pipeline)` | Get segment name |
| `get_current_segment_state(in_pipeline)` | Get segment state |
| `get_estimated_time_remaining(in_pipeline)` | Get time remaining |
| `get_job_author(in_job)` | Get job author |
| `get_job_name(in_job)` | Get job name |
| `get_map_package_name(in_job)` | Get level path |
| `get_movie_pipeline_queue()` | Get render queue |
| `get_pipeline_state(in_pipeline)` | Get pipeline state |
| `is_rendering()` | Check if rendering |
| `render_queue_with_executor(executor_type, queue)` | Start render |
| `resolve_version_number(params, force_version_number)` | Resolve version |
| `update_job_shot_list_from_sequence(level_sequence, job)` | Update shot list |

---

## 10. GameplayStatics

Gameplay utility functions.

| Method | Description |
|--------|-------------|
| `apply_damage(damaged_actor, base_damage, event_instigator, damage_causer, damage_type_class)` | Apply damage |
| `apply_point_damage(damaged_actor, base_damage, hit_from_direction, hit_info, event_instigator, damage_causer, damage_type_class)` | Apply point damage |
| `apply_radial_damage(world_context, base_damage, origin, damage_radius, ...)` | Apply radial damage |
| `begin_deferred_actor_spawn_from_class(world_context, actor_class, spawn_transform, collision_handling, owner)` | Begin deferred spawn |
| `create_player(world_context, controller_id, spawn_pawn)` | Create player controller |
| `create_sound2d(world_context, sound, volume_multiplier, pitch_multiplier, ...)` | Play 2D sound |
| `deproject_screen_to_world(player, screen_position)` | Screen to world coords |
| `finish_spawning_actor(actor, spawn_transform)` | Complete deferred spawn |
| `flush_level_streaming(world_context)` | Flush streaming levels |
| `get_accurate_real_time(world_context)` | Get accurate time |
| `get_actor_array_average_location(actors)` | Average position |
| `get_actor_array_bounds(actors, only_colliding_components)` | Bounding box |
| `get_actor_of_class(world_context, actor_class)` | Find actor by class |
| `get_all_actors_of_class(world_context, actor_class)` | All actors of class |
| `get_all_actors_of_class_with_tag(world_context, actor_class, tag)` | Actors with tag |
| `get_all_actors_with_interface(world_context, interface)` | Actors with interface |
| `get_all_actors_with_tag(world_context, tag)` | Actors with tag |
| `get_current_level_name(world_context, remove_prefix_string)` | Level name |
| `get_game_instance(world_context)` | Game instance |
| `get_game_mode(world_context)` | Game mode |
| `get_game_state(world_context)` | Game state |
| `get_global_time_dilation(world_context)` | Time dilation |
| `get_platform_name()` | Platform name |
| `get_player_camera_manager(world_context, player_index)` | Camera manager |
| `get_player_character(world_context, player_index)` | Player character |
| `get_player_controller(world_context, player_index)` | Player controller |
| `get_player_pawn(world_context, player_index)` | Player pawn |
| `get_real_time_seconds(world_context)` | Real time |
| `get_streaming_level(world_context, package_name)` | Streaming level |
| `get_time_seconds(world_context)` | Game time |
| `get_world_delta_seconds(world_context)` | Delta time |
| `is_game_paused(world_context)` | Check paused |
| `load_stream_level(world_context, level_name, make_visible_after_load, should_block_on_load, out_latent_info)` | Load streaming level |
| `open_level(world_context, level_name, absolute, options)` | Open level |
| `play_dialogue_2d(world_context, dialogue, context, ...)` | Play dialogue |
| `play_dialogue_at_location(world_context, dialogue, context, location, ...)` | Play spatial dialogue |
| `play_sound_2d(world_context, sound, volume_multiplier, ...)` | Play 2D sound |
| `play_sound_at_location(world_context, sound, location, ...)` | Play spatial sound |
| `play_world_camera_shake(world_context, shake, epicenter, ...)` | Camera shake |
| `project_world_to_screen(player, world_position, player_viewport_relative)` | World to screen |
| `save_game_to_slot(save_game_object, slot_name, user_index)` | Save game |
| `set_game_paused(world_context, paused)` | Pause game |
| `set_global_time_dilation(world_context, time_dilation)` | Set time dilation |
| `spawn_decal_at_location(world_context, decal_material, decal_size, location, ...)` | Spawn decal |
| `spawn_emitter_at_location(world_context, emitter_template, location, ...)` | Spawn particles |
| `spawn_object(object_class, outer)` | Spawn UObject |
| `spawn_sound_2d(world_context, sound, ...)` | Spawn 2D sound |
| `spawn_sound_at_location(world_context, sound, location, ...)` | Spawn spatial sound |
| `unload_stream_level(world_context, level_name, out_latent_info, should_block_on_unload)` | Unload streaming level |

---

## 11. KismetSystemLibrary

System-level operations including traces and debugging.

| Method | Description |
|--------|-------------|
| `box_overlap_actors(world_context, box_pos, box_extent, object_types, actor_class_filter, actors_to_ignore)` | Box overlap test |
| `box_trace_multi(world_context, start, end, half_size, orientation, trace_channel, ...)` | Multi box trace |
| `box_trace_single(world_context, start, end, half_size, orientation, trace_channel, ...)` | Single box trace |
| `capsule_overlap_actors(world_context, capsule_pos, radius, half_height, ...)` | Capsule overlap |
| `capsule_trace_multi(world_context, start, end, radius, half_height, ...)` | Multi capsule trace |
| `capsule_trace_single(world_context, start, end, radius, half_height, ...)` | Single capsule trace |
| `collect_garbage()` | Force garbage collection |
| `delay(world_context, duration, out_latent_info)` | Wait for duration |
| `does_implement_interface(test_object, interface)` | Check interface |
| `draw_debug_arrow(world_context, line_start, line_end, arrow_size, line_color, duration, thickness)` | Debug arrow |
| `draw_debug_box(world_context, center, extent, line_color, rotation, duration, thickness)` | Debug box |
| `draw_debug_capsule(world_context, center, half_height, radius, rotation, line_color, duration, thickness)` | Debug capsule |
| `draw_debug_circle(world_context, center, radius, num_segments, line_color, ...)` | Debug circle |
| `draw_debug_cone(world_context, origin, direction, length, angle_width, angle_height, num_sides, line_color, ...)` | Debug cone |
| `draw_debug_cylinder(world_context, start, end, radius, segments, line_color, ...)` | Debug cylinder |
| `draw_debug_float_history_location(world_context, float_history, draw_location, draw_size, draw_color, ...)` | Debug float history |
| `draw_debug_frustum(world_context, frustum_transform, frustum_color, ...)` | Debug frustum |
| `draw_debug_line(world_context, line_start, line_end, line_color, duration, thickness)` | Debug line |
| `draw_debug_plane(world_context, plane_coordinates, location, size, plane_color, ...)` | Debug plane |
| `draw_debug_point(world_context, position, size, point_color, duration)` | Debug point |
| `draw_debug_sphere(world_context, center, radius, segments, line_color, duration, thickness)` | Debug sphere |
| `draw_debug_string(world_context, text_location, text, test_base_actor, text_color, duration)` | Debug text |
| `execute_console_command(world_context, command, specific_player)` | Run console command |
| `flush_debug_strings(world_context)` | Clear debug strings |
| `flush_persistent_debug_lines(world_context)` | Clear debug lines |
| `get_class_display_name(class_)` | Class display name |
| `get_command_line()` | Get command line args |
| `get_console_variable_bool_value(variable_name)` | Get CVar bool |
| `get_console_variable_float_value(variable_name)` | Get CVar float |
| `get_console_variable_int_value(variable_name)` | Get CVar int |
| `get_display_name(object)` | Object display name |
| `get_engine_version()` | Engine version string |
| `get_game_bundle_id()` | Bundle ID |
| `get_game_name()` | Project name |
| `get_object_name(object)` | Object name |
| `get_outer_object(object)` | Get outer object |
| `get_path_name(object)` | Full path name |
| `get_platform_user_dir()` | User directory |
| `get_platform_user_name()` | User name |
| `get_project_content_directory()` | Content directory |
| `get_project_directory()` | Project directory |
| `get_project_saved_directory()` | Saved directory |
| `get_soft_class_reference(object)` | Soft class reference |
| `get_soft_object_reference(object)` | Soft object reference |
| `get_system_path(object)` | System path |
| `get_unique_device_id()` | Device ID |
| `is_dedicated_server(world_context)` | Check dedicated server |
| `is_packaged_for_distribution()` | Check distribution build |
| `is_server(world_context)` | Check if server |
| `is_standalone(world_context)` | Check standalone |
| `is_valid(object)` | Check validity |
| `is_valid_soft_class_reference(soft_class_reference)` | Check soft class |
| `is_valid_soft_object_reference(soft_object_reference)` | Check soft object |
| `line_trace_multi(world_context, start, end, trace_channel, ...)` | Multi line trace |
| `line_trace_single(world_context, start, end, trace_channel, ...)` | Single line trace |
| `load_asset_blocking(soft_object_reference)` | Load asset blocking |
| `load_class_asset_blocking(soft_class_reference)` | Load class blocking |
| `make_literal_bool(value)` | Create bool literal |
| `make_literal_byte(value)` | Create byte literal |
| `make_literal_double(value)` | Create double literal |
| `make_literal_float(value)` | Create float literal |
| `make_literal_int(value)` | Create int literal |
| `make_literal_int64(value)` | Create int64 literal |
| `make_literal_name(value)` | Create name literal |
| `make_literal_string(value)` | Create string literal |
| `make_literal_text(value)` | Create text literal |
| `move_component_to(component, target_relative_location, target_relative_rotation, ...)` | Move component |
| `print_string(world_context, in_string, print_to_screen, print_to_log, text_color, duration)` | Print to screen/log |
| `print_text(world_context, in_text, print_to_screen, print_to_log, text_color, duration)` | Print text |
| `print_warning(in_string)` | Print warning |
| `quit_editor()` | Quit editor |
| `quit_game(world_context, specific_player, quit_preference, ignore_platform_restrictions)` | Quit game |
| `reset_game_and_movie_player()` | Reset game |
| `set_bool_property_by_name(object, property_name, value)` | Set bool property |
| `set_byte_property_by_name(object, property_name, value)` | Set byte property |
| `set_class_property_by_name(object, property_name, value)` | Set class property |
| `set_console_variable_bool_value(variable_name, value)` | Set CVar bool |
| `set_console_variable_float_value(variable_name, value)` | Set CVar float |
| `set_console_variable_int_value(variable_name, value)` | Set CVar int |
| `set_console_variable_string_value(variable_name, value)` | Set CVar string |
| `set_float_property_by_name(object, property_name, value)` | Set float property |
| `set_int_property_by_name(object, property_name, value)` | Set int property |
| `set_int64_property_by_name(object, property_name, value)` | Set int64 property |
| `set_linear_color_property_by_name(object, property_name, value)` | Set color property |
| `set_object_property_by_name(object, property_name, value)` | Set object property |
| `set_rotator_property_by_name(object, property_name, value)` | Set rotator property |
| `set_soft_class_property_by_name(object, property_name, value)` | Set soft class property |
| `set_soft_object_property_by_name(object, property_name, value)` | Set soft object property |
| `set_string_property_by_name(object, property_name, value)` | Set string property |
| `set_struct_property_by_name(object, property_name, value)` | Set struct property |
| `set_suppresses_viewport_transition_message(suppress)` | Suppress messages |
| `set_text_property_by_name(object, property_name, value)` | Set text property |
| `set_transform_property_by_name(object, property_name, value)` | Set transform property |
| `set_vector_property_by_name(object, property_name, value)` | Set vector property |
| `sphere_overlap_actors(world_context, sphere_pos, sphere_radius, ...)` | Sphere overlap |
| `sphere_trace_multi(world_context, start, end, radius, ...)` | Multi sphere trace |
| `sphere_trace_single(world_context, start, end, radius, ...)` | Single sphere trace |
| `stack_trace()` | Print stack trace |
| `transact_object(object)` | Create undo transaction |

---

## 12. KismetMathLibrary

Math utilities.

| Method | Description |
|--------|-------------|
| `abs(a)` | Absolute value |
| `acos(a)` | Arc cosine |
| `add_vectors(a, b)` | Add two vectors |
| `asin(a)` | Arc sine |
| `atan(a)` | Arc tangent |
| `atan2(y, x)` | Arc tangent 2 |
| `break_color(color)` | Break color to RGBA |
| `break_rotator(rotator)` | Break rotator to pitch/yaw/roll |
| `break_transform(transform)` | Break transform to loc/rot/scale |
| `break_vector(vector)` | Break vector to XYZ |
| `clamp(value, min, max)` | Clamp value |
| `clamp_angle(angle, min, max)` | Clamp angle |
| `compose_transforms(a, b)` | Combine transforms |
| `cos(a)` | Cosine |
| `cross(a, b)` | Cross product |
| `degrees_to_radians(a)` | Convert to radians |
| `divide(a, b)` | Divide |
| `dot(a, b)` | Dot product |
| `ease(a, b, alpha, easing_func, blend_exp, steps)` | Easing interpolation |
| `equal(a, b, error_tolerance)` | Check equality with tolerance |
| `exp(a)` | Exponential |
| `find_closest_point_on_line(point, line_origin, line_direction)` | Closest point on line |
| `find_closest_point_on_segment(point, segment_start, segment_end)` | Closest point on segment |
| `find_look_at_rotation(start, target)` | Calculate look-at rotation |
| `floor(a)` | Floor |
| `fmod(dividend, divisor)` | Float modulo |
| `get_ax_vector(rotator)` | Get forward vector |
| `get_direction_unit_vector(from, to)` | Direction between points |
| `get_forward_vector(rotator)` | Get forward vector |
| `get_right_vector(rotator)` | Get right vector |
| `get_up_vector(rotator)` | Get up vector |
| `hypotenuse(width, height)` | Calculate hypotenuse |
| `inverse_transform_direction(transform, direction)` | Inverse transform direction |
| `inverse_transform_location(transform, location)` | Inverse transform location |
| `inverse_transform_rotation(transform, rotation)` | Inverse transform rotation |
| `is_nearly_zero(a, error_tolerance)` | Check near zero |
| `lerp(a, b, alpha)` | Linear interpolation |
| `lerp_using_hsvcolor(a, b, alpha)` | HSV lerp |
| `loge(a)` | Natural log |
| `log10(a)` | Log base 10 |
| `make_color(r, g, b, a)` | Create color |
| `make_plane_from_point_and_normal(point, normal)` | Create plane |
| `make_rotator(pitch, yaw, roll)` | Create rotator |
| `make_transform(location, rotation, scale)` | Create transform |
| `make_vector(x, y, z)` | Create vector |
| `map_range_clamped(value, in_range_a, in_range_b, out_range_a, out_range_b)` | Clamped range map |
| `map_range_unclamped(value, in_range_a, in_range_b, out_range_a, out_range_b)` | Unclamped range map |
| `max(a, b)` | Maximum |
| `min(a, b)` | Minimum |
| `multiply(a, b)` | Multiply |
| `multiply_vectors(a, b)` | Multiply vectors |
| `negate_rotator(rotator)` | Negate rotator |
| `negate_vector(a)` | Negate vector |
| `normalize(a, tolerance)` | Normalize vector |
| `normalize_axis(angle)` | Normalize angle |
| `not_equal(a, b, error_tolerance)` | Check inequality |
| `percent(a, b)` | Percent (modulo) |
| `point_on_segment_closest_to_point(point, segment_start, segment_end)` | Closest point |
| `power(base, exp)` | Power |
| `radians_to_degrees(a)` | Convert to degrees |
| `random_bool()` | Random boolean |
| `random_bool_with_weight(weight)` | Weighted random bool |
| `random_float()` | Random float 0-1 |
| `random_float_in_range(min, max)` | Random float in range |
| `random_integer(max)` | Random integer 0-max |
| `random_integer_in_range(min, max)` | Random int in range |
| `random_point_in_bounding_box(origin, box_extent)` | Random point in box |
| `random_rotator(roll)` | Random rotator |
| `random_unit_vector()` | Random direction |
| `reflect_vector_on_vector_normal(in_vect, surface_normal)` | Reflect vector |
| `rotate_vector_around_axis(in_vect, angle_deg, axis)` | Rotate around axis |
| `round(a)` | Round |
| `safe_divide(a, b, error_tolerance)` | Safe divide with error tolerance |
| `select_color(a, b, pick_a)` | Select color |
| `select_float(a, b, pick_a)` | Select float |
| `select_int(a, b, pick_a)` | Select int |
| `select_rotator(a, b, pick_a)` | Select rotator |
| `select_string(a, b, pick_a)` | Select string |
| `select_transform(a, b, pick_a)` | Select transform |
| `select_vector(a, b, pick_a)` | Select vector |
| `sign_of_float(a)` | Sign of float |
| `sign_of_integer(a)` | Sign of integer |
| `sin(a)` | Sine |
| `sqrt(a)` | Square root |
| `square(a)` | Square |
| `subtract(a, b)` | Subtract |
| `subtract_vectors(a, b)` | Subtract vectors |
| `tan(a)` | Tangent |
| `transform_direction(transform, direction)` | Transform direction |
| `transform_location(transform, location)` | Transform location |
| `transform_rotation(transform, rotation)` | Transform rotation |
| `vector_length(a)` | Vector magnitude |
| `vector_length_squared(a)` | Squared magnitude |
| `vector_length_xy(a)` | XY magnitude |
| `vector_length_xy_squared(a)` | Squared XY magnitude |

---

## 13. KismetStringLibrary

String manipulation utilities.

| Method | Description |
|--------|-------------|
| `build_string_bool(append_to, prefix, in_bool, suffix)` | Build string with bool |
| `build_string_float(append_to, prefix, in_float, suffix)` | Build string with float |
| `build_string_int(append_to, prefix, in_int, suffix)` | Build string with int |
| `build_string_name(append_to, prefix, in_name, suffix)` | Build string with name |
| `build_string_object(append_to, prefix, in_obj, suffix)` | Build string with object |
| `build_string_vector(append_to, prefix, in_vector, suffix)` | Build string with vector |
| `concat_strings(a, b)` | Concatenate strings |
| `contains(search_in, substring, search_case, search_dir)` | Check contains |
| `conv_bool_to_string(in_bool)` | Bool to string |
| `conv_byte_to_string(in_byte)` | Byte to string |
| `conv_color_to_string(in_color)` | Color to string |
| `conv_float_to_string(in_float)` | Float to string |
| `conv_int_to_string(in_int)` | Int to string |
| `conv_int_vector_to_string(in_int_vector)` | IntVector to string |
| `conv_matrix_to_string(in_matrix)` | Matrix to string |
| `conv_name_to_string(in_name)` | Name to string |
| `conv_object_to_string(in_obj)` | Object to string |
| `conv_rotator_to_string(in_rot)` | Rotator to string |
| `conv_string_to_name(in_string)` | String to name |
| `conv_text_to_string(in_text)` | Text to string |
| `conv_transform_to_string(in_trans)` | Transform to string |
| `conv_vector_to_string(in_vec)` | Vector to string |
| `ends_with(source_string, suffix, search_case)` | Check ends with |
| `find_substring(search_in, substring, use_case, search_from_end, start_position)` | Find substring |
| `get_character_array_from_string(source_string)` | String to char array |
| `get_character_as_number(source_string, index)` | Char at index to number |
| `get_substring(source_string, start_index, length)` | Get substring |
| `is_empty(in_string)` | Check if empty |
| `is_numeric(in_string)` | Check if numeric |
| `join_string_array(source_array, separator)` | Join string array |
| `left(source_string, count)` | Left substring |
| `left_chop(source_string, count)` | Left chop |
| `left_pad(source_string, ch_count, padding_char)` | Left pad |
| `len(s)` | String length |
| `matches_wildcard(source_string, wildcard, search_case)` | Wildcard match |
| `mid(source_string, start, count)` | Mid substring |
| `parse_into_array(source_string, delimiter, cull_empty_strings)` | Parse to array |
| `replace(source_string, from, to, search_case)` | Replace substring |
| `replace_inline(source_string, search_text, replacement_text, search_case)` | Replace inline |
| `reverse(source_string)` | Reverse string |
| `right(source_string, count)` | Right substring |
| `right_chop(source_string, count)` | Right chop |
| `right_pad(source_string, ch_count, padding_char)` | Right pad |
| `split(source_string, delimiter, left_s, right_s, search_case, search_dir)` | Split string |
| `starts_with(source_string, prefix, search_case)` | Check starts with |
| `time_seconds_to_string(in_seconds)` | Seconds to time string |
| `to_lower(source_string)` | To lowercase |
| `to_upper(source_string)` | To uppercase |
| `trim(source_string)` | Trim whitespace |
| `trim_trailing(source_string)` | Trim trailing |

---

## 14. BlueprintPathsLibrary

File and path utilities.

| Method | Description |
|--------|-------------|
| `automation_dir()` | Automation directory |
| `automation_log_dir()` | Automation log directory |
| `automation_transient_dir()` | Automation transient directory |
| `bug_it_dir()` | BugIt directory |
| `change_extension(in_path, new_extension)` | Change file extension |
| `cloud_dir()` | Cloud directory |
| `collapse_relative_directories(in_path)` | Collapse relative dirs |
| `combine(path_a, path_b)` | Combine paths |
| `convert_from_sandbox_path(in_path, in_sandbox_name)` | Convert from sandbox |
| `convert_relative_path_to_full(in_path)` | Relative to absolute |
| `convert_to_sandbox_path(in_path, in_sandbox_name)` | Convert to sandbox |
| `create_temp_filename(path, prefix, extension)` | Create temp filename |
| `diff_dir()` | Diff directory |
| `directory_exists(in_path)` | Check directory exists |
| `engine_config_dir()` | Engine config directory |
| `engine_content_dir()` | Engine content directory |
| `engine_dir()` | Engine directory |
| `engine_intermediate_dir()` | Engine intermediate directory |
| `engine_plugins_dir()` | Engine plugins directory |
| `engine_saved_dir()` | Engine saved directory |
| `engine_source_dir()` | Engine source directory |
| `engine_user_dir()` | Engine user directory |
| `engine_version_agnostic_user_dir()` | Engine version agnostic user directory |
| `enterprise_dir()` | Enterprise directory |
| `enterprise_feature_pack_dir()` | Enterprise feature pack directory |
| `enterprise_plugins_dir()` | Enterprise plugins directory |
| `feature_pack_dir()` | Feature pack directory |
| `file_exists(in_path)` | Check file exists |
| `game_agnostic_saved_dir()` | Game agnostic saved directory |
| `game_developers_dir()` | Game developers directory |
| `game_source_dir()` | Game source directory |
| `game_user_developer_dir()` | Game user developer directory |
| `generated_config_dir()` | Generated config directory |
| `get_base_filename(in_path, remove_path)` | Get base filename |
| `get_clean_filename(in_path)` | Get clean filename |
| `get_extension(in_path, include_dot)` | Get extension |
| `get_invalid_file_system_chars()` | Get invalid chars |
| `get_path(in_path)` | Get path |
| `get_project_file_path()` | Get project file path |
| `get_relative_path_to_root()` | Get relative path to root |
| `has_project_persistent_download_dir()` | Check persistent download dir |
| `is_drive(in_path)` | Check if drive |
| `is_project_file_path_set()` | Check if project path set |
| `is_relative(in_path)` | Check if relative |
| `is_restricted_path(in_path)` | Check if restricted |
| `is_same_path(path_a, path_b)` | Check if same path |
| `launch_dir()` | Launch directory |
| `log_dir()` | Log directory |
| `make_path_relative_to(in_path, relative_to)` | Make relative path |
| `make_platform_filename(in_path)` | Make platform filename |
| `make_standard_filename(in_path)` | Make standard filename |
| `make_valid_file_name(in_string, replacement_char)` | Make valid filename |
| `normalize_directory_name(in_path)` | Normalize directory |
| `normalize_filename(in_path)` | Normalize filename |
| `profiling_dir()` | Profiling directory |
| `project_config_dir()` | Project config directory |
| `project_content_dir()` | Project content directory |
| `project_dir()` | Project directory |
| `project_intermediate_dir()` | Project intermediate directory |
| `project_log_dir()` | Project log directory |
| `project_mods_dir()` | Project mods directory |
| `project_persistent_download_dir()` | Project persistent download directory |
| `project_plugins_dir()` | Project plugins directory |
| `project_saved_dir()` | Project saved directory |
| `project_user_dir()` | Project user directory |
| `remove_duplicate_slashes(in_path)` | Remove duplicate slashes |
| `root_dir()` | Root directory |
| `sandbox_dir()` | Sandbox directory |
| `screen_shot_dir()` | Screenshot directory |
| `set_extension(in_path, new_extension)` | Set extension |
| `set_project_file_path(new_game_project_file_path)` | Set project file path |
| `shader_working_dir()` | Shader working directory |
| `should_save_to_user_dir()` | Check should save to user dir |
| `source_config_dir()` | Source config directory |
| `split(in_path)` | Split path |
| `validate_path(in_path)` | Validate path |
| `video_capture_dir()` | Video capture directory |

---

## 15. LiveLinkBlueprintLibrary

LiveLink motion capture/tracking operations.

| Method | Description |
|--------|-------------|
| `add_source_to_subject(source, subject_key)` | Add source to subject |
| `evaluate_live_link_frame(subject_representation, data_type)` | Evaluate frame data |
| `get_available_subject_names()` | Get all subject names |
| `get_live_link_enabled_subject_names(include_virtual_subjects)` | Get enabled subjects |
| `get_live_link_subjects(include_disabled_subjects, include_virtual_subjects)` | Get all subjects |
| `get_specific_live_link_subject_role(subject_key)` | Get subject role |
| `has_subject_frame_data(subject_key)` | Check frame data exists |
| `is_live_link_subject_enabled(subject_key)` | Check subject enabled |
| `is_source_still_valid(handle)` | Check source valid |
| `is_specific_live_link_subject_enabled(subject_key, enabled)` | Check specific subject enabled |
| `remove_source(handle)` | Remove source |
| `set_live_link_subject_enabled(subject_key, enabled)` | Enable/disable subject |

---

## 16. AnimationLibrary

Animation Blueprint and sequence operations.

| Method | Description |
|--------|-------------|
| `add_animation_notify_event(animation_sequence, track_name, start_time, notify_class)` | Add notify event |
| `add_curve(animation_sequence, curve_name, curve_type, metadata_exists)` | Add animation curve |
| `add_metadata_to_animation_asset(animation_asset, metadata_class)` | Add metadata |
| `copy_animation_curves(source_animation, destination_animation, curve_names)` | Copy curves |
| `does_bone_name_exist(animation_sequence, bone_name, skeleton)` | Check bone exists |
| `does_contain_transform_curve_only(animation_sequence, bone_name)` | Check transform curve |
| `get_anim_notify_event_trigger_time(notify_event)` | Get notify trigger time |
| `get_animation_curve_names(animation_sequence, curve_type)` | Get curve names |
| `get_animation_graph_nodes_of_class(animation_blueprint, node_class, include_child_classes)` | Get graph nodes |
| `get_animation_notify_events(animation_sequence)` | Get notify events |
| `get_animation_notify_event_names(animation_sequence)` | Get notify names |
| `get_animation_sync_markers(animation_sequence)` | Get sync markers |
| `get_animation_sync_marker_names(animation_sequence)` | Get marker names |
| `get_bone_compression_settings(animation_sequence)` | Get bone compression |
| `get_bone_pose_for_frame(animation_sequence, bone_name, frame, extract_root_motion)` | Get bone pose |
| `get_bone_pose_for_time(animation_sequence, bone_name, time, extract_root_motion)` | Get bone pose at time |
| `get_bone_poses_for_frame(animation_sequence, bone_names, frame, extract_root_motion, preview_mesh)` | Get multiple bone poses |
| `get_bone_poses_for_time(animation_sequence, bone_names, time, extract_root_motion, preview_mesh)` | Get poses at time |
| `get_curve_compression_settings(animation_sequence)` | Get curve compression |
| `get_float_keys(animation_sequence, curve_name)` | Get float curve keys |
| `get_metadata_for_sequence(animation_sequence)` | Get sequence metadata |
| `get_num_frames(animation_sequence)` | Get frame count |
| `get_rate_scale(animation_sequence)` | Get rate scale |
| `get_root_motion_lock_type(animation_sequence)` | Get root motion lock |
| `get_sequence_length(animation_sequence)` | Get sequence length |
| `get_time_at_frame(animation_sequence, frame)` | Get time at frame |
| `get_transform_keys(animation_sequence, curve_name)` | Get transform keys |
| `get_unique_marker_names(animation_sequence)` | Get unique markers |
| `is_root_motion_enabled(animation_sequence)` | Check root motion |
| `is_valid_anim_notify_track_name(animation_sequence, track_name)` | Validate track name |
| `remove_all_animation_notify_tracks(animation_sequence)` | Remove all notify tracks |
| `remove_all_animation_sync_markers(animation_sequence)` | Remove all markers |
| `remove_all_curve_data(animation_sequence)` | Remove all curves |
| `remove_all_metadata_from_animation_asset(animation_asset)` | Remove all metadata |
| `remove_animation_notify_events_by_name(animation_sequence, notify_name)` | Remove notifies by name |
| `remove_animation_notify_events_by_track(animation_sequence, track_name)` | Remove notifies by track |
| `remove_animation_notify_track(animation_sequence, track_name)` | Remove notify track |
| `remove_animation_sync_markers_by_name(animation_sequence, marker_name)` | Remove markers by name |
| `remove_animation_sync_markers_by_track(animation_sequence, track_name)` | Remove markers by track |
| `remove_bone_animation(animation_sequence, bone_name, include_children, finalize)` | Remove bone animation |
| `remove_curve(animation_sequence, curve_name, remove_name_from_skeleton)` | Remove curve |
| `remove_metadata_from_animation_asset(animation_asset, metadata_class)` | Remove metadata |
| `remove_virtual_bone(animation_sequence, bone_name)` | Remove virtual bone |
| `remove_virtual_bones(animation_sequence, bone_names)` | Remove virtual bones |
| `set_bone_compression_settings(animation_sequence, bone_compression_settings)` | Set bone compression |
| `set_curve_compression_settings(animation_sequence, curve_compression_settings)` | Set curve compression |
| `set_rate_scale(animation_sequence, rate_scale)` | Set rate scale |
| `set_root_motion_enabled(animation_sequence, enabled)` | Enable root motion |
| `set_root_motion_lock_type(animation_sequence, root_motion_lock_type)` | Set root motion lock |

---

## 17. Object Property Access

All UObjects support these methods for property manipulation.

| Method | Description |
|--------|-------------|
| `get_editor_property(property_name)` | Get any UPROPERTY value |
| `set_editor_property(property_name, value)` | Set any UPROPERTY value |
| `get_class()` | Get UClass |
| `get_name()` | Get object name |
| `get_path_name()` | Get full path name |
| `get_outer()` | Get outer object |
| `get_outermost()` | Get outermost object |
| `is_a(class_type)` | Check class inheritance |
| `cast(class_type)` | Cast to specific class |
| `modify()` | Mark object modified |
| `rename(new_name, new_outer, flags)` | Rename object |

---

## 18. StaticMeshEditorSubsystem

Static mesh editing operations.

| Method | Description |
|--------|-------------|
| `add_simple_collisions(static_mesh, shape_type)` | Add simple collision |
| `add_uv_channel(static_mesh, lod_index)` | Add UV channel |
| `bulk_set_convex_decomposition_collisions(static_meshes, hull_count, max_hull_verts, hull_precision)` | Bulk set convex collision |
| `enable_section_cast_shadow(static_mesh, shadow_casting_enabled, lod_index, section_index)` | Enable section shadow |
| `enable_section_collision(static_mesh, collision_enabled, lod_index, section_index)` | Enable section collision |
| `generate_planar_uv_channel(static_mesh, lod_index, uv_channel_index, position, orientation, tiling)` | Generate planar UVs |
| `get_collision_complexity(static_mesh)` | Get collision complexity |
| `get_convex_decomposition_collisions(static_mesh, hull_count, max_hull_verts, hull_precision)` | Get convex collision |
| `get_lod_build_settings(static_mesh, lod_index)` | Get LOD build settings |
| `get_lod_count(static_mesh)` | Get LOD count |
| `get_lod_reduction_settings(static_mesh, lod_index)` | Get LOD reduction settings |
| `get_lod_screen_sizes(static_mesh)` | Get LOD screen sizes |
| `get_nanite_settings(static_mesh)` | Get Nanite settings |
| `get_number_materials(static_mesh)` | Get material count |
| `get_number_verts(static_mesh, lod_index)` | Get vertex count |
| `get_simple_collision_count(static_mesh)` | Get simple collision count |
| `has_instance_vertex_colors(static_mesh_component)` | Check instance vertex colors |
| `has_vertex_colors(static_mesh)` | Check vertex colors |
| `insert_uv_channel(static_mesh, lod_index, uv_channel_index)` | Insert UV channel |
| `is_section_collision_enabled(static_mesh, lod_index, section_index)` | Check section collision |
| `remove_collisions(static_mesh)` | Remove all collision |
| `remove_collisions_with_notification(static_mesh, apply_changes)` | Remove collision with notify |
| `remove_lods(static_mesh)` | Remove all LODs |
| `remove_uv_channel(static_mesh, lod_index, uv_channel_index)` | Remove UV channel |
| `set_allow_cpu_access(static_mesh, allow_cpu_access)` | Set CPU access |
| `set_collision_complexity(static_mesh, collision_complexity)` | Set collision complexity |
| `set_convex_decomposition_collisions(static_mesh, hull_count, max_hull_verts, hull_precision)` | Set convex collision |
| `set_convex_decomposition_collisions_with_notification(static_mesh, hull_count, max_hull_verts, hull_precision, apply_changes)` | Set convex with notify |
| `set_generate_lightmap_uvs(static_mesh, generate_lightmap_uvs)` | Set generate lightmap UVs |
| `set_lod_build_settings(static_mesh, lod_index, build_options)` | Set LOD build settings |
| `set_lod_count(static_mesh, lod_count)` | Set LOD count |
| `set_lod_from_static_mesh(destination_static_mesh, destination_lod_index, source_static_mesh, source_lod_index, recompute_tangents)` | Copy LOD from mesh |
| `set_lod_reduction_settings(static_mesh, lod_index, reduction_options)` | Set LOD reduction |
| `set_lod_screen_size(static_mesh, lod_index, screen_size)` | Set LOD screen size |
| `set_lods(static_mesh, reduction_options)` | Set all LODs |
| `set_lods_with_notification(static_mesh, reduction_options, apply_changes)` | Set LODs with notify |
| `set_nanite_settings(static_mesh, nanite_settings)` | Set Nanite settings |

---

## 19. SkeletalMeshEditorSubsystem

Skeletal mesh editing operations.

| Method | Description |
|--------|-------------|
| `get_lod_build_settings(skeletal_mesh, lod_index)` | Get LOD build settings |
| `get_lod_count(skeletal_mesh)` | Get LOD count |
| `get_lod_material_slot(skeletal_mesh, lod_index, section_index)` | Get LOD material slot |
| `get_num_sections(skeletal_mesh, lod_index)` | Get section count |
| `get_num_verts(skeletal_mesh, lod_index)` | Get vertex count |
| `import_lod(base_mesh, lod_index, source_filename)` | Import LOD from file |
| `regenerate_lod(skeletal_mesh, new_lod_count, regenerate_even_if_imported, generate_base_lod)` | Regenerate LODs |
| `reimport_all_custom_lo_ds(skeletal_mesh)` | Reimport all custom LODs |
| `set_lod_build_settings(skeletal_mesh, lod_index, build_options)` | Set LOD build settings |
| `set_section_cast_shadow(skeletal_mesh, lod_index, section_index, cast_shadow)` | Set section shadow |
| `set_section_recompute_tangent(skeletal_mesh, lod_index, section_index, recompute_tangent)` | Set section recompute tangent |
| `set_section_recompute_tangent_vertices_mask_channel(skeletal_mesh, lod_index, section_index, vertices_mask_channel)` | Set recompute tangent mask |
| `set_section_visible_in_ray_tracing(skeletal_mesh, lod_index, section_index, visible_in_ray_tracing)` | Set ray tracing visibility |
| `strip_lod_geometry(skeletal_mesh, lod_index, texture_mask, threshold)` | Strip LOD geometry |

---

## 20. Import/Export Functions

Asset import operations.

| Method | Description |
|--------|-------------|
| `import_asset(filename, destination_path)` | Import single asset |
| `import_assets(files_to_import, destination_path, options)` | Import multiple assets |
| `export_assets(assets_to_export, export_path)` | Export assets |
| `reimport_asset(asset)` | Reimport existing asset |
| `get_asset_import_data(asset)` | Get import metadata |
| `set_asset_import_data(asset, import_data)` | Set import metadata |

---

## Summary

This document contains the complete list of available Python API methods through the `unreal` module. Key libraries:

1. **EditorAssetLibrary** - Asset CRUD operations
2. **EditorLevelLibrary** - Level/world manipulation
3. **EditorActorSubsystem** - Actor operations (UE5+)
4. **EditorUtilityLibrary** - Editor utilities
5. **LevelEditorSubsystem** - Level editor control
6. **UnrealEditorSubsystem** - Editor-wide operations
7. **AssetEditorSubsystem** - Asset editor windows
8. **LevelSequenceEditorBlueprintLibrary** - Sequencer operations
9. **MoviePipelineBlueprintLibrary** - Movie render queue
10. **GameplayStatics** - Gameplay utilities
11. **KismetSystemLibrary** - System/trace operations
12. **KismetMathLibrary** - Math utilities
13. **KismetStringLibrary** - String manipulation
14. **BlueprintPathsLibrary** - File/path utilities
15. **LiveLinkBlueprintLibrary** - Motion capture/tracking
16. **AnimationLibrary** - Animation operations
17. **Object Property Access** - UPROPERTY get/set
18. **StaticMeshEditorSubsystem** - Static mesh editing
19. **SkeletalMeshEditorSubsystem** - Skeletal mesh editing
20. **Import/Export Functions** - Asset import/export

For any operation not listed here, check the Unreal Python API documentation or use `dir(unreal.ClassName)` to enumerate available methods.
