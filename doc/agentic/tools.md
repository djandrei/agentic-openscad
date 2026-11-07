# Agentic-OpenSCAD tool reference

Generated from `tools/list` of `openscad --mcp-stdio` (41 tools). Arguments marked * are required. Every project-scoped tool accepts an optional `project` argument (the project folder); it defaults to the current project.

## Project and session

### `version`

Report the OpenSCAD version, the MCP server name and protocol version, and the run mode.


### `status`

Report the run mode, the open projects and which one is current.


### `project_open`

Open a folder as a project (creating .openscad-project.json if missing) and make it the current project. In GUI mode this shows the project in a window. Returns the project info.

| argument | type | description |
|---|---|---|
| `path`* | string | Absolute path of the project folder (must exist). |

### `project_list`

List the open projects.


### `project_info`

Describe a project: root folder, main file, and the contents of .openscad-project.json.


### `project_set`

Change project settings in .openscad-project.json: the main file (the one previewed and rendered by default) and autoPreview (GUI: preview after each write to the main file).

| argument | type | description |
|---|---|---|
| `autoPreview` | boolean |  |
| `main` | string | Main .scad file, relative to the root. |

## Files and editor

### `file_list`

List the files of the project (paths relative to the project root) with size and version. Hidden entries are skipped unless includeHidden is set.

| argument | type | description |
|---|---|---|
| `dir` | string | Sub-folder to list; default is the project root. |
| `includeHidden` | boolean | Default false. |
| `recursive` | boolean | Default true. |

### `file_read`

Read a project file. Returns the content (or a window of lines when startLine/lineCount are given) and the file version to pass as baseVersion to file_write.

| argument | type | description |
|---|---|---|
| `lineCount` | integer | Number of lines to return. |
| `path`* | string | File path relative to the project root. |
| `startLine` | integer | First line (1-based). |

### `file_write`

Create or replace a project file. Pass either `content` (the whole file) or `edits` (exact text replacements applied in order; each `old` must occur exactly once unless replaceAll). Pass `baseVersion` from file_read to fail safely if the file changed meanwhile. In GUI mode the file is opened in a tab and the change is highlighted for the user. Fails when the user has unsaved edits in that file unless `force` is set.

| argument | type | description |
|---|---|---|
| `baseVersion` | string | Version the edit is based on. |
| `content` | string | Whole new content. |
| `edits` | array |  |
| `force` | boolean | Overwrite unsaved editor changes. |
| `path`* | string | File path relative to the project root; folders are created. |

### `file_rename`

Rename or move a project file (the target must not exist).

| argument | type | description |
|---|---|---|
| `from`* | string | Current path relative to the project root. |
| `to`* | string | New path relative to the project root. |

### `file_delete`

Delete a project file.

| argument | type | description |
|---|---|---|
| `path`* | string | File path relative to the project root. |

### `editor_get`

Return what the editor currently shows for a file (GUI mode), including unsaved user edits, with its version. Falls back to the file on disk when the file is not open or in headless mode. Without a path, the project's main file is used.

| argument | type | description |
|---|---|---|
| `path` | string | File path relative to the project root. |

## Compiling

### `check`

Parse a file without evaluating it. Fast syntax check; returns parser errors with line and column.

| argument | type | description |
|---|---|---|
| `file` | string | File to compile, relative to the project root. Defaults to the project's main file. |

### `preview`

Evaluate a file the way F5 does: parse, run the script and build the CSG tree. Returns every message of the run (errors, warnings, echo output) with file/line, counts and timing. In GUI mode the result is shown in the 3D view.

| argument | type | description |
|---|---|---|
| `file` | string | File to compile, relative to the project root. Defaults to the project's main file. |
| `timeoutSeconds` | number | Seconds before evaluation is cancelled. Default 120. |

### `render`

Evaluate a file the way F6 does: full geometry evaluation. Slower than preview but yields the final mesh; the result includes a geometry summary (dimension, facets, bounding box) besides the messages. In GUI mode the mesh is shown in the 3D view.

| argument | type | description |
|---|---|---|
| `file` | string | File to compile, relative to the project root. Defaults to the project's main file. |
| `timeoutSeconds` | number | Seconds before evaluation is cancelled. Default 120. |

### `ast_dump`

Return the parsed abstract syntax tree of a file as OpenSCAD source text.

| argument | type | description |
|---|---|---|
| `file` | string | File to compile, relative to the project root. Defaults to the project's main file. |

### `csg_tree_dump`

Evaluate a file and return the resulting CSG tree (the .csg export: every primitive with its resolved parameters and transforms).

| argument | type | description |
|---|---|---|
| `file` | string | File to compile, relative to the project root. Defaults to the project's main file. |
| `timeoutSeconds` | number | Seconds before evaluation is cancelled. Default 120. |

### `csg_products_dump`

Evaluate a file and return the normalized CSG products (the .term export) used for preview.

| argument | type | description |
|---|---|---|
| `file` | string | File to compile, relative to the project root. Defaults to the project's main file. |
| `timeoutSeconds` | number | Seconds before evaluation is cancelled. Default 120. |

### `log_get`

Return OpenSCAD's log messages with sequence numbers greater than `since` (all modes, all projects), oldest first. Use the last `seq` you saw to page.

| argument | type | description |
|---|---|---|
| `limit` | integer | Default 500. |
| `since` | integer | Default 0. |

## View, captures, geometry, export

### `camera_get`

Return the current camera: vpt (target), vpr (rotation), vpd (distance), fov, projection.


### `camera_set`

Change the camera. Any subset of preset, vpt, vpr, vpd, fov, projection and viewAll may be given; a preset or viewAll fits the model into view. Returns the new camera.

| argument | type | description |
|---|---|---|
| `fov` | number | Field of view in degrees (as $vpf). |
| `preset` | string (back, bottom, diagonal, front, iso, left, right, top) | Named orientation; fits the whole model into view. |
| `projection` | string (perspective, orthogonal) |  |
| `viewAll` | boolean | Fit the whole model into view. |
| `vpd` | number | Distance (as $vpd). |
| `vpr` | array | Rotation in degrees (as $vpr). |
| `vpt` | array | Camera target (as $vpt). |

### `view_options`

Get or set view options: edges, axes, crosshairs, scales, colorScheme, and the view mode (preview = F5 look, render = F6 mesh, thrownTogether). Without arguments it only reports the current values and the available color schemes.

| argument | type | description |
|---|---|---|
| `axes` | boolean |  |
| `colorScheme` | string | One of the names reported by view_options. |
| `crosshairs` | boolean |  |
| `edges` | boolean |  |
| `mode` | string (preview, render, thrownTogether) |  |
| `scales` | boolean | Scale markers on the axes. |

### `view_capture`

Capture PNG images of the model from one or more views and return them as image content. In GUI mode the capture shows what the user sees (the 3D view, scaled to the requested size); in headless mode the model is drawn offscreen (preview or render look). Use render mode after `render` to see the final mesh. Edge/axis/color overrides apply to the capture only.

| argument | type | description |
|---|---|---|
| `axes` | boolean |  |
| `colorScheme` | string | One of the names reported by view_options. |
| `crosshairs` | boolean |  |
| `edges` | boolean |  |
| `height` | integer | Default 600. |
| `keep` | boolean | Keep the last captured view's camera afterwards. |
| `mode` | string (preview, render, thrownTogether) | What to draw; default is the current view mode. |
| `scales` | boolean | Scale markers on the axes. |
| `timeoutSeconds` | number |  |
| `views` | array | Views to capture: "current", a preset name (top, bottom, left, right, front, back, diagonal/iso), or a camera spec object (optionally with a name). Default ["current"]. |
| `width` | integer | Default 800. |

### `geometry_info`

Statistics of the last rendered geometry (call render first): dimension, bounding box, facet and vertex counts, manifoldness, volume and surface area (3D) or area and contours (2D).


### `export`

Write the last rendered geometry (call render first) to a file inside the project. Formats: stl (ascii), binstl, obj, off, wrl, 3mf, pov (3D); svg, dxf, pdf (2D); png (image of the current camera). Options use OpenSCAD's export settings, e.g. {"export-pdf/paper-size": "a4"}.

| argument | type | description |
|---|---|---|
| `format`* | string (stl, asciistl, binstl, obj, off, wrl, 3mf, pov, svg, dxf, pdf, png) |  |
| `options` | object | Export settings as section/key: value. |
| `path`* | string | Target path relative to the project root. |

## Customizer

### `params_get`

List the customizer parameters a file declares (top-level assignments with their customizer comments: caption, group, range/step or options) with their current values, the active overrides and the preset names.

| argument | type | description |
|---|---|---|
| `file` | string | File relative to the project root; defaults to the project's main file. |

### `params_set`

Set customizer parameter values for a file (numbers, booleans, strings or vectors by parameter name), select a saved preset, or reset to the design defaults. The values apply to the next preview/render; in GUI mode the Customizer panel shows them. Returns params_get.

| argument | type | description |
|---|---|---|
| `file` | string | File relative to the project root; defaults to the project's main file. |
| `preset` | string | Preset to load before applying values. |
| `reset` | boolean | Drop all overrides first. |
| `values` | object | Parameter name to value. |

### `params_presets_list`

List the customizer presets stored next to a file (<file>.json) with their values.

| argument | type | description |
|---|---|---|
| `file` | string | File relative to the project root; defaults to the project's main file. |

### `params_preset_save`

Save the current parameter values as a named preset in <file>.json (replacing a preset of the same name).

| argument | type | description |
|---|---|---|
| `file` | string | File relative to the project root; defaults to the project's main file. |
| `name`* | string |  |

## Libraries

### `library_catalog`

List the OpenSCAD libraries available for installation (bundled registry seeded from openscad.org plus the project's registries) with description, repository, docs URL and the include/use line to write. `installed` tells whether the project already has it.

| argument | type | description |
|---|---|---|
| `query` | string | Substring of name or description. |

### `library_installed`

List the libraries installed in the project's libraries folder.


### `library_install`

Install a library into <project>/libraries/<name> so `use <name/file.scad>` works. `library` is a catalog name (e.g. BOSL2), a git repository URL on GitHub/GitLab/Codeberg (archive download) or elsewhere (git clone), or a zip URL/path. Reinstalls if already present.

| argument | type | description |
|---|---|---|
| `dirName` | string | Folder name under libraries/. |
| `library`* | string | Catalog name, repository URL or zip. |
| `ref` | string | Branch or tag; default main/master. |

### `library_remove`

Remove an installed library folder and forget it in the project config.

| argument | type | description |
|---|---|---|
| `name`* | string |  |

### `library_files`

List the files of an installed library. With `describe`, parse its .scad files and report the top-level modules and functions with their parameter names (use `filter` to narrow to a file).

| argument | type | description |
|---|---|---|
| `describe` | boolean |  |
| `filter` | string | Substring of the file path. |
| `name`* | string |  |

### `library_registry_list`

List the library registries the project consults.


### `library_registry_add`

Add a registry (URL or project-relative path of a JSON file with {"name", "libraries": [{name, repository, description, ref, docs, usage}]}) to the project; its entries extend or override the bundled catalog.

| argument | type | description |
|---|---|---|
| `location`* | string |  |

## Documentation

### `docs_search`

Search the OpenSCAD documentation: builtin reference (signatures and semantics), the offline User Manual and cheat sheet (downloaded once with docs_update), and the README/markdown files of the project's installed libraries. Returns ranked hits with snippets; read a hit with docs_page.

| argument | type | description |
|---|---|---|
| `limit` | integer | Default 8. |
| `query`* | string |  |
| `scope` | string (all, manual, cheatsheet, builtins, libraries) |  |

### `docs_page`

Read a documentation page by id: 'builtins' (all builtins), 'builtins/<name>', 'cheatsheet', 'manual' (list of pages), 'manual/<Page>' (e.g. manual/Primitive_Solids) or 'library/<name>/<file>' for an installed library's documentation.

| argument | type | description |
|---|---|---|
| `id`* | string |  |
| `maxChars` | integer | Default 40000. |

### `docs_builtin`

Reference for one builtin module, function, keyword or special variable: call signatures, a summary of its semantics, and the best matching manual sections.

| argument | type | description |
|---|---|---|
| `name`* | string |  |

### `docs_status`

Report what documentation is available locally and where it is cached.


### `docs_update`

Download the offline User Manual and the cheat sheet from openscad.org into the local cache (network access) and rebuild the search index. Needed once before manual searches work.

| argument | type | description |
|---|---|---|
| `force` | boolean | Re-download even if cached. |

## Resources

- `openscad://docs/builtins`: Signatures and semantics of every builtin module, function and special variable
- `openscad://docs/cheatsheet`: The openscad.org cheat sheet as text (after docs_update)
- `openscad://docs/manual`: List of the offline manual pages (after docs_update)
- `openscad://docs/manual/{page}`: One page of the offline User Manual
- `openscad://docs/builtins/{name}`: Signatures and semantics of one builtin
- `openscad://library/{name}/{file}`: README or markdown file of an installed library

## Prompts

- `openscad-language-rules`: Rules of the OpenSCAD language that commonly trip up generated code
- `openscad-workflow`: How to iterate on a model with this server's tools
- `library-usage` (name): How to use a library from the catalog (include line, docs, README excerpt when installed)
